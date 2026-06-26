#include "didaq_rno-g.h"
#include "didaq.h"


int didaq_read_event(didaq_dev_t * bd, rno_g_header_t * hd, rno_g_waveform_t * wf)
{



}


int didaq_read_daqstatus(didaq_dev_t * bd, rno_g_daqstatus_t)
{

}
int didaq_poll_trigger_ready(didaq_dev_t * bd, int timeout_ms)
{
  return didaq_event_wait(bd, timeout_ms / 1000.);
}


