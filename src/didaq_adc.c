#include "didaq_adc.h"
#include "didaq_regs.h"
#include "didaq_internal.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define READ_BYTE 0x01
#define WRITE_BYTE 0x02
#define BYTES_PER_WORD 4

#define CHECK(RET)  if (RET) return RET;

// these are the same as pydidaq, just ported to c

int didaq_usleep(int time_sleep_us)
{
  // helper function for abs time sleep

  struct timespec t0, t_end;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  if(time_sleep_us*1000 + t0.tv_nsec > 999999999)
  {
    t_end.tv_sec = t0.tv_sec + 1;
    t_end.tv_nsec = time_sleep_us * 1000 + t0.tv_nsec - 999999999;
  }
  else
  {
    t_end.tv_sec = t0.tv_sec;
    t_end.tv_nsec = time_sleep_us * 1000 + t_end.tv_sec;
  }

  while (EINTR==clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_end, 0));

  return 0;
}

int didaq_uart_write(didaq_dev_t * dev, uint32_t addr, uint32_t data, uint8_t num_words, int read_req)
{
  if(dev->uart_fd < 0) return -1;

  memset(dev->uart_tx_buf, 0, sizeof(dev->uart_tx_buf));

  // write/read req to fpga reg
  dev->uart_tx_buf[0] = (read_req) ? READ_BYTE : WRITE_BYTE;

  dev->uart_tx_buf[1] = (addr & 0xff000000) >> 24; // map offset
  dev->uart_tx_buf[2] = (addr & 0xff0000) >> 16; // map offset
  dev->uart_tx_buf[3] = ((addr << 2) & 0xff00) >> 8; // convert word to byte addr
  dev->uart_tx_buf[4] = ((addr << 2 ) & 0xff); // convert word to byte addr
  dev->uart_tx_buf[5] = num_words; // usually 1

  if(read_req) num_words = 0; // don't actually send anything on a request

  for (int i = 0; i< num_words*BYTES_PER_WORD; i++)
  {
    // this should also take care of big/little endian ordering
    // e.g. fifo reset writes 0x3. these non-zero bits should be sent last to be cons. with pydidaq
    dev->uart_tx_buf[i+6] = (data >> (8*(num_words*BYTES_PER_WORD-i-1))) & 0xff;
  }

  int sent_bytes = 0;
  int ret = 0;
  int timeout_us = 10000;
  int elapsed_us = 0;
  struct timespec t0, tnow;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  while(sent_bytes < 6+num_words*BYTES_PER_WORD-1)
  {
    ret = write(dev->uart_fd, dev->uart_tx_buf+sent_bytes, 6+num_words*BYTES_PER_WORD-sent_bytes);

    // if bad error stop, otherwise try again with remaining bytes?
    if(ret < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
    else sent_bytes += ret;

    ret = didaq_usleep(500); CHECK(ret);

    // timeout
    clock_gettime(CLOCK_MONOTONIC, &tnow);
    elapsed_us = (tnow.tv_sec - t0.tv_sec) * 1000000 + (tnow.tv_nsec - t0.tv_nsec) / 1000;
    if (elapsed_us > timeout_us) 
    {
      printf("timeout write, sent bytes %d\n", sent_bytes);
      return -sent_bytes;
    }
  }
  ret = didaq_usleep(50000); CHECK(ret);

  return 0;
}

int didaq_uart_read(didaq_dev_t * dev, uint32_t addr, uint8_t num_words)
{
  if(dev->uart_fd < 0) return -1;

  // read from fpga reg
  // num bytes (words) usally just 4 (1), but letting it be flexible
  // high byte fills rx buf [0], low byte fulls rx buf[3]

  int ret = didaq_uart_write(dev, addr, 0, 0); CHECK(ret); // read req

  memset(dev->uart_rx_buf, 0, sizeof(dev->uart_rx_buf));

  int count_ret = 0;
  struct timespec t0, tnow;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  int elapsed_us = 0;
  int timeout_us = 500000;

  while(count_ret < num_words*BYTES_PER_WORD-1)
  {
    ret = read(dev->uart_fd, dev->uart_rx_buf+count_ret, num_words*BYTES_PER_WORD-count_ret);
    if(ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    else count_ret += ret;

    // if we're good, return now, otherwise it will wait a bit and check again
    if (count_ret >= num_words*BYTES_PER_WORD) return 0;

    ret = didaq_usleep(1000); CHECK(ret);

    // timeout
    clock_gettime(CLOCK_MONOTONIC, &tnow);
    elapsed_us = (tnow.tv_sec - t0.tv_sec) * 1000000 + (tnow.tv_nsec - t0.tv_nsec) / 1000;
    if(elapsed_us > timeout_us)
    {
      printf("timout read, read bytes %d\n", count_ret);
      return -count_ret;
    }
  }

  return 0;
}

static int didaq_uart_adc_fifo_reset(didaq_dev_t * dev, bool wr, bool rd)
{
  // flush the fpga tx and rx fifo buffers, should be called before reg read or write
  uint32_t message = 0;
  if (wr) message |= 0x1;
  if (rd) message |= 0x2;

  int ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_CTRL, message, 1); CHECK(ret);

  return 0;
}

int didaq_uart_adc_set_spi_cfg(didaq_dev_t * dev, uint8_t spi_speed_setting)
{
  if(dev->uart_fd < 0) return -1;

  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_SETNGS_0, 1); CHECK(ret);

  int message = (dev->uart_rx_buf[0]<<24) + (dev->uart_rx_buf[1]<<16) + (dev->uart_rx_buf[2]<<8) + (((spi_speed_setting<<2) & 0x3C) | dev->uart_rx_buf[3]);
  ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_SETNGS_0, message, 1); CHECK(ret);
  ret = didaq_uart_adc_fifo_reset(dev, true, true); CHECK(ret);

  return 0;
}

static int didaq_uart_adc_spi_rx_fifo_level(didaq_dev_t * dev)
{
  // check how many bytes are in the fpga rx fifo
  // to be used to shuffle through the fifo to find real data
  int buffer_level = 0;
  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_RX_NUM, 1); CHECK(ret);

  buffer_level = dev->uart_rx_buf[3] & 0xff;
  return buffer_level;
}

static int didaq_uart_adc_spi_tx_fifo_level(didaq_dev_t * dev)
{
  // check how many bytes are in the fpga rx fifo
  // to be used to shuffle through the fifo to find real data
  int buffer_level = 0;
  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_TX_NUM, 1); CHECK(ret);
  // if(num_bytes != BYTES_PER_WORD) return 0; // not sure about ret checking here
  
  buffer_level = dev->uart_rx_buf[3];
  return buffer_level;
}

static int didaq_uart_adc_read_single_rx_buffer(didaq_dev_t * dev, int num_bytes)
{
  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_RX_DATA, 1);
  if(ret) return -1;

  int data = dev->uart_rx_buf[3];
  return data;
}

static int didaq_uart_adc_read_until_not_ff(didaq_dev_t * dev)
{
  // loop through fpga fifo and return the real data (first byte that is not 0xff)
  //int ret = didaq_uart_adc_read(dev)

  int fifo_level = didaq_uart_adc_spi_rx_fifo_level(dev);
  if(fifo_level < 0) return -1;

  int data = 0;
  for(int i = 0; i<fifo_level-1; i++)
  {
    data = didaq_uart_adc_read_single_rx_buffer(dev, 1);
    if(data < 0) return -1;
    //if(data!=0xff) printf("uh oh, found %d in fifo before last entry\n", data);
  }

  data = didaq_uart_adc_read_single_rx_buffer(dev, 1);
  if(data < 0) return -1;

  return data;
}

static int didaq_uart_adc_fill_tx_buffer(didaq_dev_t * dev, uint32_t data, uint8_t num_bytes)
{
  // fill fpga's tx buffer
  int ret = 0;
  for(int i = 0; i<num_bytes; i++)
  {
    ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_TX_DATA, (data >> (8*(num_bytes-i-1))) &0xff, 1); CHECK(ret);
  }
  return 0;
}

static int didaq_uart_adc_select(didaq_dev_t * dev, uint8_t adc)
{
  // tell the fpga which adc we want to communicate with.
  // call before do rx or tx. accesses user reg space
  uint32_t spi_sel_reg_addr = 0x01080000 + 0x0D;

  int ret = didaq_uart_write(dev, spi_sel_reg_addr, adc&0x7, 1); CHECK(ret);

  return 0;
}

static int didaq_uart_adc_do_spi_trx(didaq_dev_t * dev, uint8_t num_bytes_per_trx)
{

  // tell fpga to do the spi transer with the adc
  int packet = ((0xff&num_bytes_per_trx) << 16) + 1;
  int ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_ACTION, packet, 1); CHECK(ret);

  // we should technically able to check the trx status bit to know if it's still happening
  // but who knows if it really works or not
  int spi_busy = 1;

  int timeout_us = 10000;
  int elapsed_us = 0;
  struct timespec t0, tnow;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  while(spi_busy > 0)
  {
    ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_ACTION, 1); CHECK(ret);
    spi_busy = dev->uart_rx_buf[3] & 0x1;

    // timeout
    clock_gettime(CLOCK_MONOTONIC, &tnow);
    elapsed_us = (tnow.tv_sec - t0.tv_sec) * 1000000 + (tnow.tv_nsec - t0.tv_nsec) / 1000;
  
    if (elapsed_us > timeout_us) 
    {
      printf("timeout fpga-adc spi trx\n");
      return -1;
    }
    if(spi_busy == 0) return 0;

    ret = didaq_usleep(100); CHECK(ret);
  }

  // absolute wait option?
  // ret = didaq_usleep(10000); CHECK(ret);

  return 0;
}

int didaq_uart_adc_reg_read(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t trx_bytes)
{
  if(dev->uart_fd < 0) return -1;

  // set adc num, can probably optimize by tracking current adc in reg
  int ret = didaq_uart_adc_select(dev, iadc); CHECK(ret);
  
  // reset fifo
  ret = didaq_uart_adc_fifo_reset(dev, true, true); CHECK(ret);
  
  // fill tx buffer
  int packet = ((0x80 | ((0x3f&reg) >> 8))<<16) + ((reg & 0xff) << 8);
  ret = didaq_uart_adc_fill_tx_buffer(dev, packet, 3); CHECK(ret);

  // initiate spi trx to adc
  ret = didaq_uart_adc_do_spi_trx(dev, 3); CHECK(ret);

  // read return byte
  int val = didaq_uart_adc_read_until_not_ff(dev);
  if(val < 0) return -1;

  return val;
}

int didaq_uart_adc_reg_write(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint16_t data)
{
  if(dev->uart_fd < 0) return -1;

  // set adc num
  int ret = didaq_uart_adc_select(dev, iadc); CHECK(ret);

  ret = didaq_uart_adc_fifo_reset(dev, true, true); CHECK(ret);

  int packet = ((0x7f & ((0x3f00&reg) >> 8))<<16) + ((reg & 0xff) << 8) + (data & 0xff); // omg this byte ordering is all over the place
  ret = didaq_uart_adc_fill_tx_buffer(dev, packet, 3); CHECK(ret);

  // initiate spi trx
  ret = didaq_uart_adc_do_spi_trx(dev, 3); CHECK(ret);
  
  return 0;
}

static int didaq_adc_sched_reg_rw(didaq_dev_t * dev, bool write, uint8_t iadc, uint16_t reg, uint8_t * val)
{

  if (iadc < 0 || iadc >= DIDAQ_NUM_ADC) return -EINVAL;

  int ret = 0;
  //see if we need to switch adc
  if (dev->selected_adc != iadc)
  {
    ret = didaq_sched_write_ADC_SPI_SEL(dev, & (const didaq_reg_spi_sel_t) { .spi_sel = iadc} );
    CHECK(ret);
    dev->selected_adc = iadc;
  }

  //write the address
  ret = didaq_sched_sysaccess_write(dev, DIDAQ_SPI_ADR_TX_DATA,
      ( write ? 0x7f  : 0x80 ) & (( 0x3f00 & reg) >> 8));

  CHECK(ret);

  ret = didaq_sched_sysaccess_write(dev, DIDAQ_SPI_ADR_TX_DATA, reg & 0xff);
  CHECK(ret);

  ret = didaq_sched_sysaccess_write(dev, DIDAQ_SPI_ADR_TX_DATA, write ? *val : 0x00);
  CHECK(ret);

  // 3 byte transaction
  ret = didaq_sched_sysaccess_write(dev, DIDAQ_SPI_ADR_ACTION, (3 << 16 ) | 0x01);
  CHECK(ret);

  if (!write)
  {
    uint32_t rx_bytes = 0;
    ret = didaq_sysaccess_read(dev, DIDAQ_SPI_ADR_RX_NUM, &rx_bytes);
    CHECK(ret);

    rx_bytes &= 0xffff;

    uint32_t value = 0;
    while (rx_bytes > 0)
    {
      uint32_t ignored = 0;
      ret = didaq_sched_sysaccess_read(dev, DIDAQ_SPI_ADR_RX_DATA, &ignored);
      CHECK (ret);
      ret = didaq_sysaccess_read(dev, DIDAQ_SPI_ADR_RX_NUM, &rx_bytes);
      CHECK(ret);
    }
    *val = value & 0xff;
  }

  return 0;
}

int didaq_adc_sched_reg_write(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t val)
{
  return didaq_adc_sched_reg_rw(dev, true, iadc, reg, &val);
}

int didaq_adc_reg_write(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t val)
{
  return didaq_adc_sched_reg_write(dev, iadc, reg, val) || didaq_complete(dev);
}


int didaq_adc_reg_read(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t *val)
{
  return didaq_adc_sched_reg_rw(dev, false, iadc, reg, val);
}


