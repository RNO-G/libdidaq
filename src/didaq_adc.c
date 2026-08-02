#include "didaq_adc.h"
#include "didaq_regs.h"
#include "didaq_internal.h"

#include <errno.h>

#define READ_BYTE 0x01
#define WRITE_BYTE 0x02
#define BYTES_PER_WORD 4

#define CHECK(RET)  if (RET) return RET;

// these are the same as pydidaq, just ported to c


static int didaq_uart_read(didaq_dev_t * dev, uint32_t addr, uint8_t num_words)
{
  // read from fpga reg
  // num bytes can be default 4, but letting it be flexible
  dev->uart_tx_buf[0] = READ_BYTE;
  dev->uart_tx_buf[1] = (addr & 0xff000000) << 24;
  dev->uart_tx_buf[2] = (addr & 0xff0000) << 16;
  dev->uart_tx_buf[3] = (addr & 0xff00) << 10; // + 2 for byte to word addr
  dev->uart_tx_buf[4] = (addr & 0xff) << 2; // 2 for byte to word addr
  dev->uart_tx_buf[5] = num_words;

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 6); CHECK(ret); // read req
  int ret_bytes = read(dev->uart_fd, dev->uart_rx_buf, num_words*BYTES_PER_WORD); // ret packet
  
  if(ret_bytes != num_words*BYTES_PER_WORD) return 1;

  return 0;
}

static int didaq_uart_write(didaq_dev_t * dev, uint32_t addr, uint32_t data, uint8_t num_words)
{
  // write to fpga reg
  dev->uart_tx_buf[0] = WRITE_BYTE;
  dev->uart_tx_buf[1] = (addr & 0xff000000) << 24;
  dev->uart_tx_buf[2] = (addr & 0xff0000) << 16;
  dev->uart_tx_buf[3] = (addr & 0xff00) << 10; // + 2 for byte to word addr
  dev->uart_tx_buf[4] = (addr & 0xff) << 2; // 2 for byte to word addr
  dev->uart_tx_buf[5] = num_words; // usually 1

  for (int i = 0; i< num_words*BYTES_PER_WORD; i++)
  {
    // this should also take care of big/little endian ordering
    dev->uart_tx_buf[i+5] = (data >> 8*(num_words*BYTES_PER_WORD-i-1)) & 0xff;
  }

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 6+num_words*BYTES_PER_WORD); CHECK(ret);

  return 0;
}

int didaq_uart_adc_set_spi_cfg(didaq_dev_t * dev, uint8_t spi_speed_setting)
{
  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_SETNGS_0, 1);
  if(ret != BYTES_PER_WORD) return 1;

  // TODO check order
  int message = (dev->uart_rx_buf[3]<<24) + (dev->uart_rx_buf[2]<<16) + (dev->uart_rx_buf[1]<<8) + (((spi_speed_setting<<2) & 0x3C) | dev->uart_rx_buf[0]);
  didaq_uart_write(dev, DIDAQ_SPI_ADR_SETNGS_0, message, 1);
  didaq_uart_adc_fifo_reset(dev, true, true);

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

//TODO, rewrite for uart write/read
static int didaq_uart_adc_spi_fifo_level(didaq_dev_t * dev)
{
  // check how many bytes are in the fpga rx fifo
  // to be used to shuffle through the fifo to find real data
  int buffer_level = 0;
  int num_bytes = didaq_uart_read(dev, DIDAQ_SPI_ADR_RX_NUM, 1);
  if(num_bytes != BYTES_PER_WORD) return 0; 
  
  buffer_level = dev->uart_rx_buf[3] + (dev->uart_rx_buf[2]<<8); //maybe 0 and 1?, might be 2 and 3?
  return buffer_level;
}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_read_single_rx_buffer(didaq_dev_t * dev, int num_bytes)
{
  int ret = didaq_uart_read(dev, DIDAQ_SPI_ADR_RX_DATA, 1); CHECK(ret);
  int data = dev->uart_rx_buf[0]; // maybe 3?
  return data;
}

//TODO, fill using uart write/read
static int didaq_uart_adc_read_until_not_f(didaq_dev_t * dev)
{
  // loop through fpga fifo and return the real data (first byte that is not 0xff)
  //int ret = didaq_uart_adc_read(dev)

  int fifo_level = didaq_uart_adc_spi_fifo_level(dev);
  int data = 0;
  for(int i = 0; i<fifo_level; i++)
  {
    data = didaq_uart_adc_read_single_rx_buffer(dev, 1);
    if(data!=0xff) printf("uh oh, found %d in fifo before last entry\n", data);
  }

  data = didaq_uart_adc_read_single_rx_buffer(dev, 1);

  return data;
}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_fill_tx_buffer(didaq_dev_t * dev, uint32_t data, uint8_t num_bytes)
{
  // fill fpga's tx buffer
  int ret;
  for(int i = 0; i<num_bytes; i++)
  {
    ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_TX_DATA, (data >> 8*i) &0xff, 1); CHECK(ret);
  }
  return 0;
}

//TODO, rewrite for uart write/read
static int didaq_uart_adc_select(didaq_dev_t * dev, uint8_t adc)
{
  // tell the fpga which adc we want to communicate with.
  // call before do rx or tx. accesses user reg space
  uint32_t spi_sel_reg_addr = 0x01080000 | 0x0D<<2;

  int ret = didaq_uart_write(dev, spi_sel_reg_addr, adc&0x7, 1); CHECK(ret);

  return 0;
}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_do_spi_trx(didaq_dev_t * dev, int num_bytes_per_trx)
{
  // tell fpga to do the spi transer with the adc
  int packet = ((0xff&num_bytes_per_trx) << 16) + 1;
  int ret = didaq_uart_write(dev, DIDAQ_SPI_ADR_ACTION, packet, 1); CHECK(ret);
  return 0;
}


// TODO
int didaq_uart_adc_reg_read(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t trx_bytes)
{
  // set adc num
  int ret = didaq_uart_adc_select(dev, iadc); CHECK(ret);
  
  // reset fifo
  ret += didaq_uart_adc_fifo_reset(dev, 1, 1); CHECK(ret);
  
  // fill tx buffer
  int packet = ((0x80 | ((0x3f>>8) & reg))<<16) + (reg & 0xff << 8); // omg this byte ordering is all over the place
  ret += didaq_uart_adc_fill_tx_buffer(dev, packet, 3); CHECK(ret);

  // initiate spi trx to adc
  ret += didaq_uart_adc_do_spi_trx(dev, 3);

  // not sure about checking the ret

  // read return byte
  int val = didaq_uart_adc_read_until_not_f(dev);

  return val;
}

//TODO
int didaq_uart_adc_reg_write(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint16_t data)
{
  // set adc num
  int ret = didaq_uart_adc_select(dev, iadc);

  ret += didaq_uart_adc_fifo_reset(dev, 1, 1);

  //[0x7F & ((0x3F00 & adr) >> 8), adr & 0xFF, data & 0xFF]
  int packet = ((0x7f & ((0x3f00>>8) & reg))<<16) + (reg & 0xff << 8) + data & 0xff; // omg this byte ordering is all over the place
  ret += didaq_uart_adc_fill_tx_buffer(dev, packet, 3);

  // initiate spi trx
  ret += didaq_uart_adc_do_spi_trx(dev, 3);
  
  // check returns to make sure consistant with error codes, or if num bytes
  CHECK(ret);
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


