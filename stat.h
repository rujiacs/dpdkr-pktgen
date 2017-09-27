#ifndef _PKTGEN_STAT_H_
#define _PKTGEN_STAT_H_

struct stat_info {
	uint64_t last_bytes;
	uint64_t last_pkts;
	uint64_t stat_bytes;
	uint64_t stat_pkts;
};

#define STAT_PERIOD_USEC 500000
#define STAT_PERIOD_MULTI 2

void stat_thread_run(void);

void stat_update_rx(uint64_t bytes);

void stat_update_tx(uint64_t bytes);

#endif /* _PKTGEN_STAT_H_ */
