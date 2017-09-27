#ifndef _PKTGEN_RXTX_H_
#define _PKTGEN_RXTX_H_

void rxtx_thread_run_rxtx(struct rte_ring *rx_ring,
				struct rte_ring *tx_ring);

void rxtx_thread_run_rx(struct rte_ring *rx_ring);

void rxtx_thread_run_tx(struct rte_ring *tx_ring);

#endif /* _PKTGEN_RXTX_H_ */
