#ifndef DIDAQ_SDM_H
#define DIDAQ_SDM_H

#define DIDAQ_SDM_BASE_ADDR 0x010C0000

#include "didaq.h"

enum e_didaq_sdm_reg
{

  DIDAQ_SDM_COMMAND_ADDR           = DIDAQ_SDM_BASE_ADDR | ( 0x0 << 2),
  DIDAQ_SDM_COMMAND_LAST_WORD_ADDR = DIDAQ_SDM_BASE_ADDR | ( 0x1 << 2),
  DIDAQ_SDM_READ_ADDR              = DIDAQ_SDM_BASE_ADDR | ( 0x5 << 2),

};

typedef union
{
  uint8_t bytes[4];
  uint32_t word;
} didaq_sdm_data_t;


int didaq_sdm_read_values(didaq_dev_t * dev, size_t N, didaq_sdm_data_t *dest);


int didaq_sdm_write(didaq_dev_t * dev, uint32_t addr, didaq_sdm_data_t data);


#endif



