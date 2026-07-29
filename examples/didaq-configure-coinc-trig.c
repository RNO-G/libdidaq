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
  int channel_thresh = 30;


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
    /*
        else if (strcmp(args[i], "-t") == 0) {
        // Read following arguments until another flag or end of argv
        while (i + 1 < nargs && args[i + 1][0] != '-') {
            i++; // Move index to the number string
            
            // Convert string to integer securely using strtol
            channel_thresh[count] = strtoul(args[++i], 0, 0);
            count++;
            
            if (count > 3) break; // Prevent buffer overflow
        }
    }
    */

    else if(!strcmp(args[i],"-u") && i< nargs-1)
    {
      upper_or_lower = strtoul(args[++i], 0, 0);
    }
    else
    {
      fprintf(stderr,"Usage:  didaq-configure-coinc-trig [ -d DEVICE ] [ -w COINC_WINDOW ] [ -n NUM_REQUIRED ] [ -M MASK ] [-t channel_thresh ] [-e enables ]\n");

      return 0;
    }
  }

  for (int trig = 0; trig<2; trig++)
  {
    // upper 12 ch = 2, lower 12 ch = 1
    if (upper_or_lower & (1<<trig) == trig+1)
    {
      s.coinc[trig].enable |= (enables & (1<<trig) == trig+1);
      s.coinc[trig].enable_readout |= (enables & (1<<trig) == trig+1);
    }
  }

  didaq_setup_t setup = { 
    .spi_device = dev,
    .spi_en_gpio_label = "NSPIBUS_EN", 
  };

  didaq_dev_t * dev = didaq_open(&setup);
  didaq_configure_trigger(dev, &s);
  didaq_dump(dev,stdout,0);

  didaq_coin_thresholds_t th; 
  didaq_scalers_t scal; 

	for (int j = 0; j < DIDAQ_NUM_CHANNELS; j++) 
	{
		if (j < 4) th.coin_thresholds[j] = channel_thresh[j];
		else th.coin_thresholds[j] = 255;
	}

	didaq_set_thresholds(dev, 0, &th);
	printf("Using threshold of %d, %d, %d, %d\n", channel_thresh[0], channel_thresh[1], channel_thresh[2], channel_thresh[3]);

	sleep(10);
	didaq_read_scalers(dev, &scal);
	didaq_dump_scalers(&scal,stdout);
  
  return didaq_close(dev);

}
