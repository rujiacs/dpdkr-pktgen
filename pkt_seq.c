#include "util.h"
#include "pkt_seq.h"

#include <rte_malloc.h>

#define IP_VERSION 0x40
#define IP_HDRLEN 0x05
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)
#define IP_TTL_DEF 64

static struct ether_addr mac_src = {
	.addr_bytes = {12},
};
static struct ether_addr mac_dst = {
	.addr_bytes = {21},
};

static void __parse_mac_addr(const char *str,
				struct ether_addr *addr)
{
	uint32_t mac[6] = {0};
	int ret = 0, i = 0;

	ret = sscanf(str, "%x:%x:%x:%x:%x:%x",
			&mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
	if (ret < 6) {
		LOG_ERROR("Failed to parse MAC from string %s", str);
	}

	for (i = 0; i < 6; i++) {
		addr->addr_bytes[i] = mac[i] & 0xff;
	}
}

void pkt_seq_set_mac_src(const char *str)
{
	__parse_mac_addr(str, &mac_src);
}

void pkt_seq_set_mac_dst(const char *str)
{
	__parse_mac_addr(str, &mac_dst);
}

void pkt_seq_init(struct pkt_seq_info *info)
{
	info->src_ip = PKT_SEQ_IP_SRC;
	info->dst_ip = PKT_SEQ_IP_DST;
	info->proto = PKT_SEQ_PROTO;

	info->src_port = PKT_SEQ_PORT_SRC;
	info->dst_port = PKT_SEQ_PORT_DST;

	info->pkt_len = PKT_SEQ_PKT_LEN;
//	info->seq_cnt = PKT_SEQ_CNT;
}

static void __setup_udp_ip_hdr(struct udp_hdr *udp,
				struct ipv4_hdr *ip)
{
	uint16_t *ptr16 = NULL;
	uint32_t ip_cksum = 0;

	/* Setup UDP header */
	udp->src_port = rte_cpu_to_be_16(PKT_SEQ_PROBE_PORT_SRC);
	udp->dst_port = rte_cpu_to_be_16(PKT_SEQ_PROBE_PORT_DST);
	udp->dgram_len = rte_cpu_to_be_16(PKT_SEQ_PROBE_PKT_LEN
										- sizeof(struct ether_hdr)
										- sizeof(struct ipv4_hdr));
	udp->dgram_cksum = 0;	/* No UDP checksum */

	/* Setup IPv4 header */
	ip->version_ihl = IP_VHL_DEF;
	ip->type_of_service = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = IP_TTL_DEF;
	ip->next_proto_id = PKT_SEQ_PROBE_PROTO;
	ip->packet_id = 0;
	ip->total_length = rte_cpu_to_be_16(PKT_SEQ_PROBE_PKT_LEN
										- sizeof(struct ether_hdr));
	ip->src_addr = rte_cpu_to_be_32(PKT_SEQ_IP_SRC);
	ip->dst_addr = rte_cpu_to_be_32(PKT_SEQ_IP_DST);

	/* Compute IPv4 header checksum */
	ptr16 = (unaligned_uint16_t*)ip;
	ip_cksum = 0;
	ip_cksum += ptr16[0]; ip_cksum += ptr16[1];
	ip_cksum += ptr16[2]; ip_cksum += ptr16[3];
	ip_cksum += ptr16[4];
	ip_cksum += ptr16[6]; ip_cksum += ptr16[7];
	ip_cksum += ptr16[8]; ip_cksum += ptr16[9];

	/* Reduce 32 bit checksum to 16 bits and complement it. */
	ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) +
		(ip_cksum & 0x0000FFFF);
	if (ip_cksum > 65535)
		ip_cksum -= 65535;
	ip_cksum = (~ip_cksum) & 0x0000FFFF;
	if (ip_cksum == 0)
		ip_cksum = 0xFFFF;
	ip->hdr_checksum = (uint16_t)ip_cksum;
}

struct pkt_probe *pkt_seq_create_probe(void)
{
	struct pkt_probe *pkt = NULL;

	pkt = rte_zmalloc("pktgen: struct pkt_probe",
						sizeof(struct pkt_probe), 0);
	if (pkt == NULL) {
		LOG_ERROR("Failed to allocate memory for pkt_probe");
		return NULL;
	}

	LOG_DEBUG("eth_hdr %lu, ip_hdr %lu, udp_hdr %lu, idx %lu, magic %lu, pkt_len %lu",
					sizeof(pkt->eth_hdr), sizeof(pkt->ip_hdr),
					sizeof(pkt->udp_hdr), sizeof(pkt->probe_idx),
					sizeof(pkt->probe_idx), sizeof(struct pkt_probe));

	/* Setup UDP and IPv4 headers */
	__setup_udp_ip_hdr(&pkt->udp_hdr, &pkt->ip_hdr);

	/* Setup Ethernet header */
	ether_addr_copy(&mac_src, &pkt->eth_hdr.s_addr);
	ether_addr_copy(&mac_dst, &pkt->eth_hdr.d_addr);
	pkt->eth_hdr.ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	/* Setup probe info */
	pkt->probe_idx = 0;
	pkt->probe_magic = PKT_PROBE_MAGIC;
	pkt->send_cycle = 0;
	return pkt;
}

int pkt_seq_get_idx(struct rte_mbuf *pkt, uint32_t *idx)
{
	struct ether_hdr *eth_hdr = NULL;
	struct ipv4_hdr *ip_hdr = NULL;
	struct pkt_probe *probe = NULL;

	eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);

//	LOG_INFO("PKT len %u", pkt->data_len);

	if (eth_hdr->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4)) {
//		LOG_INFO("Not IPv4");
		return -1;
	}

	ip_hdr = rte_pktmbuf_mtod_offset(pkt, struct ipv4_hdr *,
					sizeof(struct ether_hdr));
	if (ip_hdr->next_proto_id != IPPROTO_UDP) {
//		LOG_INFO("Not UDP");
		return -1;
	}

	probe = rte_pktmbuf_mtod(pkt, struct pkt_probe *);
	if (probe->probe_magic != PKT_PROBE_MAGIC) {
//		LOG_INFO("Wrong magic %u %u", PKT_PROBE_MAGIC, probe->probe_magic);
		return -1;
	}

	*idx = probe->probe_idx;
	return 0;
}
