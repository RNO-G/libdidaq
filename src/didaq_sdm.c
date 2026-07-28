#include "didaq_sdm.h"
#include "didaq_regs.h"
#include <unistd.h>

int didaq_sdm_write(didaq_dev_t * dev, uint32_t addr, didaq_sdm_data_t data)
{
  return didaq_sysaccess_write(dev, addr | DIDAQ_SDM_BASE_ADDR, data.word);
}


int didaq_sdm_read_values(didaq_dev_t * dev, size_t N, didaq_sdm_data_t *dest)
{

  for (size_t i = 0; i < N; i++)
  {
    usleep(5000);
    int ret = didaq_sysaccess_read(dev, DIDAQ_SDM_READ_ADDR | DIDAQ_SDM_BASE_ADDR, &dest[i].word);
    if (ret) return ret;
  }

  return 0;
}
