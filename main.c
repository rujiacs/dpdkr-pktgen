#include <rte_mbuf.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_config.h>

#include "util.h"
#include <getopt.h>

#define CLIENT_RXQ_NAME "dpdkr%u_rx"
#define CLIENT_TXQ_NAME "dpdkr%u_tx"

static unsigned int client_id = -1;

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
					RTE_LOG(ERR, RING, "Wrong client id %s\n", optarg);
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

int main(int argc, char *argv[])
{
	struct rte_ring *rx_ring = NULL;
	struct rte_ring *tx_ring = NULL;
	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0) {
		RTE_LOG(ERR, EAL, "Failed to initialize dpdk eal");
		return -1;
	}

	argc -= retval;
	argv += retval;

	if (__parse_options(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");
	}

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

	RTE_LOG(INFO, RING, "Processing client %u\n", client_id);
	return 0;
}
