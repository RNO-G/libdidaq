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
      .clks_over_thresh = 4,
      .channel_exclude_mask = 0xffffff
    },
    {
      .enable = false,
      .enable_readout = false,
      .num_required = 2,
      .coinc_window = 2,
      .clks_over_thresh = 4,
      .channel_exclude_mask = 0xffffff
    }
  }
};


int main (int nargs, char ** args) 
{
  int channel_thresh[24] = {1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,
                            1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000,1000};
  int enable = 0;
  for (int i = 1; i < nargs; i++)
  {
    if (!strcmp(args[i],"-d") && i < nargs-1)  
    {
      dev = args[++i];
    }
    else if (!strcmp(args[i],"-e") && i < nargs-1)
    {
      enable = atoi(args[++i]);
      if(enable & 1)
      {
        s.coinc[0].enable = true;
        s.coinc[0].enable_readout = true;
      }
      if(enable & 2)
      {
        s.coinc[1].enable = true;
        s.coinc[1].enable_readout = true;
      }
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
    else if (!strcmp(args[i], "-o") && i < nargs-1)
    {
      int n = atoi(args[++i]);
      s.coinc[0].clks_over_thresh = n;
      s.coinc[1].clks_over_thresh = n;
    }
    else if (!strcmp(args[i],"-M0") && i < nargs-1)
    {
      uint32_t M = strtoul(args[++i], 0, 0);
      uint32_t E = ~M;
      s.coinc[0].channel_exclude_mask = E & (0xffffff);
    }
    else if (!strcmp(args[i],"-M1") && i < nargs-1)
    {
      uint32_t M = strtoul(args[++i], 0, 0);
      uint32_t E = ~M;
      s.coinc[1].channel_exclude_mask = E & (0xffffff);
    }
    else if (!strcmp(args[i], "-t") && i < nargs-1)
    {
      int count = 0;
      while (i < nargs-1) 
      {
        channel_thresh[count] = strtoul(args[++i], 0, 0);
        count++;
      }
    }
    else if (!strcmp(args[i], "-T") && i < nargs-1)
    {
      uint32_t thresh = strtoul(args[++i], 0, 0);
      for(int j = 0; j<24; j++)
      {
        channel_thresh[j] = thresh;
      }
    }
    else
    {
      fprintf(stderr,"Usage:  didaq-configure-coinc-trig [ -d DEVICE ] [ -e enables ] [ -w COINC_WINDOW ] [ -n NUM_REQUIRED ] [-o CLKS_OVER_THRESH ] [ -M0 MASK0 ] [ -M1 MASK0 ] [ -T global_thresh ] [-t channel_thresholds (put last)]\n");
      return 0;
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

  for (int j = 0; j < 24; j++) 
  {
    th.coin_thresholds[j] = channel_thresh[j];
  }

  // set thresholds if any enabled
  if(enable) didaq_set_thresholds(dev, 0, &th);

  printf("Using Ch. thresholds of :\n");
  for(int i = 0; i<24; i++)
  {
    printf(" Ch. %02d : Th. %04d,", i, th.coin_thresholds[i]);
    if((i+1)%4==0) printf("\n");
  }

  sleep(25);
  didaq_read_scalers(dev, &scal);
  didaq_dump_scalers(&scal,stdout);
  
  return didaq_close(dev);

}
