#ifndef _PERF_PKT_SEQ_H_
#define _PERF_PKT_SEQ_H_

#include <stdio.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

struct pkt_seq_info {
	/* Ethernet info */
	struct ether_addr src_mac;
	struct ether_addr dst_mac;

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
#define PKT_SEQ_MAC_SRC 0x12
#define PKT_SEQ_MAC_DST 0x21
#define PKT_SEQ_IP_SRC IPv4(192,168,0,12)
#define PKT_SEQ_IP_DST IPv4(192,168,0,21)
#define PKT_SEQ_PKT_LEN 60

#define PKT_SEQ_PROBE_PROTO IPPROTO_UDP
#define PKT_SEQ_PROBE_PORT_SRC 1024
#define PKT_SEQ_PROBE_PORT_DST 1024
//#define PKT_SEQ_CNT 10

void pkt_seq_init(struct pkt_seq_info *info);

struct pkt_probe *pkt_seq_create_probe(
					struct pkt_seq_info *info);

int pkt_seq_get_idx(struct rte_mbuf *pkt, uint32_t *idx);

#define ETH_CRC_LEN 4

static inline uint16_t pkt_seq_wire_size(uint16_t pkt_len)
{
	return (pkt_len + ETH_CRC_LEN);
}

/* Return the number of bits in mask */
static __inline__ int
mask_size(uint32_t mask) {
	if (mask == 0)
		return 0;
	else if (mask == 0xFF000000)
		return 8;
	else if (mask == 0xFFFF0000)
		return 16;
	else if (mask == 0xFFFFFF00)
		return 24;
	else if (mask == 0xFFFFFFFF)
		return 32;
	else {
		int i;
		for (i = 0; i < 32; i++)
			if ( (mask & (1 << (31 - i))) == 0)
				break;
		return i;
	}
}

/* Convert IPv4 address to string */
static __inline__ char *
inet_ntop4(char *buff, int len, unsigned long ip_addr, unsigned long mask) {
	char lbuf[64];

	inet_ntop(AF_INET, &ip_addr, buff, len);
	if (mask != 0xFFFFFFFF) {
		snprintf(lbuf, sizeof(lbuf), "%s/%d", buff, mask_size(mask));
		strncpy(buff, lbuf, len);
	}
	return buff;
}

/* Convert MAC address to string */
static __inline__ char *
inet_mtoa(char *buff, int len, struct ether_addr *eaddr) {
	snprintf(buff, len, "%02x:%02x:%02x:%02x:%02x:%02x",
		 eaddr->addr_bytes[0], eaddr->addr_bytes[1],
		 eaddr->addr_bytes[2], eaddr->addr_bytes[3],
		 eaddr->addr_bytes[4], eaddr->addr_bytes[5]);
	return buff;
}

#endif /* _PERF_PKT_SEQ_H_ */
