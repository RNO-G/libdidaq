#ifndef DIDAQ_RNO_G_H
#define DIDAQ_RNO_G_H

#include "librno-g.h"

/** RNO-G-like shims for libdidaq
 * Compiled conditionally
 * */


// like radiant_soft_trigger
#define didaq_soft_trigger didaq_force_trigger

int didaq_read_event(didaq_dev_t * bd, rno_g_header_t * hd, rno_g_waveform_t * wf);
int didaq_read_daqstatus(didaq_dev_t * bd, rno_g_daqstatus_t);
int didaq_poll_trigger_ready(didaq_dev_t * bd, int timeout_ms);



#endif
