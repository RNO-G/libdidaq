#include "didaq.h"
#include "didaq_adc.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>


const char * device = "/dev/spidev1.0";
#define MAX_OPS 1024

struct
{
  bool write;
  uint16_t addr;
  uint8_t val;
  int adc;
} ops [MAX_OPS];


void usage()
{
  printf("Usage: didaq-adc-reg [-d DEVICE]  { [@IADC],[w0xADDR 0xVAL],[r0xADDR] } ... \n  e.g. @0 sets ADC to 0. w0x100 0x50 writes 0x50 to register 0x100 on current ADC)\n");
}

int main (int nargs, char **args)
{


  int nops = 0;
  int adc = 0;
  for (int iarg = 1; iarg < nargs; iarg++)
  {
    //check for an argument
    if (!strcmp(args[iarg],"-d") && iarg < nargs-1 )
    {
      device = args[++iarg];
    }
    else if (args[iarg][0] == '@')
    {
      // set ADC
      int parsed = sscanf(args[iarg],"@%d", &adc);
      if (!parsed)
      {
        usage();
        return 1;
      }
    }
    else
    {
      char op = '\0';
      uint16_t addr = 0;
      char extra = '\0';
      int parsed = sscanf(args[iarg], "%c0x%hx%c", &op, &addr, &extra);

      if (parsed != 2)
      {
        usage();
        return 1;
      }

      if (op == 'w') 
      {
        if (iarg == nargs-1)
        {
          usage();
          return 1;
        }

        uint8_t data = 0;
        parsed = sscanf(args[++iarg],"0x%hhx", &data);
        if (parsed !=1)
        {
          usage();
          return 1;
        }

        ops[nops].write = true;
        ops[nops].val = data;
      }
      else
      {
        ops[nops].write = false;
      }

      ops[nops].adc = adc;
      ops[nops++].addr = addr;

      if (nops == MAX_OPS)
      {
        fprintf(stderr,"Hit max ops limit!\n");
        break;
      }
    }
  }


  didaq_setup_t setup = { 
    .spi_device = device,
    .spi_en_gpio_label = "NSPIBUS_EN"
  };

  didaq_dev_t * dev = didaq_open(&setup);


  if (! nops)
  {
    usage();
  }
  for (int iop = 0; iop < nops; iop++)
  {
    int ret = 0;

    if (ops[iop].write) 
    {
      printf("Writing 0x%hhx to reg 0x%hx on adc%d\n", ops[iop].val, ops[iop].addr, ops[iop].adc);
      ret = didaq_adc_sched_reg_write(dev,ops[iop].adc, ops[iop].addr, ops[iop].val);
    }
    else
    {
      printf("Reading reg 0x%hx on adc%d: \n", ops[iop].addr, ops[iop].adc);
      uint8_t val = 0;
      ret = didaq_adc_reg_read(dev,ops[iop].adc, ops[iop].addr, &val);
      if (!ret) printf(" result: 0x%hhx\n", val);
    }

    if (ret) 
    {
      fprintf(stderr,"Op returned %d, bailing\n", ret);
      break;
    }
  }


  return 0;

}

