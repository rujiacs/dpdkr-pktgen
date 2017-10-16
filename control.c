#include "util.h"
#include "control.h"

#include <signal.h>

static bool force_quit = false;
static bool stat_inited = false;
static bool rx_stopped = false;
static bool tx_stopped = false;
static bool rxtx_inited = false;

bool ctl_is_stop(void)
{
	return force_quit;
}

bool ctl_is_stat_inited(void)
{
	return stat_inited;
}

void ctl_stat_inited(void)
{
	stat_inited = true;
}

void ctl_signal_handler(int signo)
{
	if (signo == SIGINT || signo == SIGTERM) {
		LOG_INFO("Signal %d reveiced. Preparing to exit...",
						signo);
		force_quit = true;
	}
}

bool ctl_is_stat_stop(void)
{
	return (rxtx_inited && rx_stopped && tx_stopped);
}

void ctl_rxtx_inited(void)
{
	rxtx_inited = true;
}

void ctl_rxtx_stopped(int type)
{
	switch(type) {
		case STOP_TYPE_TX:
			tx_stopped = true;
			break;
		case STOP_TYPE_RX:
			rx_stopped = true;
			break;
		case STOP_TYPE_RXTX:
			tx_stopped = true;
			rx_stopped = true;
			break;
		default:
			break;
	}
}
