#ifndef _PKTGEN_CONTROL_H_
#define _PKTGEN_CONTROL_H_

bool ctl_is_stop(void);

void ctl_signal_handler(int signo);

bool ctl_is_stat_inited(void);

void ctl_stat_inited(void);

#endif /* _PKTGEN_CONTROL_H_ */
