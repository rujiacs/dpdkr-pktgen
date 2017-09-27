#include "util.h"
#include "control.h"

#include <signal.h>

static bool force_quit = false;
static bool stat_inited = false;

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
