#include "didaq.h"
#include <stdlib.h>

#define LEN 768

int main (int nargs, char ** args)
{
  didaq_setup_t setup = { 
    .spi_device = nargs > 3 ? args[3] : "/dev/spidev1.0", 
    .spi_en_gpio_label = "NSPIBUS_EN", 
    .trig_ready_gpio_label = (nargs > 4) ? args[4]: 0,
    .spi_speed = nargs > 5 ? atoi(args[5]) : 0 ,
    .pipeline_reads = false
  };
  int N = nargs > 1 ? atoi(args[1]) : 10;
  bool force = nargs > 2 ? atoi(args[2]) : 0;

  didaq_dev_t * dev = didaq_open(&setup);

  didaq_reset_acq(dev);


  static uint8_t wfs[DIDAQ_NUM_CHANNELS][LEN];

  didaq_event_readout_t rdout = { .in  = {.len = LEN, .start = 384}, .wfs = 
    { 
      wfs[0], wfs[1], wfs[2], wfs[3], wfs[4], wfs[5],
      wfs[6], wfs[7], wfs[8], wfs[9], wfs[10], wfs[11],
      wfs[12], wfs[13], wfs[14], wfs[15], wfs[16], wfs[17],
      wfs[18], wfs[19], wfs[20], wfs[21], wfs[22], wfs[23]
    }
  };


  for (int i = 0; i < N; i++)
  {
    if (force) didaq_force_trigger(dev);
    didaq_event_readout(dev, &rdout);
    didaq_dump_event_readout(&rdout, stdout);
  }

  return didaq_close(dev);
}
