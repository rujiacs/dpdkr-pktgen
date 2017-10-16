#include <rte_mbuf.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_config.h>
#include <rte_mempool.h>
#include <rte_ethdev.h>
#include <rte_eth_ring.h>

#include <getopt.h>
#include <signal.h>

#include "util.h"
#include "stat.h"
#include "rxtx.h"
#include "control.h"
#include "pkt_seq.h"

#define CLIENT_RXQ_NAME "dpdkr%u_tx"
#define CLIENT_TXQ_NAME "dpdkr%u_rx"
#define CLIENT_MP_NAME_PREFIX	"ovs_mp_2030_0"
#define CLIENT_MP_PREFIX_LEN 13

static unsigned int client_id = -1;

struct lcore_param {
	bool is_rx;
	bool is_tx;
	bool is_stat;
};

#define LCORE_MAX 3

static struct lcore_param lcore_param[RTE_MAX_LCORE] = {
	{
		.is_rx = false,
		.is_tx = false,
		.is_stat = false,
	},
};

//static struct rte_ring *rx_ring;
//static struct rte_ring *tx_ring;
static int ring_portid;
static struct rte_mempool *mp = NULL;

static struct pkt_seq_info pkt_seq;

static const char *__get_rxq_name(unsigned int id)
{
	static char buffer[RTE_RING_NAMESIZE];

	snprintf(buffer, sizeof(buffer), CLIENT_RXQ_NAME, id);
	return buffer;
}

static const char *__get_txq_name(unsigned int id)
{
	static char buffer[RTE_RING_NAMESIZE];

	snprintf(buffer, sizeof(buffer), CLIENT_TXQ_NAME, id);
	return buffer;
}

static int __parse_client_num(const char *client)
{
	if (str_to_uint(client, 10, &client_id)) {
		return 0;
	}
	return -1;
}

static void __usage(const char *progname)
{
	LOG_INFO("Usage: %s [<EAL args> --proc-type=secondary] -- -n <client id>"
					" -r <TX rate>",
					progname);
}

static int __parse_options(int argc, char *argv[])
{
	int opt = 0;
	char **argvopt = argv;
	const char *progname = NULL;

	progname = argv[0];

	while ((opt = getopt(argc, argvopt, "n:r:")) != -1) {
		switch(opt) {
			case 'n':
				if (__parse_client_num(optarg) != 0) {
					LOG_ERROR("Wrong client id %s", optarg);
					__usage(progname);
					return -1;
				}
				break;
			case 'r':
				rxtx_set_rate(optarg);
				break;
			default:
				__usage(progname);
				return -1;
		}
	}
	return 0;
}

/* return value: if need to create stats thread */
static bool __set_lcore(void)
{
	unsigned core = 0;
	unsigned used_core[3] = {UINT_MAX}, used = 0;

	for (core = 0; core < RTE_MAX_LCORE; core++) {
		if (rte_lcore_is_enabled(core) == 0)
			continue;

		used_core[used] = core;
		used++;
		if (used >= 3)
			break;
	}

	if (used == 1) {
		lcore_param[used_core[0]].is_rx = true;
		lcore_param[used_core[0]].is_tx = true;
		return true;
	} else if (used == 2) {
		lcore_param[used_core[0]].is_rx = true;
		lcore_param[used_core[1]].is_tx = true;
		return true;
	} else if (used == 3) {
		lcore_param[used_core[0]].is_rx = true;
		lcore_param[used_core[1]].is_tx = true;
		lcore_param[used_core[2]].is_stat = true;
		return false;
	}
	return false;
}

static int __lcore_main(__attribute__((__unused__))void *arg)
{
	unsigned lcoreid;
	struct lcore_param *param;

	lcoreid = rte_lcore_id();
//	if (lcoreid >= LCORE_MAX)
//		return 0;

	LOG_INFO("lcore %u started.", lcoreid);
	param = &lcore_param[lcoreid];

	if (param->is_stat) {
		stat_thread_run();
	} else if (param->is_rx && param->is_tx) {
		rxtx_thread_run_rxtx(ring_portid, mp, &pkt_seq);
	} else if (param->is_rx) {
		rxtx_thread_run_rx(ring_portid);
	} else if (param->is_tx) {
		rxtx_thread_run_tx(ring_portid, mp, &pkt_seq);
	}

	LOG_INFO("lcore %u finished.", lcoreid);
	return 0;
}

static void __mempool_walk_func(struct rte_mempool *p,
				void *arg)
{
	struct rte_mempool **target = arg;

	LOG_INFO("mempool %s", p->name);
	if (strncmp(p->name, CLIENT_MP_NAME_PREFIX,
							strlen(CLIENT_MP_NAME_PREFIX)) == 0) {
		if (*target == NULL)
			*target = p;
	}
}

static struct rte_mempool *__lookup_mempool(void)
{
	struct rte_mempool *p = NULL;

	rte_mempool_walk(__mempool_walk_func, (void*)&p);
	if (p == NULL) {
		LOG_ERROR("Cannot find mempool");
		return NULL;
	}
	return p;
}

static int __get_ring_dev(unsigned int id)
{
	char buf[10] = {'\0'};
	struct rte_ring *tx_ring = NULL, *rx_ring = NULL;
	struct rte_eth_conf conf;
	int ret = 0;

	rx_ring = rte_ring_lookup(__get_rxq_name(id));
	if (rx_ring == NULL) {
		LOG_ERROR("Cannot get RX ring for client %u", id);
		return -1;
	}

	tx_ring = rte_ring_lookup(__get_txq_name(id));
	if (tx_ring == NULL) {
		LOG_ERROR("Cannot get TX ring for client %u", id);
		return -1;
	}

	sprintf(buf, "dpdkr%d", id);
	ring_portid = rte_eth_from_rings(buf, &rx_ring, 1, &tx_ring, 1, 0);
	if (ring_portid < 0) {
		LOG_ERROR("Failed to create dev from ring %u", id);
		return -1;
	}

	/* Find mempool created by ovs */
	mp = __lookup_mempool();
	if (mp != NULL) {
		LOG_INFO("Found mempool %s", mp->name);
	} else {
		LOG_ERROR("Cannot find mempool for dpdkr%u", id);
		return -1;
	}

	/* Setup interface */
	memset(&conf, 0, sizeof(conf));
	ret = rte_eth_dev_configure(ring_portid, 1, 1, &conf);
	if (ret) {
		LOG_ERROR("Failed to configure %s", buf);
		return -1;
	}

	/* Setup tx queue */
	ret = rte_eth_tx_queue_setup(ring_portid, 0, 2048, 0, NULL);
	if (ret) {
		LOG_ERROR("Failed to setup tx queue");
		return -1;
	}

	/* Setup rx queue */
	ret = rte_eth_rx_queue_setup(ring_portid, 0, 2048, 0,
						NULL, mp);
	if (ret) {
		LOG_ERROR("Failed to setup rx queue");
		return -1;
	}
	return ring_portid;
}

int main(int argc, char *argv[])
{
	int retval = 0;
	int coreid = 0;
	bool is_create_stat = false;
	pthread_t tid;

	if ((retval = rte_eal_init(argc, argv)) < 0) {
		LOG_ERROR("Failed to initialize dpdk eal");
		return -1;
	}

	argc -= retval;
	argv += retval;

	if (__parse_options(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");
	}

	signal(SIGINT, ctl_signal_handler);
	signal(SIGTERM, ctl_signal_handler);

	if (__get_ring_dev(client_id) < 0) {
		rte_exit(EXIT_FAILURE, "Failed to get dpdkr device\n");
	}

	/* Start device */
	if (rte_eth_dev_start(ring_portid) < 0) {
		rte_exit(EXIT_FAILURE, "Cannot start dpdkr device\n");
	}

	pkt_seq_init(&pkt_seq);

	is_create_stat = __set_lcore();
	if (is_create_stat) {
		if (pthread_create(&tid, NULL, (void *)stat_thread_run, NULL)) {
			rte_exit(EXIT_FAILURE, "Cannot create statistics thread\n");
		}
	}

	LOG_INFO("Processing client %u", client_id);

	retval = rte_eal_mp_remote_launch(__lcore_main, NULL, CALL_MASTER);
	if (retval < 0) {
		rte_exit(EXIT_FAILURE, "mp launch failed\n");
	}

	RTE_LCORE_FOREACH_SLAVE(coreid) {
		if (rte_eal_wait_lcore(coreid) < 0) {
			retval = -1;
			break;
		}
	}

	if (is_create_stat) {
		pthread_join(tid, NULL);
	}

	rte_eth_dev_stop(ring_portid);

	LOG_INFO("Done.");
	return 0;
}
