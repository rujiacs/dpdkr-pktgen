#include <rte_mbuf.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_config.h>

#include <getopt.h>
#include <signal.h>

#include "util.h"
#include "stat.h"
#include "rxtx.h"
#include "control.h"

#define CLIENT_RXQ_NAME "dpdkr%u_rx"
#define CLIENT_TXQ_NAME "dpdkr%u_tx"

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

struct rte_ring *rx_ring;
struct rte_ring *tx_ring;

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
	LOG_INFO("Usage: %s [<EAL args> --proc-type=secondary] -- -n <client id>",
					progname);
}

static int __parse_options(int argc, char *argv[])
{
	int opt = 0;
	char **argvopt = argv;
	const char *progname = NULL;

	progname = argv[0];

	while ((opt = getopt(argc, argvopt, "n:")) != -1) {
		switch(opt) {
			case 'n':
				if (__parse_client_num(optarg) != 0) {
					LOG_ERROR("Wrong client id %s", optarg);
					__usage(progname);
					return -1;
				}
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
		rxtx_thread_run_rxtx(rx_ring, tx_ring);
	} else if (param->is_rx) {
		rxtx_thread_run_rx(rx_ring);
	} else if (param->is_tx) {
		rxtx_thread_run_tx(tx_ring);
	}

	LOG_INFO("lcore %u finished.", lcoreid);
	return 0;
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

	rx_ring = rte_ring_lookup(__get_rxq_name(client_id));
	if (rx_ring == NULL) {
		rte_exit(EXIT_FAILURE, "Cannot get RX ring for client %u\n",
						client_id);
	}

	tx_ring = rte_ring_lookup(__get_txq_name(client_id));
	if (tx_ring == NULL) {
		rte_exit(EXIT_FAILURE, "Cannot get TX ring for client %u\n",
						client_id);
	}

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

	LOG_INFO("Done.");
	return 0;
}
