#ifndef _PKTGEN_CONTROL_H_
#define _PKTGEN_CONTROL_H_

enum {
	STOP_TYPE_TX = 0,
	STOP_TYPE_RX,
	STOP_TYPE_RXTX
};

bool ctl_is_stop(void);

void ctl_signal_handler(int signo);

bool ctl_is_stat_stop(void);

bool ctl_is_stat_inited(void);

void ctl_stat_inited(void);

void ctl_rxtx_inited(void);

void ctl_rxtx_stopped(int type);

#endif /* _PKTGEN_CONTROL_H_ */
