#include "util.h"
#include "control.h"
#include "stat.h"

#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_ring.h>
#include <rte_malloc.h>

enum {
	STAT_RX_IDX = 0,
	STAT_TX_IDX,
	STAT_IDX_MAX
};

struct probe_record {
	uint32_t idx;
	uint64_t cycles;
};

static struct stat_info ring_stat[STAT_IDX_MAX];

static FILE *fout_rx = NULL;
static FILE *fout_tx = NULL;

static bool __stat_init(void)
{
	fout_rx = fopen("probe.rx", "w");
	if (fout_rx == NULL) {
		LOG_ERROR("Failed to open RX output file");
		return false;
	}

	fout_tx = fopen("probe.tx", "w");
	if (fout_tx == NULL) {
		LOG_ERROR("Failed to open TX output file");
		goto close_free_rx;
	}

	return true;

close_free_rx:
	fclose(fout_rx);
	fout_rx = NULL;

	return false;
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

void stat_update_rx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle)
{
	if (fout_rx != NULL)
		fprintf(fout_rx, "%u,%u,%lu\n", idx, RECORD_RX, cycle);

	LOG_DEBUG("RX probe packet %u at %lu", idx, (unsigned long)cycle);
	__update_stat(&ring_stat[STAT_RX_IDX], bytes);
}

void stat_update_tx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle)
{
	if (fout_tx != NULL)
		fprintf(fout_tx, "%u,%u,%lu\n", idx, RECORD_TX, cycle);

	__update_stat(&ring_stat[STAT_TX_IDX], bytes);
}

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

static bool __is_stat_stop(void)
{
	unsigned int tx_state, rx_state;

	tx_state = ctl_get_state(WORKER_TX);
	rx_state = ctl_get_state(WORKER_RX);

	if ((tx_state == STATE_UNINIT || tx_state == STATE_INITED) &&
					(rx_state == STATE_UNINIT || rx_state == STATE_INITED))
		return false;
	return true;
}

void stat_thread_run(void)
{
	uint64_t start_cyc = 0, end_cyc = 0;
	int count = 0;

	/* init */
	memset(ring_stat, 0, sizeof(struct stat_info) * STAT_IDX_MAX);

	if (__stat_init()) {
		ctl_set_state(WORKER_STAT, STATE_INITED);
		LOG_INFO("Statistics running on lcore %u",
						rte_lcore_id());
	} else {
		LOG_ERROR("Failed to initialize stat");
		ctl_set_state(WORKER_STAT, STATE_ERROR);
		return;
	}

	start_cyc = rte_get_tsc_cycles();

	while (!__is_stat_stop()) {
		// TODO
		usleep(STAT_PERIOD_USEC);

		if (count == STAT_PRINT_INTERVAL) {
			__process_stat(&ring_stat[STAT_RX_IDX], "RX");
			__process_stat(&ring_stat[STAT_TX_IDX], "TX");
			count = 0;
		} else
			count++;
	}

	end_cyc = rte_get_tsc_cycles();
	__summary_stat(end_cyc - start_cyc);

	if (fout_tx != NULL)
		fclose(fout_tx);

	if (fout_rx != NULL)
		fclose(fout_rx);

	ctl_set_state(WORKER_STAT, STATE_STOPPED);
}
