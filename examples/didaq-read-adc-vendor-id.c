#include "didaq.h"
#include "didaq_adc.h"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>


int main (int nargs, char ** args) 
{
    // open spi and uart devices and try to read adc vendor id using uart

    const char * spidev = "/dev/spidev1.0";
    const char * uartdev = "/dev/ttyUSB0";


    didaq_setup_t setup = { 
    .spi_device = spidev,
    .spi_en_gpio_label = "NSPIBUS_EN",
    .uart_device = uartdev
  };

  didaq_dev_t * dev = didaq_open(&setup);

  // read adc
  uint8_t iadc = 0; // choose adc 0
  uint16_t reg_addr = 0x0C; // vendor id at reg 0xC
  uint8_t trx_bytes = 3; // always 3 for adc regs

  // quick check for firmware version
  didaq_uart_read(dev,0x0102000,1);
  printf("FPGA Firm. Ver. %x%x%x%x", dev->uart_rx_buf[0], dev->uart_rx_buf[1],dev->uart_rx_buf[2], dev->uart_rx_buf[3]);

  // check low byte of adc vendor id
  int val = didaq_adc_reg_read(dev, iadc, reg_addr, trx_bytes);
  printf("does %d = 81?\n", val);

  return didaq_close(dev);

}