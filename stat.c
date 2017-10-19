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

static struct stat_info ring_stat[STAT_IDX_MAX];

static uint64_t probe_timeout = 0;

static uint32_t probe_max_idx = 0;
static struct probe_info *probe_stat = NULL;
static struct rte_ring *free_idx = NULL;

static FILE *fout = NULL;

static bool __stat_init(unsigned max_idx)
{
	unsigned i = 0;

	probe_max_idx = roundup_2(max_idx);
	probe_stat = rte_zmalloc("struct probe_info *",
					sizeof(struct probe_info) * probe_max_idx, 0);
	if (probe_stat == NULL) {
		LOG_ERROR("Failed to allocate memory for probe_stat[%u]",
						(max_idx + 1));
		return false;
	}


	free_idx = rte_ring_create("PROBE_STAT_RING", probe_max_idx, 0, 0);
	if (free_idx == NULL) {
		LOG_ERROR("Failed to create ring to store free idx");
		goto close_free_stat;
	}

	fout = fopen("probe.data", "w");
	if (fout == NULL) {
		LOG_ERROR("Failed to open output file");
		goto close_free_idx;
	}

	/* add all probe_stat elements into free_idx */
	for (i = 0; i <= max_idx; i++) {
		probe_stat[i].idx = i;
		probe_stat[i].state = PROBE_STATE_FREE;
		probe_stat[i].send_cyc = 0;
		probe_stat[i].recv_cyc = 0;

		if (rte_ring_enqueue(free_idx, &probe_stat[i]) != 0)
			LOG_ERROR("Failed to enqueu idx %u to free_idx", i);
	}

	probe_timeout = rte_get_tsc_hz() * PROBE_TIMEOUT;
	
	return true;

close_free_idx:
	rte_ring_free(free_idx);
	free_idx = NULL;

close_free_stat:
	rte_free(probe_stat);
	probe_stat = NULL;

	return false;
}

uint32_t stat_get_free_idx(void)
{
	struct probe_info *free_info = NULL;

	if (rte_ring_dequeue(free_idx, (void **)&free_info) != 0) {
		LOG_ERROR("No available free idx");
		return UINT_MAX;
	}

	free_info->state = PROBE_STATE_WAIT;
	return free_info->idx;
}

void stat_set_free(uint32_t idx)
{
	struct probe_info *info = NULL;

	if (idx > probe_max_idx) {
		LOG_ERROR("Wrong probe_idx %u", idx);
		return;
	}

	info = &probe_stat[idx];
	info->send_cyc = info->recv_cyc = 0;
	info->state = PROBE_STATE_FREE;
	rte_ring_enqueue(free_idx, info);
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
	struct probe_info *info = NULL;

	if (idx > probe_max_idx) {
		LOG_ERROR("Wrong probe_idx %u", idx);
		return;
	}

	info = &probe_stat[idx];

	if (info->state != PROBE_STATE_SEND) {
		LOG_ERROR("Wrong state %u", (unsigned)info->state);

		if (info->state != PROBE_STATE_FREE)
			stat_set_free(idx);
		return;
	}

	info->recv_cyc = cycle;
	info->state = PROBE_STATE_RECV;
//	LOG_INFO("RX probe packet %u at %lu", idx, (unsigned long)cycle);
	__update_stat(&ring_stat[STAT_RX_IDX], bytes);
}

void stat_update_tx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle)
{
	struct probe_info *info = NULL;

	if (idx > probe_max_idx) {
		LOG_ERROR("Wrong probe_idx %u", idx);
		return;
	}

	info = &probe_stat[idx];

	if (info->state != PROBE_STATE_WAIT) {
		LOG_ERROR("Wrong state %u", (unsigned)info->state);

		if (info->state != PROBE_STATE_FREE)
			stat_set_free(idx);
		return;
	}

	info->send_cyc = cycle;
	info->state = PROBE_STATE_SEND;
	__update_stat(&ring_stat[STAT_TX_IDX], bytes);
}

static void __check_probe_timeout(uint64_t cur_cycle)
{
	uint32_t i = 0;
	struct probe_info *info = NULL;
	uint64_t timeout = PROBE_TIMEOUT * rte_get_tsc_hz();

//	LOG_INFO("Timeout %lu", (unsigned long)timeout);

	for (i = 0; i < probe_max_idx; i++) {
		info = &probe_stat[i];

		if (info->state == PROBE_STATE_SEND) {
			if (info->send_cyc + timeout >= cur_cycle) {
				/* recv_cycle(timeout) idx send_cycle latency(0)*/
				fprintf(fout, "%lu,%u,%lu,0\n",
								(unsigned long)cur_cycle, i,
								(unsigned long)info->send_cyc);
				stat_set_free(i);
			}
		} else if (info->state == PROBE_STATE_RECV) {
			fprintf(fout, "%lu,%u,%lu,%lu\n",
							(unsigned long)info->recv_cyc, i,
							(unsigned long)info->send_cyc,
							(unsigned long)(info->recv_cyc - info->send_cyc));
			stat_set_free(i);
		}
	}
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

void stat_thread_run(uint32_t *max_ptr)
{
	uint64_t start_cyc = 0, end_cyc = 0;
	uint32_t max_idx = *max_ptr;

	/* init */
	memset(ring_stat, 0, sizeof(struct stat_info) * STAT_IDX_MAX);

	if (__stat_init(max_idx)) {
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
		__process_stat(&ring_stat[STAT_RX_IDX], "RX");
		__process_stat(&ring_stat[STAT_TX_IDX], "TX");
		__check_probe_timeout(rte_get_tsc_cycles());
	}

	end_cyc = rte_get_tsc_cycles();
	__summary_stat(end_cyc - start_cyc);

	if (probe_stat != NULL)
		rte_free(probe_stat);

	if (free_idx != NULL)
		rte_ring_free(free_idx);

	if (fout != NULL)
		fclose(fout);

	ctl_set_state(WORKER_STAT, STATE_STOPPED);
}
