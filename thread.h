#ifndef _PKTGEN_THREAD_H_
#define _PKTGEN_THREAD_H_

struct stat_info {
	uint64_t last_bytes;
	uint64_t last_pkts;
	uint64_t stat_bytes;
	uint64_t stat_pkts;
};

#define STAT_PERIOD_USEC 500000
#define STAT_PERIOD_MULTI 2

void thread_stat_run(void);

void thread_rx_tx_run(struct rte_ring *rx_ring,
				struct rte_ring *tx_ring);

void thread_rx_run(struct rte_ring *rx_ring);

void thread_tx_run(struct rte_ring *tx_ring);

#endif /* _PKTGEN_THREAD_H_ */
