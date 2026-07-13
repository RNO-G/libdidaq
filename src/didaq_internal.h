#ifndef _DIDAQ_INTERNAL_H
#define _DIDAQ_INTERNAL_H

#include "libgpios.h"
#include <linux/spi/spidev.h>
#include <stdio.h>
#include "didaq_regs.h"
#include "didaq.h"

/** This file is not part of the API, and may change willy-nilly */

typedef struct didaq_txn
{
  uint8_t payload[6];
  uint32_t orig_len;
  uint32_t elem_len;
  char * orig_dest;
} didaq_txn_t;


#define DIDAQ_PIPELINED_BUFFERS(NAME, ADDR, NADDR, RW, VAR, T) \
  DIDAQ_IIF(DIDAQ_IS_ZERO(VAR))(\
   /*not variable */, \
    T pipelined_##NAME[NADDR][VAR]; )


struct didaq_dev
{
  didaq_setup_t setup;
  int spi_fd;

  gpios_line_t spi_en;
  gpios_line_t trig_rdy;
  didaq_reg_capture_ctl_t capture_ctl;
  didaq_reg_phas_trig_ctl_t phased_ctl;
  didaq_reg_coin_trig_ctl_t coin_ctl[DIDAQ_NUM_COINC];

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
  int dbg;
  uint32_t poll_usleep_amt;
  int event_ready;
  struct timespec event_ready_time;

  int run_number;

  // pipeline buffers
  DIDAQ_REGS(DIDAQ_PIPELINED_BUFFERS)

  //start at -1 to indicate nothing selected
  int selected_adc;

};




#endif
