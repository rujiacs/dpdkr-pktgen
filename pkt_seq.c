#include "util.h"
#include "pkt_seq.h"

#include <rte_malloc.h>

#define IP_VERSION 0x40
#define IP_HDRLEN 0x05
#define IP_VHL_DEF (IP_VERSION | IP_HDRLEN)
#define IP_TTL_DEF 64

void pkt_seq_init(struct pkt_seq_info *info)
{
	int i = 0;

	for (i = 0; i < 6; i++) {
		info->src_mac.addr_bytes[i] = 0;
		info->dst_mac.addr_bytes[i] = 0;
	}
	info->src_mac.addr_bytes[5] = PKT_SEQ_MAC_SRC;
//	info->dst_mac.addr_bytes[5] = PKT_SEQ_MAC_DST;

	info->src_ip = PKT_SEQ_IP_SRC;
	info->dst_ip = PKT_SEQ_IP_DST;

	info->src_port = PKT_SEQ_PORT_SRC;
	info->dst_port = PKT_SEQ_PORT_DST;

	info->pkt_len = PKT_SEQ_PKT_LEN;
	info->seq_cnt = PKT_SEQ_CNT;
}

static void __setup_udp_ip_hdr(struct udp_hdr *udp,
				struct ipv4_hdr *ip, struct pkt_seq_info *info)
{
	uint16_t *ptr16 = NULL;
	uint32_t ip_cksum = 0;

	/* Setup UDP header */
	udp->src_port = rte_cpu_to_be_16(info->src_port);
	udp->dst_port = rte_cpu_to_be_16(info->dst_port);
	udp->dgram_len = rte_cpu_to_be_16(info->pkt_len
										- sizeof(struct ether_hdr)
										- sizeof(struct ipv4_hdr));
	udp->dgram_cksum = 0;	/* No UDP checksum */

	/* Setup IPv4 header */
	ip->version_ihl = IP_VHL_DEF;
	ip->type_of_service = 0;
	ip->fragment_offset = 0;
	ip->time_to_live = IP_TTL_DEF;
	ip->next_proto_id = IPPROTO_UDP;
	ip->packet_id = 0;
	ip->total_length = rte_cpu_to_be_16(info->pkt_len
										- sizeof(struct ether_hdr));
	ip->src_addr = rte_cpu_to_be_32(info->src_ip);
	ip->dst_addr = rte_cpu_to_be_32(info->dst_ip);

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

struct pkt_probe *pkt_seq_create_template(struct pkt_seq_info *info)
{
	struct pkt_probe *pkt = NULL;

	pkt = rte_zmalloc("pktgen: struct pkt_probe",
						sizeof(struct pkt_probe), 0);
	if (pkt == NULL) {
		LOG_ERROR("Failed to allocate memory for pkt_probe");
		return NULL;
	}

	LOG_INFO("eth_hdr %lu, ip_hdr %lu, udp_hdr %lu, idx %lu, magic %lu, pkt_len %lu",
					sizeof(pkt->eth_hdr), sizeof(pkt->ip_hdr),
					sizeof(pkt->udp_hdr), sizeof(pkt->probe_idx),
					sizeof(pkt->probe_idx), sizeof(struct pkt_probe));

	/* Setup UDP and IPv4 headers */
	__setup_udp_ip_hdr(&pkt->udp_hdr, &pkt->ip_hdr, info);

	/* Setup Ethernet header */
	ether_addr_copy(&info->src_mac, &(pkt->eth_hdr.s_addr));
	ether_addr_copy(&info->dst_mac, &(pkt->eth_hdr.d_addr));
	pkt->eth_hdr.ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	/* Setup probe info */
	pkt->probe_idx = 0;
	pkt->probe_magic = PKT_PROBE_MAGIC;
	return pkt;
}

int pkt_seq_get_idx(struct rte_mbuf *pkt, uint32_t *idx)
{
	struct ipv4_hdr *ip_hdr = NULL;
	struct pkt_probe *probe = NULL;

	if (RTE_ETH_IS_IPV4_HDR(pkt->packet_type) == 0)
		return -1;

	ip_hdr = rte_pktmbuf_mtod_offset(pkt, struct ipv4_hdr *,
					sizeof(struct ether_hdr));
	if (ip_hdr->next_proto_id != IPPROTO_UDP)
		return -1;

	probe = rte_pktmbuf_mtod(pkt, struct pkt_probe *);
	if (probe->probe_magic != PKT_PROBE_MAGIC)
		return -1;

	*idx = probe->probe_idx;
	return 0;
}
