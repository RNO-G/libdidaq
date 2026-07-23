#include "didaq.h"
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


// hard code for a 4 channel test using the lab setup with 4 signal chains and pulsers connected to
// channels 0-3, will add in configuration for upward and downward facing triggers

const char * dev = "/dev/spidev1.0";


didaq_trigger_setup_t s = {
  .coinc =
  {
     {
      .enable = true,
			.enable_readout = true,
      .num_required = 2,
      .coinc_window = 2,
			.quad_mode = 1,
    },
    {
      .enable = true,
			.enable_readout = true,
      .num_required = 2,
      .coinc_window = 2,
			quad_mode = 1
    }
  }
};

int channel_thresh[4] = {0,0,0,0};

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
      uint32_t E = ~M;
      s.coinc[0].channel_exclude_mask = E & (0xfff);
      s.coinc[1].channel_exclude_mask = (E >> 12) & (0xfff);
    }
    else if (!strcmp(args[i],"-e") && i< nargs-1)
    {
      uint32_t enables = strtoul(args[++i], 0, 0);
      s.coinc[0].enable = (enables & 0x1 == 1);
      s.coinc[1].enable = (enables & 0x2 == 2);
    }
    else if (strcmp(args[i], "-t") == 0) {
        // Read following arguments until another flag or end of argv
        while (i + 1 < argc && args[i + 1][0] != '-') {
            i++; // Move index to the number string
            
            // Convert string to integer securely using strtol
            channel_thresh[count] = strtoul(args[++i], 0, 0);
            count++;
            
            if (count > 3) break; // Prevent buffer overflow
        }
    }
    else
    {

      fprintf(stderr,"Usage:  didaq-configure-coinc-trig [ -d DEVICE ] [ -w COINC_WINDOW ] [ -n NUM_REQUIRED ] [ -M MASK ] [-t channel_thresh ] [-e enables ]\n");

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

  didaq_coin_thresholds_t th; 
  didaq_scalers_t scal; 

	for (int j = 0; j < DIDAQ_NUM_CHANNELS; j++) 
	{
		if (j < 4) th.coin_thresholds[j] = thresh[j];
		else th.coinc_thresholds[j] = 255;
	}

	didaq_set_thresholds(dev, 0, &th);
	printf("Using threshold of %d, %d, %d, %d\n", thresh[0], thresh[1], thresh[2], thresh[3]);

	sleep(10);
	didaq_read_scalers(dev, &scal);
	didaq_dump_scalers(&scal,stdout);
  
  return didaq_close(dev);

}
