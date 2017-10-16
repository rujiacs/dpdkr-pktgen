#include "util.h"
#include "control.h"
#include "stat.h"

#include <rte_lcore.h>
#include <rte_cycles.h>

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

static void __summary_stat(uint64_t cycles)
{
	double sec = 0;
	uint64_t rx_bytes, rx_pkts, tx_bytes, tx_pkts;

	sec = (double)cycles / rte_get_tsc_hz();
	rx_bytes = ring_stat[STAT_RX_IDX].stat_bytes;
	rx_pkts = ring_stat[STAT_RX_IDX].stat_pkts;
	tx_bytes = ring_stat[STAT_TX_IDX].stat_bytes;
	tx_pkts = ring_stat[STAT_TX_IDX].stat_pkts;

	LOG_INFO("Running %lf seconds.", sec);
	LOG_INFO("\tRX %lu bytes (%lf bps), %lu packets (%lf pps)",
					rx_bytes, (rx_bytes * 8 / sec),
					rx_pkts, (rx_pkts / sec));
	LOG_INFO("\tTX %lu bytes (%lf bps), %lu packets (%lf pps)",
					tx_bytes, (tx_bytes * 8 / sec),
					tx_pkts, (tx_pkts / sec));
}

void stat_thread_run(void)
{
	uint64_t start_cyc = 0, end_cyc = 0;

	/* init */
	memset(ring_stat, 0, sizeof(struct stat_info) * STAT_IDX_MAX);

	LOG_INFO("statistics running on lcore %u",
					rte_lcore_id());
	ctl_stat_inited();

	start_cyc = rte_get_tsc_cycles();

	while (!ctl_is_stat_stop()) {
		// TODO
		usleep(STAT_PERIOD_USEC);
		__process_stat(&ring_stat[STAT_RX_IDX], "RX");
		__process_stat(&ring_stat[STAT_TX_IDX], "TX");
	}

	end_cyc = rte_get_tsc_cycles();
	__summary_stat(end_cyc - start_cyc);
}

static void __update_stat(struct stat_info *stat, uint64_t byte)
{
	stat->stat_bytes += byte;
	stat->stat_pkts ++;
}

void stat_update_rx(uint64_t byte)
{
	__update_stat(&ring_stat[STAT_RX_IDX], byte);
}

void stat_update_tx(uint64_t byte)
{
	__update_stat(&ring_stat[STAT_TX_IDX], byte);
}
