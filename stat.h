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
	RECORD_RX = 0,
	RECORD_TX
};

#define STAT_PERIOD_USEC 200000
#define STAT_PRINT_USEC	1000000
#define STAT_PERIOD_MULTI (1000000 / STAT_PRINT_USEC)
#define STAT_PRINT_INTERVAL (STAT_PRINT_USEC / STAT_PERIOD_USEC - 1)

void stat_thread_run(void);

void stat_update_rx(uint64_t bytes);

void stat_update_rx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle);

void stat_update_tx_probe(uint32_t idx, uint64_t bytes, uint64_t cycle);

//uint32_t stat_get_free_idx(void);

//void stat_set_free(uint32_t idx);

#endif /* _PKTGEN_STAT_H_ */
