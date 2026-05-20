#include "didaq.h"
#include <stdlib.h>

int main (int nargs, char ** args)
{
  didaq_setup_t setup = { 
    .spi_device = nargs > 1 ? args[1] : "/dev/spidev1.0", 
    .spi_en_gpio_label = "NSPIBUS_EN", 
    .spi_speed = nargs > 2 ? atoi(args[2]) : 0 
  };

  didaq_dev_t * dev = didaq_open(&setup);

  didaq_dump( dev, stdout);
  return didaq_close(dev);
}
