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
      .enable = false,
      .enable_readout = false,
      .num_required = 2,
      .coinc_window = 2,
      .quad_mode = 0,
    },
    {
      .enable = false,
      .enable_readout = false,
      .num_required = 2,
      .coinc_window = 2,
      .quad_mode = 0
    }
  }
};


int main (int nargs, char ** args) 
{
  uint32_t quad_mode = 0;
  uint8_t upper_or_lower = 0;
  int channel_thresh[12] = {30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};

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
      uint32_t E = ~M;
      s.coinc[0].channel_exclude_mask = E & (0xfff);
      s.coinc[1].channel_exclude_mask = (E >> 12) & (0xfff);
    }
    else if (!strcmp(args[i],"-q") && i< nargs-1)
    {
      quad_mode = strtoul(args[++i], 0, 0);
      s.coinc[0].quad_mode = quad_mode;
      s.coinc[1].quad_mode = quad_mode;

    }
    else if (strcmp(args[i], "-t") && i < nargs-1)
    {
      int count = 0;
      while (i < nargs) 
      {
        if(count > 12) break;
        channel_thresh[count] = strtoul(args[++i], 0, 0);
        count++;
      }
    }
    else if(!strcmp(args[i],"-u") && i< nargs-1)
    {
      upper_or_lower = strtoul(args[++i], 0, 0);
    }
    else
    {
      fprintf(stderr,"Usage:  didaq-configure-coinc-trig [ -d DEVICE ] [ -w COINC_WINDOW ] [ -n NUM_REQUIRED ] [ -M MASK ] [-u upper_or_lower ] [-q quad_mode ] [-t channel_thresholds (put last)]\n");
      return 0;
    }
  }

  for (int trig = 0; trig<2; trig++)
  {
    // upper 12 ch = 2, lower 12 ch = 1
    if(upper_or_lower & (1<<trig))
    {
      s.coinc[trig].enable = true;
      s.coinc[trig].enable_readout = true;
    }
  }

  didaq_setup_t setup = { 
    .spi_device = dev,
    .spi_en_gpio_label = "NSPIBUS_EN"
  };

  didaq_dev_t * dev = didaq_open(&setup);
  didaq_configure_trigger(dev, &s);
  didaq_dump(dev, stdout, 0);

  didaq_coin_thresholds_t th; 
  didaq_scalers_t scal; 

  for (int j = 0; j < 12; j++) 
  {
    if(upper_or_lower & 1) th.coin_thresholds[j] = channel_thresh[j];
    if(upper_or_lower & 2) th.coin_thresholds[j+12] = channel_thresh[j];
  }

  // set thresholds if any enabled
  if(upper_or_lower) didaq_set_thresholds(dev, 0, &th);

  if(upper_or_lower & 1)
  {
    printf("Using lower 12 ch thresholds of %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
          th.coin_thresholds[0], th.coin_thresholds[1], th.coin_thresholds[2], th.coin_thresholds[3],
          th.coin_thresholds[4], th.coin_thresholds[5], th.coin_thresholds[6], th.coin_thresholds[7],
          th.coin_thresholds[8], th.coin_thresholds[9], th.coin_thresholds[10], th.coin_thresholds[11]);
  }
  if(upper_or_lower & 2)
  {
    printf("Using upper 12 ch thresholds of %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
          th.coin_thresholds[12], th.coin_thresholds[13], th.coin_thresholds[14], th.coin_thresholds[15],
          th.coin_thresholds[16], th.coin_thresholds[17], th.coin_thresholds[18], th.coin_thresholds[19],
          th.coin_thresholds[20], th.coin_thresholds[21], th.coin_thresholds[22], th.coin_thresholds[23]);
  }
  sleep(25);
  didaq_read_scalers(dev, &scal);
  didaq_dump_scalers(&scal,stdout);
  
  return didaq_close(dev);

}
