#include "didaq.h"

#include "didaq_regs.h"
#include "didaq_internal.h"
#include "didaq_helpers.h"
#include <sys/ioctl.h>


#include <errno.h>



didaq_dev_t * didaq_open( const char * spi_device,
    const char * spi_en_gpio_label,  const char * trig_ready_gpio_label,
    uint32_t spi_speed)
{
  didaq_dev_t * dev = 0;

  int spi_fd = open(spi_device, O_RDWR);
  if (spi_fd < 0)
  {
    fprintf(stderr, "Couldn't open %s\n", spi_device);
    return NULL;
  }

  //advisory locks
  int locked_spi = flock(spi_fd, LOCK_EX | LOCK_NB);
  if (locked_spi < 0)
  {
    fprintf(stderr,"Could not get exclusive access to %s\n", spi_device);
    close(spi_fd);
    return NULL;
  }

  gpios_line_t spi_en = {0};
  gpios_line_t trig_gpio = {0};
  if (spi_en_gpio_label)
  {
    int ret = gpios_get_line_by_label(spi_en_gpio_label, &spi_en, GPIOS_OUTPUT | GPIOS_ACTIVE_LOW);
    if (ret)
    {
      fprintf(stderr, "Could't load GPIO with label %s. Probably not gonna work.\n", spi_en_gpio_label);
    }
  }

  if ( trig_ready_gpio_label)
  {
    int ret = gpios_get_line_by_label(trig_ready_gpio_label, &trig_gpio, GPIOS_POLL_RISING | GPIOS_POLL_CLOCK_REALTIME);
    if (ret)
    {
      fprintf(stderr, "Could't load GPIO with label %s. Will have to poll register.\n", trig_ready_gpio_label);
    }
  }


  int speed_ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);

  if (speed_ret < 0)
  {
    fprintf(stderr,"Trouble setting speed to  %d: (%d, %s)\n", spi_speed, errno, strerror(errno));
  }

  //set up for 16 bits per word and mode 0. These shouldn't fail. Probably.
  uint8_t mode =0 ;
  uint8_t bpw = 16;
  ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
  ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);


  //allocate memory for dev
  dev = calloc(sizeof(didaq_dev_t),1);
  dev->spi_fd = spi_fd;
  memcpy(&dev->spi_en,  &spi_en, sizeof(spi_en));
  memcpy(&dev->trig_rdy,  &trig_gpio, sizeof(trig_gpio));

  dev->ferr = stderr;

  dev->spi_max_bufsiz = 4096; //this is the default

  // see if spi_max_bufsiz is bigger, if we can read it
  FILE* fbufsiz= fopen("/sys/module/spidev/parameters/bufsiz", "r");
  if (fbufsiz)
  {
    uint32_t max_buf_siz = 0;
    fscanf(fbufsiz, "%u",&max_buf_siz);
    if (max_buf_siz > 0) dev->spi_max_bufsiz = max_buf_siz;
    fclose(fbufsiz);
  }

  // enable spi enable
  gpios_set_value(&dev->spi_en, true);

  return dev;
}

int didaq_close (didaq_dev_t * dev)
{
  if (!dev) return 0;

  int ret = didaq_complete(dev);

  if (dev->spi_en.fd)
    gpios_release(&dev->spi_en);

  if (dev->trig_rdy.fd)
    gpios_release(&dev->trig_rdy);

  if (dev->spi_fd)
  {
    flock(dev->spi_fd, LOCK_UN);
    close(dev->spi_fd);
  }

  free(dev);

  return ret;
}


int didaq_dump(didaq_dev_t * dev, FILE * f)
{
  int ret = 0;
  ret += fprintf(f, "[[DIDAQ at 0x%p]]\n", dev);
  ret += fprintf(f, "  Revision: 0x%x\n", dev->revision);
  ret += fprintf(f, "  Board_ID: 0x%x\n", dev->board_id);
  ret += fprintf(f, "  nxfers queued: %lu\n", dev->nxfers);
  ret += fprintf(f, "  nxfers completed:%lu\n", dev->nxfers_complete);
  ret += fprintf(f, "  bufsiz queued: %lu\n", dev->spi_bufsiz);
  ret += fprintf(f, "  bufsiz completed: %lu\n", dev->spi_bufsiz_complete);
  ret += fprintf(f, "  max_bufsiz: %lu\n", dev->spi_max_bufsiz);
  ret += fprintf(f, "  spi_en_gpio_present? %d,  trig_rdy_gpio_present? %d\n", !!dev->spi_en.fd, !!dev->trig_rdy.fd);
  return ret;
}
