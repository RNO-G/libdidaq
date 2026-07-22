#include "didaq_adc.h"
#include "didaq_regs.h"
#include "didaq_internal.h"

#include <errno.h>



#define CHECK(RET)  if (RET) return RET;

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


