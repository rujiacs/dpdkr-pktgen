#include <rte_ring.h>
#include <rte_eal.h>

#include "util.h"
#include "control.h"
#include "thread.h"

#include <signal.h>

enum {
	STAT_RX_IDX = 0,
	STAT_TX_IDX,
	STAT_IDX_MAX
};

static struct stat_info ring_stat[STAT_IDX_MAX];

static void __process_stat(struct stat_info *stat,
				const char *prefix)
{
	uint64_t bytes = 0, pkts = 0;
	uint64_t last_b = 0, last_p = 0;

	bytes = stat->stat_bytes;
	pkts = stat->stat_pkts;
	last_b = stat->last_bytes;
	last_p = stat->last_pkts;
	stat->last_bytes = bytes;
	stat->last_pkts = pkts;

	LOG_INFO("%s %lu bps, %lu pps", prefix,
				(unsigned long)((bytes - last_b) * STAT_PERIOD_MULTI * 8),
				(unsigned long)((pkts - last_p) * STAT_PERIOD_MULTI));
}

void thread_stat_run(void)
{
	/* init */
	signal(SIGINT, ctl_signal_handler);
	signal(SIGTERM, ctl_signal_handler);
	memset(ring_stat, 0, sizeof(struct stat_info) * STAT_IDX_MAX);

	LOG_INFO("statistics running on lcore %u",
					rte_lcore_id());
	ctl_stat_inited();

	while (!ctl_is_stop()) {
		// TODO
		usleep(STAT_PERIOD_USEC);
		// rx
		__process_stat(&ring_stat[STAT_RX_IDX], "RX");
		__process_stat(&ring_stat[STAT_TX_IDX], "TX");
	}
}

static void __process_rx(struct rte_ring *rx_ring)
{
	struct stat_info *stat;

	if (rx_ring == NULL)
		return;

	stat = &ring_stat[STAT_RX_IDX];
	stat->stat_bytes += 8;
	stat->stat_pkts += 1;	
}

static void __process_tx(struct rte_ring *tx_ring)
{
	struct stat_info *stat;

	if (tx_ring == NULL)
		return;

	stat = &ring_stat[STAT_TX_IDX];
	stat->stat_bytes += 8;
	stat->stat_pkts += 1;	
}

void thread_rx_run(struct rte_ring *rx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("rx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_rx(rx_ring);
		usleep(100000);
	}
}

void thread_tx_run(struct rte_ring *tx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("tx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_tx(tx_ring);
		usleep(200000);
	}
}

void thread_rx_tx_run(struct rte_ring *rx_ring,
				struct rte_ring *tx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("rx and tx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_rx(rx_ring);
		__process_rx(rx_ring);
		__process_tx(tx_ring);
		usleep(200000);
	}
}
