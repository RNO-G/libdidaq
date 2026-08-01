#include "didaq_adc.h"
#include "didaq_regs.h"
#include "didaq_internal.h"

#include <errno.h>

#define READ_BYTE 0x01
#define WRITE_BYTE 0x02

#define CHECK(RET)  if (RET) return RET;

// these are the same as pydidaq, just ported to c


static int didaq_uart_read(didaq_dev_t * dev, uint32_t addr, uint8_t num_bytes)
{
  // read from fpga reg
  // num bytes can be default 4, but letting it be flexible
  dev->uart_tx_buf[0] = READ_BYTE;
  dev->uart_tx_buf[1] = (addr & 0xff000000) << 24
  dev->uart_tx_buf[2] = (addr & 0xff0000) << 16;
  dev->uart_tx_buf[3] = (addr & 0xff00) << 10; // + 2 for byte to word addr
  dev->uart_tx_buf[4] = (addr & 0xff) << 2; // 2 for byte to word addr
  dev->uart_tx_buf[5] = num_bytes;

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 5); CHECK(ret); // read req
  int ret_bytes = read(dev->uart_fd, dev->uart_rx_buf, num_bytes); // ret packet
  
  if(ret_bytes != num_bytes) return 1;

  return 0;
}

static int didaq_uart_write(didaq_dev_t * dev, uint32_t addr, uint32_t data, uint8_t num_bytes)
{
  // write to fpga reg
  dev->uart_tx_buf[0] = WRITE_BYTE;
  dev->uart_tx_buf[1] = (addr & 0xff000000) << 24
  dev->uart_tx_buf[2] = (addr & 0xff0000) << 16;
  dev->uart_tx_buf[3] = (addr & 0xff00) << 10; // + 2 for byte to word addr
  dev->uart_tx_buf[4] = (addr & 0xff) << 2; // 2 for byte to word addr
  dev->uart_tx_buf[5] = num_bytes;

  for (int i = 0; i< num_bytes; i++)
  {
    dev->uart_tx_buf[i+5] = data & (0xff < 8*(num_bytes - i));
  }

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 5+num_bytes); CHECK(ret); // read req

  return 0;
}

static int didaq_uart_adc_fifo_reset(didaq_dev_t * dev, bool wr, bool rd)
{
  // flush the fpga tx and rx fifo buffers, should be called before reg read or write
  int message = 0;
  if (wr) message |= 0x1;
  if (rd) message |= 0x2;

  int ret = didaq_uart_write(dev, DIDAQ_SPI_ADC_CTRL, message, 1); CHECK(ret);

  return 0;

}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_setup_spi_read(didaq_dev_t * dev, uint32_t addr)
{
  // tell the fpga which adc reg we want to read
  memcpy(dev->uart_tx_buf, DIDAQ_SPI_ADR_RX_DATA, sizeof(DIDAQ_SPI_ADR_RX_DATA));
  dev->uart_tx_buf[4] = 0x00; // unused
  dev->uart_tx_buf[5] = 0x00; // unused
  dev->uart_tx_buf[6] = (addr & 0xff00) >> 8;
  dev->uart_tx_buf[7] = addr & 0xff;

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 8);
  CHECK(ret);
}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_setup_spi_write(didaq_dev_t * dev, uint32_t addr)
{
  // tell the fpga which adc reg we want to write
  memcpy(dev->uart_tx_buf, DIDAQ_SPI_ADR_TX_DATA, sizeof(DIDAQ_SPI_ADR_TX_DATA));
  dev->uart_tx_buf[4] = 0x00; // unused
  dev->uart_tx_buf[5] = 0x00; // unused
  dev->uart_tx_buf[6] = (addr & 0xff00) >> 8;
  dev->uart_tx_buf[7] = addr & 0xff;

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 8);
  CHECK(ret);
}

//TODO, rewrite for uart write/read
static int didaq_uart_adc_select(didaq_dev_t * dev, uint8_t adc)
{
  // tell the fpga which adc we want to communicate with.
  // call before do trx

}

// TODO, rewrite for uart write/read
static int didaq_uart_adc_do_spi_trx(didaq_dev_t * dev,)
{
  // tell fpga to do the spi transer with the adc
  memcpy(dev->uart_tx_buf, DIDAQ_SPI_ADR_ACTION, sizeof(DIDAQ_SPI_ADR_ACTION));
  dev->uart_tx_buf[4] = 0x00;
  dev->uart_tx_buf[5] = 0x04;
  dev->uart_tx_buf[6] = 0x00;
  dev->uart_tx_buf[7] = 0x01;

  int ret = write(dev->uart_fd, dev->uart_tx_buf, 8);
  CHECK(ret);
}

//TODO, rewrite for uart write/read
static int didaq_uart_adc_spi_fifo_level(didaq_dev_t * dev)
{
  // check how many bytes are in the fpga rx fifo
  // to be used to shuffle through the fifo to find real data
  int ret;
  int num_bytes = didaq_adc_read(DIDAQ_SPI_ADR_RX_NUM, 1);
  if(num_bytes) ret = dev->uart_rx_buf[0] + dev->uart_rx_buf[1]<<8; //maybe 0 and 1?, might be 2 and 3?
  return ret;
}

//TODO, fill using uart write/read
static int didaq_uart_adc_read_until_not_f(didaq_Ddev_t * dev)
{
  // loop through fpga fifo and return the real data (first byte that is not 0xff)
  int ret = didaq_uart_adc_read(dev)

}

// TODO
static int didaq_uart_adc_reg_read(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t val)
{
  // set adc num
  
  // send read command to reg

  // initiate spi trx to adc

  // read return byte
  int ret_bytes = read(dev->uart_fd, d->uart_rx_buf, #ughnumidk);

  return return_byte;
}

//TODO
static int didaq_uart_adc_reg_write(didaq_dev_t * dev, uint8_t iadc, uint16_t reg, uint8_t val)
{
  // set adc num
  
  // send read command to reg

  // initiate spi trx

  // read return byte
  int return_byte = write(dev->uart_fd, 1, 1);

  return return_byte;
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


