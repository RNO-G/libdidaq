#include "didaq.h"
#include <stdlib.h>

int main (int nargs, char ** args)
{

  didaq_dev_t * dev = didaq_open(nargs > 1 ? args[1] : "/dev/spidev1.0", "NSPI_EN", 0, nargs > 2 ? atoi(args[2]) : 25000000);

  didaq_dump( dev, stdout);
  return didaq_close(dev);
}
