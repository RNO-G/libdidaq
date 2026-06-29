#include "didaq_regs.h"
#include "didaq_internal.h"
#include <errno.h>
#include "didaq_helpers.h"
#include <sys/ioctl.h>
#include <assert.h>



static int didaq_append_tx(didaq_dev_t * dev, uint16_t addr, uint32_t payload)
{
  if (!dev) return -EIO;

  // check if we are going to overflow bufsiz and complete existing transactions if that's the case
  if ( (dev->spi_bufsiz + 6 > dev->spi_max_bufsiz) || dev->nxfers == 511)
  {
    int ret = didaq_complete(dev);
    if (ret) return ret;
  }

  size_t idx = dev->nxfers++;
  dev->tx_bufs[idx].payload[1] = addr & 0xff;
  dev->tx_bufs[idx].payload[0]  = addr >> 8;
  dev->tx_bufs[idx].payload[2] = (payload & 0xff000000) >> 24;
  dev->tx_bufs[idx].payload[3] = (payload & 0xff0000) >> 16;
  dev->tx_bufs[idx].payload[4] = (payload & 0xff00) >> 8;
  dev->tx_bufs[idx].payload[5] = (payload & 0xff);

  if (dev->dbg)
  {
    fprintf(dev->ferr, " ( APPENDTX [xfer%zu]: %x %x %x %x %x %x ) \n",
                   idx,
                   dev->tx_bufs[idx].payload[0],
                   dev->tx_bufs[idx].payload[1],
                   dev->tx_bufs[idx].payload[2],
                   dev->tx_bufs[idx].payload[3],
                   dev->tx_bufs[idx].payload[4],
                   dev->tx_bufs[idx].payload[5]
                   );
  }

  dev->xfers[idx].tx_buf = (uint64_t) &dev->tx_bufs[idx];
  dev->xfers[idx].rx_buf = 0;
  dev->xfers[idx].len = 6;
  dev->spi_bufsiz += 6;

  return 0;
}

static int didaq_append_rx(didaq_dev_t * dev, uint16_t addr, size_t elem_len, size_t len,  char * dest,
    const void * pipelined_buf, size_t pipelined_buf_siz)
{
  if (!dev) return -EIO;

  //boo
  if (len > dev->spi_max_bufsiz)
  {
    fprintf(dev->ferr,"Trying to add an SPI transaction of %zu but max SPI bufsiz is %zu. Increase bufsiz.\n", len, dev->spi_max_bufsiz);
    return -EIO;
  }


  // check if we are going to overflow bufsiz and complete existing transactions if that's the case
  if ( (dev->spi_bufsiz + (len > 4 ? 2 + len : 6) > dev->spi_max_bufsiz) || (dev->nxfers == 511))
  {
    int ret = didaq_complete(dev);
    if (ret) return ret;
  }

  size_t idx = dev->nxfers++;
  dev->xfers[idx].tx_buf = (uint64_t) &dev->tx_bufs[idx];

  dev->tx_bufs[idx].payload[1] = addr & 0xff;
  dev->tx_bufs[idx].payload[0]  = addr >> 8;
  dev->tx_bufs[idx].payload[0]  |= 0x80;

  dev->rx_bufs[idx].orig_len = len;
  dev->rx_bufs[idx].orig_dest = dest;
  dev->rx_bufs[idx].elem_len = elem_len;

  //the normal short case
  if (len <= 4)
  {
    dev->xfers[idx].rx_buf = (uint64_t) &dev->rx_bufs[idx];
    dev->xfers[idx].len = 6;
    dev->spi_bufsiz += 6;
  }
  // use our buffer to startwith only external buffer
  else
  {
    assert(pipelined_buf_siz && pipelined_buf);

    //first send the address as a first transaction
    dev->xfers[idx].len = 2;
    dev->xfers[idx].rx_buf = 0;

    dev->spi_bufsiz += 2;


    //now for the payload with the external buffer
    //check if we need to do a transaction
    if ( (dev->spi_bufsiz + len > dev->spi_max_bufsiz) || (dev->nxfers == 511))
    {
      int ret = didaq_complete(dev);
      if (ret) return ret;
    }

    idx = dev->nxfers++;
    dev->xfers[idx].len = len;
    dev->xfers[idx].tx_buf = (uint64_t)  ( (uint8_t*) pipelined_buf + (pipelined_buf_siz - len));
    dev->xfers[idx].rx_buf = (uint64_t) dest;
    dev->spi_bufsiz += len;


    // if len is not a multiple of 4, we will need to throw out a few bytes at the end
    // (hopefully we never need to exercise this code :)
    if (len % 4)
    {
      if ( (dev->spi_bufsiz + (4-(len %4)) > dev->spi_max_bufsiz) || (dev->nxfers == 511))
      {
         int ret = didaq_complete(dev);
         if (ret) return ret;
      }

      idx = dev->nxfers++;
      dev->xfers[idx].len = 4- (len % 4);
      dev->xfers[idx].tx_buf = 0;
      dev->xfers[idx].rx_buf = 0;
    }
  }

  return 0;
}


int didaq_complete(didaq_dev_t * dev)
{
  // do the SPI transaction
  if (!dev->nxfers) //nothing to do
    return 0;

  assert(dev->nxfers <=511);

  errno = 0;
  int ret = ioctl(dev->spi_fd, SPI_IOC_MESSAGE(dev->nxfers), dev->xfers);

  defer { dev->nxfers = 0; }
  defer { dev->spi_bufsiz = 0; }

  size_t nxfers_to_zero = dev->nxfers;
  defer { memset(dev->xfers,0, nxfers_to_zero * sizeof(*dev->xfers)); }
  defer { memset(dev->rx_bufs,0, nxfers_to_zero * sizeof(*dev->rx_bufs)); }
  defer { memset(dev->tx_bufs,0, nxfers_to_zero * sizeof(*dev->tx_bufs)); }

  if (ret < 0)
  {
    fprintf(dev->ferr, "didaq_complete: ioctl(SPI_IOC_MESSAGE(%ld)) returned %d (%s)\n", dev->nxfers, errno, strerror(errno));

    return -errno;
  }


  if (ret != dev->spi_bufsiz)
  {
    fprintf(dev->ferr,"didaq_complete: Expected %zu bytes transfered by ioctl, but only %d were transferred?\n", dev->spi_bufsiz, ret);
  }

  dev->spi_bufsiz_complete += ret;

  //loop over all transactions, fix endianess, copy to correct buffer if needed
  for (int ixfer = 0; ixfer < dev->nxfers; ixfer++)
  {
    // we read in something
    if ( dev->xfers[ixfer].rx_buf)
    {

      if (dev->dbg)
      {
        fprintf(dev->ferr, " ( RX [xfer%d]:\n", ixfer);
        char * buf = (char*) dev->xfers[ixfer].rx_buf;
        for (int ib = 0; ib < dev->xfers[ixfer].len; ib++)
        {
          fprintf(dev->ferr,"%s0x%02hhx", ib == 0 ? "," : "", buf[ib]);
        }
        fprintf(dev->ferr, " )\n");
      }

      //we need to copy out to external buffer
      if (dev->rx_bufs[ixfer].orig_dest !=  (char*) dev->xfers[ixfer].rx_buf)
      {
        memcpy(dev->rx_bufs[ixfer].orig_dest, dev->rx_bufs[ixfer].payload+2, dev->rx_bufs[ixfer].orig_len);
      }

      uint32_t elem_len = dev->rx_bufs[ixfer].elem_len;
      // fix endianness if needed
      if (elem_len > 1)
      {
        for (size_t ielem = 0; ielem < dev->xfers[ixfer].len / dev->rx_bufs[ixfer].elem_len; ielem++)
        {
          if (elem_len == 4)
          {
            *( (uint32_t*) (dev->rx_bufs[ixfer].orig_dest + ielem*elem_len)) = be32toh(*( (uint32_t*) (dev->rx_bufs[ixfer].orig_dest + ielem*elem_len)));
          }
          else if (elem_len == 2)
          {
            *( (uint16_t*) (dev->rx_bufs[ixfer].orig_dest + ielem*elem_len)) = be16toh(*( (uint16_t*) (dev->rx_bufs[ixfer].orig_dest + ielem*elem_len)));
          }
          else
          {
            assert(1);
          }
        }
      }
    }
  }
  dev->nxfers_complete += dev->nxfers;


  return 0;

}




const int offset = 0; //purposely shadowed
const size_t num_rbytes = 4; //purposely shadowed

// Read function implementations. The var_read case uses a static buffer to keep feeding the address.
#define DIDAQ_READ_FNS_IMPL(NAME,ADDR,NADDR,RW,VAR,T) \
int didaq_sched_read_##NAME( didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) DIDAQ_VARREAD_PARAM(VAR) T * val)\
{\
  assert(num_rbytes <=  (VAR ?: 4));\
  DIDAQ_IIF(DIDAQ_IS_ZERO(VAR)) ( const T * pipelined_buf = NULL; , const T * pipelined_buf = dev->pipelined_##NAME[offset]; )\
  size_t pipelined_buf_siz = VAR;\
  return didaq_append_rx(dev, ADDR + offset, sizeof(T), num_rbytes, (char*) val, pipelined_buf, pipelined_buf_siz);\
}\
int didaq_read_##NAME( didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) DIDAQ_VARREAD_PARAM(VAR) T * val)\
{\
  int ret =  didaq_sched_read_##NAME(dev, DIDAQ_IIF(DIDAQ_IS_ONE(NADDR))(,offset,) DIDAQ_IIF(DIDAQ_IS_ZERO(VAR))(,num_rbytes,) val);\
  if (ret) return ret;\
  return didaq_complete(dev);\
}

#define DIDAQ_WRITE_FNS_IMPL(NAME,ADDR,NADDR,RW,VAR,T) \
int didaq_sched_write_##NAME( didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR)  const T * val)\
{\
  uint32_t payload  = 0;\
  memcpy(&payload, val, sizeof(payload));\
  return didaq_append_tx(dev, ADDR + offset, payload);\
}\
int didaq_write_##NAME( didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR)  const T * val)\
{\
  uint32_t payload  = 0;\
  memcpy(&payload, val, sizeof(payload));\
  int ret =  didaq_append_tx(dev, ADDR + offset, payload);\
  if (ret) return ret; \
  return didaq_complete(dev);\
}\


DIDAQ_REGS(DIDAQ_READ_FNS_IMPL)
DIDAQ_REGS(DIDAQ_WRITE_FNS_IMPL)

