#include "didaq.h"
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
  uint16_t reg_addr = 0x0C; // id at reg 0xC
  uint8_t trx_bytes = 3; // 3 for a read
  didaq_adc_reg_read(dev, iadc, reg_addr, trx_bytes);

  printf("ind 0: %d, 1: %d, 2: %d, 3: %d\n", dev->uart_rx_buf[0],dev->uart_rx_buf[1],dev->uart_rx_buf[2],dev->uart_rx_buf[3]);
  printf("hopefully find should find 42 somewhere\n");

  return didaq_close(dev);

}