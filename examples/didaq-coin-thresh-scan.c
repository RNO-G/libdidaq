#include "didaq.h"
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


const char * dev = "/dev/spidev1.0";


didaq_trigger_setup_t s = {
  .coinc =
  {
     {
      .enable = true,
      .num_required = 2,
      .coinc_window = 2
    },
    {
      .enable = true,
      .num_required = 2,
      .coinc_window = 2
    }
  }
};

int start_thresh = 1; 
int step = 1;
int end_thresh = 255;
int thresh_step = 1;
int wait_time = 20;


int main (int nargs, char ** args) 
{

  for (int i = 1; i < nargs; i++)
  {
    if (!strcmp(args[i],"-d") && i < nargs-1)  
    {
      dev = args[++i];
    }
    else if (!strcmp(args[i],"-w") && i < nargs-1)
    {
      int w = atoi(args[++i]);
      s.coinc[0].coinc_window = w;
      s.coinc[1].coinc_window = w;
    }
    else if (!strcmp(args[i], "-n") && i < nargs-1)
    {
      int n = atoi(args[++i]);
      s.coinc[0].num_required = n;
      s.coinc[1].num_required = n;
    }
    else if (!strcmp(args[i],"-M") && i < nargs-1)
    {
      uint32_t M = strtoul(args[++i], 0, 0);
      s.coinc[0].channel_exclude_mask = M & (0xfff);
      s.coinc[1].channel_exclude_mask = (M >> 12) & (0xfff);
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
      step = strtoul(args[++i], 0, 0);
    }
    else
    {

      fprintf(stderr,"Usage:  didaq-coin-thresh-scan [ -d DEVICE ] [ -w COINC_WINDOW ] [ -n NUM_REQUIRED ] [ -M MASK ] [-b BEGIN_THRESH ] [-e END_THRESH ] [-s STEP_THRESH]  [-t WAIT_TIME ] \n");

      return 0;
    }
  }


  didaq_setup_t setup = { 
    .spi_device = dev,
    .spi_en_gpio_label = "NSPIBUS_EN", 
  };


  didaq_dev_t * dev = didaq_open(&setup);
  didaq_configure_trigger(dev, &s);

  didaq_coin_thresholds_t th; 
  didaq_scalers_t scal; 
  for (int thresh = start_thresh; thresh <= end_thresh; thresh+=thresh_step)
  {
    for (int j = 0; j < DIDAQ_NUM_CHANNELS; j++) th.coin_thresholds[j] = thresh;
    didaq_set_thresholds(dev, 0, &th);
    printf("Using threshold of %d\n", thresh);
    sleep(20);

    didaq_read_scalers(dev, &scal);
    didaq_dump_scalers(&scal,stdout);
  }



  return didaq_close(dev);

}
