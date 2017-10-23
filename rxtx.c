#include <rte_ring.h>
#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_hash_crc.h>

#include "util.h"
#include "control.h"
#include "rxtx.h"
#include "stat.h"
#include "pkt_seq.h"
#include "rate.h"

/* Rate control */
/* - default tx rate: 10kbps */
#define TX_RATE_DEF "10k"
struct rate_ctl tx_rate = {
	.rate_bps = 0,
	.cycle_per_byte = 0,
	.next_tx_cycle = 0,
}; 

/**** RX ****/
#define RX_NB_BURST 32
struct rte_mbuf *rx_buf[RX_NB_BURST] = {NULL};

static void __rx_stat(struct rte_mbuf *pkt, uint64_t recv_cyc)
{
	uint32_t probe_idx = 0;
//	int ret = 0;

	if (pkt_seq_get_idx(pkt, &probe_idx) < 0) {
		LOG_DEBUG("RX packet");
		stat_update_rx(pkt->data_len);
	} else {
		stat_update_rx_probe(probe_idx, pkt->data_len, recv_cyc);
		LOG_DEBUG("RX packet %u, len %u, recv_cyc %lu",
						probe_idx, pkt->data_len,
						(unsigned long)recv_cyc);
	}
}

static int __process_rx(int portid)
{
	uint16_t nb_rx, i = 0;
	uint64_t recv_cyc = 0;

	recv_cyc = rte_get_tsc_cycles();
	nb_rx = rte_eth_rx_burst(portid, 0, rx_buf, RX_NB_BURST);
	if (nb_rx == 0)
		return 0;

	for (i = 0; i < nb_rx; i++) {
		__rx_stat(rx_buf[i], recv_cyc);
		rte_pktmbuf_free(rx_buf[i]);
	}
	return 0;
}

void rxtx_set_rate(const char *rate_str)
{
	if (tx_rate.rate_bps > 0)
		return;

	rate_set_rate(rate_str, &tx_rate);
}

void rxtx_thread_run_rx(int portid)
{
	/* waiting for stat thread */
	while (ctl_get_state(WORKER_STAT) == STATE_UNINIT && !ctl_is_stop()) {}

	if (ctl_get_state(WORKER_STAT) == STATE_STOPPED
					|| ctl_get_state(WORKER_STAT) == STATE_ERROR)
		return;

	if (portid < 0) {
		LOG_ERROR("Invalid parameters, portid %d", portid);
		ctl_set_state(WORKER_RX, STATE_ERROR);
		return;
	}

	LOG_INFO("rx running on lcore %u", rte_lcore_id());

	ctl_set_state(WORKER_RX, STATE_INITED);

	while (!ctl_is_stop()) {
		if (__process_rx(portid) < 0) {
			LOG_ERROR("RX error!");
			break;
		}
	}

	ctl_set_state(WORKER_RX, STATE_STOPPED);
}

#define MAX_RETRY 3

void rxtx_thread_run_tx(int portid, struct rte_mempool *mp,
				struct pkt_seq_info *seq)
{
//	int ret = 0;
//	unsigned int tx_retry = 0;

	/* waiting for stat thread */
	while (ctl_get_state(WORKER_STAT) == STATE_UNINIT && !ctl_is_stop()) {}

	if (ctl_get_state(WORKER_STAT) == STATE_STOPPED
					|| ctl_get_state(WORKER_STAT) == STATE_ERROR)
		return;

	if (portid < 0 || mp == NULL || seq == NULL) {
		LOG_ERROR("Invalid parameters, portid %d, mp %p, seq %p",
						portid, mp, seq);
		ctl_set_state(WORKER_TX, STATE_ERROR);
		return;
	}

	rxtx_set_rate(TX_RATE_DEF);

	LOG_INFO("tx running on lcore %u", rte_lcore_id());

//	tx_seq_iter = 0;

	ctl_set_state(WORKER_TX, STATE_INITED);

	while (!ctl_is_stop()) {
		/* TX */
//		ret = __process_tx(portid, mp, seq, true, 0);
//		if (ret == -ERANGE || ret == -ENOMEM) {
//			LOG_ERROR("TX error!");
//			break;
//		}
//		else if (ret == -EAGAIN) {
//			if (tx_retry >= MAX_RETRY) {
//				LOG_ERROR("Reached the max retry times, TX exit.");
//				break;
//			} else {
//				tx_retry++;
//			}
//		} else {
//			tx_retry = 0;
//		}
	}
}

void rxtx_thread_run_rxtx(int sender, int recv, struct rte_mempool *mp,
				struct pkt_seq_info *seq)
{
//	int ret = 0;
	bool is_tx_err = true, is_rx_err = false;
//	unsigned int tx_retry = 0;
//	uint64_t cycle = 0;

	/* waiting for stat thread */
	while (ctl_get_state(WORKER_STAT) == STATE_UNINIT && !ctl_is_stop()) {}

	if (ctl_get_state(WORKER_STAT) == STATE_ERROR ||
					ctl_get_state(WORKER_STAT) == STATE_STOPPED)
		return;

	LOG_INFO("tx running on lcore %u", rte_lcore_id());

	if (sender < 0 || recv < 0 || mp == NULL || seq == NULL) {
		LOG_ERROR("Invalid parameters, sender %d, recv %d, mp %p, seq %p",
						sender, recv, mp, seq);
		ctl_set_state(WORKER_TX, STATE_ERROR);
		ctl_set_state(WORKER_RX, STATE_ERROR);
		return;
	}

	rxtx_set_rate(TX_RATE_DEF);
//	tx_seq_iter = 0;

	ctl_set_state(WORKER_TX, STATE_INITED);
	ctl_set_state(WORKER_RX, STATE_INITED);

	while (!ctl_is_stop()) {
		/* TX */
//		if (!is_tx_err) {
//			cycle = rte_get_tsc_cycles();
//			ret = __process_tx(sender, mp, seq, false, cycle);
//			if (ret == -ERANGE || ret == -ENOMEM) {
//				is_tx_err = true;
//				LOG_ERROR("TX error!");
//			} else if (ret == -EAGAIN) {
//				if (tx_retry >= MAX_RETRY) {
//					LOG_ERROR("Reached the max retry times, TX exit.");
//					is_tx_err = true;
//				} else {
//					tx_retry++;
//				}
//			} else {
//				tx_retry = 0;
//			}
//		}

		/* RX */
		if (!is_rx_err) {
			if (__process_rx(recv) < 0) {
				LOG_ERROR("RX error!");
				is_rx_err = true;
			}
		}

		/* Check state */
		if (is_tx_err && is_rx_err)
			break;
	}

	ctl_set_state(WORKER_TX, STATE_STOPPED);
	ctl_set_state(WORKER_RX, STATE_STOPPED);
}
