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

/* Rate control */
/* - default tx rate: 10kbps */
#define TX_RATE_DEF 10240
/* - minimum cycles of TX a packet */
static uint64_t cycle_per_pkt = 0;
/* - TX speed in unit of bps */
static uint64_t tx_rate = TX_RATE_DEF;
/* - Next time to TX a packet */
static uint64_t next_tx_cycles = 0;
/* - Cycles per second */
static uint64_t cycle_per_usec = 0;

/* Format: e.g 1000k => 1000 kbps, 2m => 2 mbps,
 * 			   128 => 128 bps
 */
void rxtx_set_rate(const char *rate_str)
{
	long val = 0;
	char *unit = NULL;

	val = strtol(rate_str, &unit, 10);
	if (errno == EINVAL || errno == ERANGE
					|| unit == rate_str) {
		LOG_ERROR("Failed to parse TX rate %s", rate_str);
		return;
	}

	if (val < 0) {
		LOG_ERROR("Wrong rate value %ld", val);
		return;
	}

	switch(*unit) {
		case 'k':	case 'K':
			tx_rate = val << 10;
			break;
		case 'm':	case 'M':
			tx_rate = val << 20;
			break;
		case 'g':	case 'G':
			tx_rate = val << 40;
			break;
		default:
			tx_rate = val;
	}
}

uint32_t rxtx_get_pkts_per_second(uint16_t pkt_len)
{
	return tx_rate / (pkt_seq_wire_size(pkt_len) * 8);
}

#define PPS_MIN 4

static void __set_cycles_per_pkt(uint16_t pkt_len)
{
	uint64_t pps = 0, hz = 0;

	hz = rte_get_tsc_hz();
	cycle_per_usec = hz / 1000000;
	pps = tx_rate / (pkt_seq_wire_size(pkt_len) * 8);
	cycle_per_pkt = (pps > 0) ? (hz / pps) : (hz / PPS_MIN);
	LOG_INFO("Cycles per packet %lu",
					(unsigned long)cycle_per_pkt);
}

static inline uint64_t __get_tx_next_cycles(uint64_t start)
{
	return start + cycle_per_pkt;
}

static inline void __wait_for_time(uint64_t next_cyc)
{
	unsigned long time = 0;
	uint64_t cur = 0;

	cur = rte_get_tsc_cycles();
	if (cur + cycle_per_usec >= next_cyc)
		return;

	time = (next_cyc - cur) / cycle_per_usec;
	usleep(time);
}

/*************************** TX ***************************/
static uint32_t tx_seq_iter = 0;
static struct pkt_probe *probe_pkt = NULL;
static unsigned int probe_pkt_len = 60;

static inline bool __copy_buf_to_pkt(void *buf, unsigned len,
				struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset),
						buf, (size_t)len);
		return true;
	}
	LOG_ERROR("Out of range, data_len %u, requested area [%u,%u]",
					pkt->data_len, offset, offset + len);
	return false;
}

static void __prepare_probe_mbuf(struct rte_mbuf **buf,
				struct rte_mempool *mp)
{
	struct rte_mbuf *pkt = NULL;
	uint32_t crc = 0;

	pkt = rte_mbuf_raw_alloc(mp);
	if (pkt == NULL) {
		LOG_ERROR("No available mbuf in mempool");
		*buf = NULL;
		return;
	}

	pkt->pkt_len = probe_pkt_len + ETH_CRC_LEN;
	pkt->data_len = probe_pkt_len + ETH_CRC_LEN;
	/* the number of packet segments */
	pkt->nb_segs = 1;

	/* construct probe packet */
	probe_pkt->probe_idx = tx_seq_iter;
	probe_pkt->send_cycle = rte_get_tsc_cycles();
	if (!__copy_buf_to_pkt(probe_pkt, sizeof(struct pkt_probe),
							pkt, 0)) {
		LOG_ERROR("Failed to copy probe packet into mbuf");
		goto close_free_mbuf;
	}

	/* calculate ethernet frame checksum */
	crc = rte_hash_crc(rte_pktmbuf_mtod(pkt, void *),
					probe_pkt_len, PKT_PROBE_INITVAL);

	if (!__copy_buf_to_pkt(&crc, sizeof(uint32_t),
							pkt, probe_pkt_len)) {
		LOG_ERROR("Failed to copy FCS into mbuf");
		goto close_free_mbuf;
	}

#ifdef PKTGEN_DEBUG
	crc = rte_hash_crc(rte_pktmbuf_mtod(pkt, void *),
					probe_pkt_len + 4, PKT_PROBE_INITVAL);
	LOG_INFO("Check FCS %x", crc);
#endif

	/* complete packet mbuf */
	pkt->ol_flags = 0;
	pkt->vlan_tci = 0;
	pkt->vlan_tci_outer = 0;
	pkt->l2_len = sizeof(struct ether_hdr);
	pkt->l3_len = sizeof(struct ipv4_hdr);
	*buf = pkt;
	return;

close_free_mbuf:
	rte_pktmbuf_free(pkt);
	*buf = NULL;
	return;
}

static int __process_tx(int portid, struct rte_mempool *mp,
				struct pkt_seq_info *seq, bool is_sleep)
{
	uint16_t nb_tx = 0;
	uint64_t start_cyc = 0;
	struct rte_mbuf *pkt = NULL;

	if (probe_pkt == NULL) {
		probe_pkt = pkt_seq_create_template(seq);
		if (probe_pkt == NULL) {
			LOG_ERROR("Failed to create template of probe pkt");
			return -ENOMEM;
		}
		probe_pkt_len = seq->pkt_len;
	}

	/* check if send now */
	start_cyc = rte_get_tsc_cycles();
	if (!is_sleep && start_cyc < next_tx_cycles)
		return 0;

	/* construct mbuf for probe packet */
	__prepare_probe_mbuf(&pkt, mp);

	if (pkt == NULL)
		return -EAGAIN;

	/* send probe packet */
	nb_tx = rte_eth_tx_burst(portid, 0, &pkt, 1);
	if (nb_tx < 1) {
		LOG_ERROR("Failed to send probe packet %u", probe_pkt->probe_idx);
		return -EAGAIN;
	}
//	LOG_INFO("TX packet %u on %lu", tx_seq_iter, start_cyc);

	/* update TX statistics */
	stat_update_tx_probe(probe_pkt->probe_idx,
					pkt->pkt_len, probe_pkt->send_cycle);
	LOG_DEBUG("TX packet %u on %lu", tx_seq_iter, probe_pkt->send_cycle);

	/* update tx seq state */
	tx_seq_iter++;

	/* calculate the next time to TX (and sleep) */
	next_tx_cycles = __get_tx_next_cycles(start_cyc);
	if (is_sleep) {
		__wait_for_time(next_tx_cycles);
	}

	return 0;
}

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
		LOG_DEBUG("RX packet %u", probe_idx);
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
	int ret = 0;
	unsigned int tx_retry = 0;

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
	LOG_INFO("tx running on lcore %u", rte_lcore_id());

	__set_cycles_per_pkt(seq->pkt_len);
	tx_seq_iter = 0;

	ctl_set_state(WORKER_TX, STATE_INITED);

	while (!ctl_is_stop()) {
		/* TX */
		ret = __process_tx(portid, mp, seq, true);
		if (ret == -ERANGE || ret == -ENOMEM) {
			LOG_ERROR("TX error!");
			break;
		}
		else if (ret == -EAGAIN) {
			if (tx_retry >= MAX_RETRY) {
				LOG_ERROR("Reached the max retry times, TX exit.");
				break;
			} else {
				tx_retry++;
			}
		} else {
			tx_retry = 0;
		}
	}
}

void rxtx_thread_run_rxtx(int sender, int recv, struct rte_mempool *mp,
				struct pkt_seq_info *seq)
{
	int ret = 0;
	bool is_tx_err = false, is_rx_err = false;
	unsigned int tx_retry = 0;

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

	__set_cycles_per_pkt(seq->pkt_len);
	tx_seq_iter = 0;

	ctl_set_state(WORKER_TX, STATE_INITED);
	ctl_set_state(WORKER_RX, STATE_INITED);

	while (!ctl_is_stop()) {
		/* TX */
		if (!is_tx_err) {
			ret = __process_tx(sender, mp, seq, false);
			if (ret == -ERANGE || ret == -ENOMEM) {
				is_tx_err = true;
				LOG_ERROR("TX error!");
			} else if (ret == -EAGAIN) {
				if (tx_retry >= MAX_RETRY) {
					LOG_ERROR("Reached the max retry times, TX exit.");
					is_tx_err = true;
				} else {
					tx_retry++;
				}
			} else {
				tx_retry = 0;
			}
		}

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
