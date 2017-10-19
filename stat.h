#ifndef _PKTGEN_STAT_H_
#define _PKTGEN_STAT_H_

#include <stdint.h>

struct stat_info {
	uint64_t last_bytes;
	uint64_t last_pkts;
	uint64_t stat_bytes;
	uint64_t stat_pkts;
};

enum {
	PROBE_STATE_FREE = 0,
	PROBE_STATE_WAIT,
	PROBE_STATE_SEND,
	PROBE_STATE_RECV
};

struct probe_info {
	uint32_t idx;
	uint8_t state;
	uint64_t send_cyc;
	uint64_t recv_cyc;
};

/* in unit of second */
#define PROBE_TIMEOUT	2

#define STAT_PERIOD_USEC 200000
#define STAT_PERIOD_MULTI 5

void stat_thread_run(uint32_t *max_ptr);

void stat_update_rx(uint64_t bytes);

void stat_update_rx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle);

void stat_update_tx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle);

uint32_t stat_get_free_idx(void);

void stat_set_free(uint32_t idx);

#endif /* _PKTGEN_STAT_H_ */
