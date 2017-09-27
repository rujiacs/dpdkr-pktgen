#include <rte_ring.h>
#include <rte_eal.h>

#include "util.h"
#include "control.h"
#include "rxtx.h"
#include "stat.h"

static void __process_rx(struct rte_ring *rx_ring)
{
	if (rx_ring == NULL)
		return;

	stat_update_rx(8);
}

static void __process_tx(struct rte_ring *tx_ring)
{
	if (tx_ring == NULL)
		return;

	stat_update_tx(8);
}

void rxtx_thread_run_rx(struct rte_ring *rx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("rx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_rx(rx_ring);
		usleep(100000);
	}
}

void rxtx_thread_run_tx(struct rte_ring *tx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("tx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_tx(tx_ring);
		usleep(200000);
	}
}

void rxtx_thread_run_rxtx(struct rte_ring *rx_ring,
				struct rte_ring *tx_ring)
{
	/* waiting for stat thread */
	while (!ctl_is_stat_inited() && !ctl_is_stop()) {}

	LOG_INFO("rx and tx running on lcore %u", rte_lcore_id());

	while (!ctl_is_stop()) {
		__process_rx(rx_ring);
		__process_rx(rx_ring);
		__process_tx(tx_ring);
		usleep(200000);
	}
}
