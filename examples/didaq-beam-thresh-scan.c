#include "didaq.h"
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


const char * dev = "/dev/spidev1.0";


didaq_trigger_setup_t s = {
  .phased =
  {
      .enable = true, .enable_readout =true,
  }
};

int start_thresh = 1000; 
int end_thresh = 30000;
int thresh_step = 100;
int wait_time = 20;
double servo_frac = 0.8;


int main (int nargs, char ** args) 
{

  for (int i = 1; i < nargs; i++)
  {
    if (!strcmp(args[i],"-d") && i < nargs-1)  
    {
      dev = args[++i];
    }
    else if (!strcmp(args[i],"-2"))
    {
      s.phased.divide_by_2 = true ;
    }
    else if (!strcmp(args[i],"-c"))
    {
      s.phased.require_consecutive_windows = true;
    }
 
    else if (!strcmp(args[i], "-B") && i < nargs-1)
    {
      uint32_t M = strtoul(args[++i], 0, 0);
      uint32_t E = ~M;
      s.phased.beam_exclude_mask = E;
    }
    else if (!strcmp(args[i],"-C") && i < nargs-1)
    {
      uint32_t M = strtoul(args[++i], 0, 0);
      uint32_t E = ~M;
      s.phased.chan_exclude_mask = E & (0xf);
    }
    else if (!strcmp(args[i],"-b") && i < nargs-1)
    {
      start_thresh = strtoul(args[++i], 0, 0);
    }
    else if (!strcmp(args[i],"-e") && i < nargs-1)
    {
      end_thresh = strtoul(args[++i], 0, 0);
    }
    else if (!strcmp(args[i],"-t") && i < nargs-1)
    {
      wait_time = strtoul(args[++i], 0, 0);
    }
    else if (!strcmp(args[i],"-s") && i < nargs-1)
    {
      thresh_step = strtoul(args[++i], 0, 0);
    }
    else if (!strcmp(args[i],"-f") && i < nargs-1)
    {
      servo_frac = atof(args[++i]);
    }
 
    else
    {

      fprintf(stderr,"Usage:  didaq-coin-thresh-scan [ -d DEVICE ] [ -2 -c] [ -B meam mask ] [ -C channel_mask ] [-b BEGIN_THRESH ] [-e END_THRESH ] [-s STEP_THRESH]  [-t WAIT_TIME ] \n");

      return 0;
    }
  }


  didaq_setup_t setup = { 
    .spi_device = dev,
    .spi_en_gpio_label = "NSPIBUS_EN", 
  };


  didaq_dev_t * dev = didaq_open(&setup);
  didaq_configure_trigger(dev, &s);
  didaq_dump(dev,stdout,0);

  didaq_phased_thresholds_t th; 
  didaq_scalers_t scal; 
  for (int thresh = start_thresh; thresh <= end_thresh; thresh+=thresh_step)
  {
    for (int j = 0; j < DIDAQ_NUM_BEAMS; j++)
    {
	    th.beam_trig_thresholds[j] = thresh;
	    th.beam_servo_thresholds[j] = thresh* servo_frac;;
    }
    didaq_set_thresholds(dev, &th, 0);
    printf("Using threshold of %d\n", thresh);
    sleep(1);
    didaq_read_scalers(dev, &scal);
    didaq_dump_scalers(&scal,stdout);
    sleep(10);
    didaq_read_scalers(dev, &scal);
    didaq_dump_scalers(&scal,stdout);
    sleep(10);
    didaq_read_scalers(dev, &scal);
    didaq_dump_scalers(&scal,stdout);
  }



  return didaq_close(dev);

}
