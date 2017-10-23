#ifndef _PERF_PKT_SEQ_H_
#define _PERF_PKT_SEQ_H_

#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_mbuf.h>

struct pkt_seq_info {
	/* IPv4 info */
	uint32_t src_ip;
	uint32_t dst_ip;
	uint8_t proto;

	/* L3 info */
	uint16_t src_port;
	uint16_t dst_port;

	uint16_t pkt_len;
};

#define PKT_PROBE_MAGIC 0x12345678
#define PKT_PROBE_INITVAL 7

struct pkt_probe {
	struct ether_hdr eth_hdr;
	struct ipv4_hdr ip_hdr;
	struct udp_hdr udp_hdr;
	uint32_t probe_idx;
	uint32_t probe_magic;
	uint64_t send_cycle;
//	uint8_t pad[10];
};

#define IPv4(a, b, c, d)   ((uint32_t)(((a) & 0xff) << 24) |   \
			    (((b) & 0xff) << 16) |	\
			    (((c) & 0xff) << 8)  |	\
			    ((d) & 0xff))

/* Default packet sequence configuration */
#define PKT_SEQ_MAC_SRC "12:12:12:12:12:12"
#define PKT_SEQ_MAC_DST "21:21:21:21:21:21"
#define PKT_SEQ_IP_SRC IPv4(192,168,0,12)
#define PKT_SEQ_IP_DST IPv4(192,168,0,21)

#define PKT_SEQ_PKT_LEN 60
#define PKT_SEQ_PROTO IPPROTO_TCP
#define PKT_SEQ_PORT_SRC	9312
#define PKT_SEQ_PORT_DST	9321

#define PKT_SEQ_PROBE_PKT_LEN 60
#define PKT_SEQ_PROBE_PROTO IPPROTO_UDP
#define PKT_SEQ_PROBE_PORT_SRC 2024
#define PKT_SEQ_PROBE_PORT_DST 2024

void pkt_seq_set_mac_src(const char *str);

void pkt_seq_set_mac_dst(const char *str);

void pkt_seq_init(struct pkt_seq_info *info);

struct pkt_probe *pkt_seq_create_probe(void);

int pkt_seq_get_idx(struct rte_mbuf *pkt, uint32_t *idx);

#define ETH_CRC_LEN 4

static inline bool copy_buf_to_pkt(void *buf, unsigned len,
				struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset),
						buf, (size_t)len);
		return true;
	}
//	LOG_ERROR("Out of range, data_len %u, requested area [%u,%u]",
//					pkt->data_len, offset, offset + len);
	return false;
}

#endif /* _PERF_PKT_SEQ_H_ */
