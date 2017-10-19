#ifndef _PKTGEN_RXTX_H_
#define _PKTGEN_RXTX_H_

struct rte_mempool;
struct pkt_seq_info;

void rxtx_thread_run_rxtx(int sender, int recv, struct rte_mempool *mp,
				struct pkt_seq_info *seq);

void rxtx_thread_run_rx(int portid);

void rxtx_thread_run_tx(int portid, struct rte_mempool *mp,
				struct pkt_seq_info *seq);

void rxtx_set_rate(const char *rate_str);

uint32_t rxtx_get_pkts_per_second(uint16_t pkt_len);

#endif /* _PKTGEN_RXTX_H_ */
