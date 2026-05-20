#ifndef _DIDAQ_INTERNAL_H
#define _DIDAQ_INTERNAL_H

#include "libgpios.h"
#include <linux/spi/spidev.h>
#include <stdio.h>
#include "didaq_regs.h"

/** This file is not part of the API, and may change willy-nilly */

typedef struct didaq_txn
{
  union
  {
    struct
    {
      uint16_t write : 1;
      uint16_t addr  : 15;
    } split;
    uint16_t unified;
  } addr;
  uint8_t payload[4];
  uint32_t orig_len;
  uint32_t elem_len;
  char * orig_dest;
} didaq_txn_t;

struct didaq_dev
{
  int spi_fd;

  gpios_line_t spi_en;
  gpios_line_t trig_rdy;

  struct spi_ioc_transfer xfers[511];
  didaq_txn_t tx_bufs[511]; // memory for tx transactions
  didaq_txn_t rx_bufs[511]; // memory for (short) rx transactions
  size_t nxfers;
  size_t nxfers_complete;
  size_t spi_bufsiz;
  size_t spi_bufsiz_complete;
  size_t spi_max_bufsiz;
  uint32_t revision;
  uint32_t board_id;
  FILE * ferr;
};




#endif
