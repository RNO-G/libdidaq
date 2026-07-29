#include "didaq.h"
#include <stdlib.h>

int main (int nargs, char ** args)
{
  // idk which of these needs to be setup for simple waveform readout
  didaq_setup_t setup = { 
    .spi_device = nargs > 3 ? args[3] : "/dev/spidev1.0",
    .spi_en_gpio_label = "NSPIBUS_EN", 
    .trig_ready_gpio_label = (nargs > 4) ? args[4]: 0,
    .spi_speed = nargs > 5 ? atoi(args[5]) : 0 ,
    .pipeline_reads = false
  };
  
  int adc_mask = nargs > 1 ? atoi(args[1]) : 0x3f;
  float target_rms = nargs > 2 ? atof(args[2]) : 5;

  didaq_dev_t * dev = didaq_open(&setup);

  didaq_reset_acq(dev);

  float final_rms[DIDAQ_NUM_CHANNELS] = {0.};
  uint16_t gain_codes[DIDAQ_NUM_ADCS] = {0.};
  gain_code = didaq_auto_gain(dev, 0x3f, target_rms, final_rms);

  print("Final ADC gain codes and RMS values")
  for(int adc = 0; adc<DIDAQ_NUM_ADCS; adc++)
  {
    printf("ADC%i: Code %i; RMS CH%i %02f, CH%i %02f, CH%i %02f, CH%i %02f", adc, 
            gain_code[0], adc*4, final_rms[4*adc], adc*4+1, final_rms[4*adc+1], 
            adc*4+2, final_rms[4*adc+2] ,adc*4+3, final_rms[4*adc+3]);
  }

  return didaq_close(dev);
}
