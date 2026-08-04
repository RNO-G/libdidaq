#include "didaq.h"
#include "didaq_internal.h"
#include "didaq_regs.h"
#include "didaq_adc.h"
#include "didaq_sdm.h"
#include "didaq_helpers.h"
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <math.h>
#include <errno.h>


#define CHECK(RET)  if (RET) return RET;

didaq_dev_t * didaq_open(const didaq_setup_t * setup)
{
  // spi required
  if (!setup || !setup->spi_device || !*setup->spi_device) return NULL;
  didaq_dev_t * dev = 0;

  FILE * ferr = setup->err_out ?: stderr;

  int spi_fd = open(setup->spi_device, O_RDWR);
  if (spi_fd < 0)
  {
    fprintf(ferr, "Couldn't open %s\n", setup->spi_device);
    return NULL;
  }

  //advisory locks
  int locked_spi = flock(spi_fd, LOCK_EX | LOCK_NB);
  if (locked_spi < 0)
  {
    fprintf(ferr,"Could not get exclusive access to %s\n", setup->spi_device);
    close(spi_fd);
    return NULL;
  }

  gpios_line_t spi_en = {0};
  gpios_line_t trig_gpio = {0};
  if (setup->spi_en_gpio_label)
  {
    int ret = gpios_get_line_by_label(setup->spi_en_gpio_label, &spi_en, GPIOS_OUTPUT | (setup->spi_en_active_high ? 0 : GPIOS_ACTIVE_LOW));
    if (ret)
    {
      fprintf(ferr, "Could't load GPIO with label %s. Probably not gonna work.\n", setup->spi_en_gpio_label);
    }
  }

  if (setup->trig_ready_gpio_label)
  {
    int ret = gpios_get_line_by_label(setup->trig_ready_gpio_label, &trig_gpio, ( setup->trig_ready_active_low ? GPIOS_ACTIVE_LOW : 0)  | GPIOS_POLL_RISING | GPIOS_POLL_CLOCK_REALTIME);
    if (ret)
    {
      fprintf(ferr, "Could't load GPIO with label %s. Will have to poll register.\n", setup->trig_ready_gpio_label);
    }
  }


  int speed = setup->spi_speed ?: 25000000;
  int speed_ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

  if (speed_ret < 0)
  {
      fprintf(ferr,"Trouble setting speed to  %d: (%d, %s)\n", speed, errno, strerror(errno));
  }

  //set up for 8 bits per word for now (to do 16 eventually) and mode 0. These shouldn't fail. Probably.
  uint8_t mode =1 ;
  uint8_t bpw = 8;
  ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
  ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);


  // uart not required
  int uart_fd = -1;
  int locked_uart = 0;
  struct termios tty;

  if(!setup->uart_device || !*setup->uart_device)
  {
    fprintf(stderr, "Opening empty UART device\n");
  }
  else
  {
    uart_fd = open(setup->uart_device, O_RDWR);

    if (uart_fd < 0)
    {
      fprintf(stderr,"Could not open %s\n", setup->uart_device);
      close(uart_fd);
      return 0;
    }

    //advisory locks
    locked_uart = flock(uart_fd, LOCK_EX | LOCK_NB);
    if (locked_uart < 0)
    {
      fprintf(ferr,"Could not get exclusive access to %s\n", setup->uart_device);
      if(uart_fd) close(uart_fd);
      if(spi_fd) close(spi_fd);
      return NULL;
    }


    tcgetattr(uart_fd,  &tty);

    //clear parity bit
    tty.c_cflag &= ~PARENB;

    //one stop bit
    tty.c_cflag &= ~CSTOPB;

    //8 bits
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    //no hw flow control
    tty.c_cflag &=~CRTSCTS;

    //turn on read/disable ctrl lines
    tty.c_cflag |= CREAD | CLOCAL;

    //turn OFF canoncial mode
    tty.c_lflag &= ~ICANON;

    //disable echo bits (probably already disabled?)
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;

    //disable interpretation of signal chars
    tty.c_lflag &= ~ISIG;

    //disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    //disable special handling of input  bytes
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|ICRNL);

    //disable any special output modes
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;


    //make it nonblocking
    tty.c_cc[VTIME]=0;
    tty.c_cc[VMIN]=0;

    //baud rate
    cfsetispeed(&tty, B115200);

    //set the serial attrs
    if (0 != tcsetattr(uart_fd, TCSANOW, &tty))
    {
      fprintf(stderr,"Could not configure serial port %s :(. Got error %d: %s\n", setup->uart_device, errno, strerror(errno));
      if(uart_fd >= 0) close(uart_fd);
      if(spi_fd) close(spi_fd);
      return 0;
    }

    //drain the port
    tcflush(uart_fd, TCIOFLUSH);
  }

  //allocate memory for dev
  dev = calloc(sizeof(didaq_dev_t),1);

  if (!dev)
  {
    if(spi_fd) close(spi_fd);
    if(uart_fd >= 0) close(uart_fd);
    return NULL;
  }

  dev->uart_fd = uart_fd;
  dev->spi_fd = spi_fd;

  // setup adc spi interface if uart opened
  if(dev->uart_fd >= 0)
  {
    didaq_uart_adc_set_spi_cfg(dev, 4);
  }

  memcpy(&dev->setup, setup, sizeof(dev->setup));
  memcpy(&dev->spi_en,  &spi_en, sizeof(spi_en));
  memcpy(&dev->trig_rdy,  &trig_gpio, sizeof(trig_gpio));

  dev->ferr = ferr;
  dev->dbg = setup->dbg;
  dev->poll_usleep_amt = 1000; // TODO make this configurable

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

  // set up pipelined buffers

#define SETUP_PIPELINED_BUFFERS(NAME, ADDR, NADDR, RW, VAR, T) \
  DIDAQ_IIF(DIDAQ_IS_ZERO(VAR))\
  ( /* nothing if zero */, \
  for (size_t offset = 0; offset < NADDR; offset++)\
  {\
    size_t i = VAR - 2; \
    while(i != 2)\
    {\
      i-=4; \
      dev->pipelined_##NAME[offset][i+1] = (ADDR + offset) & 0xff; \
      dev->pipelined_##NAME[offset][i] = 0x80 | ((ADDR + offset)  >> 8); \
    }\
  }\
  )

  DIDAQ_REGS(SETUP_PIPELINED_BUFFERS)


  // enable spi enable
  gpios_set_value(&dev->spi_en, true);
  didaq_sched_read_REVISION(dev, &dev->revision);
  didaq_sched_read_BOARD_ID(dev, &dev->board_id);
  didaq_sched_read_CAPTURE_CTL(dev, &dev->capture_ctl);
  didaq_sched_read_PHASED_CTL(dev, &dev->phased_ctl);
  didaq_sched_read_COIN_CTL(dev,0, &dev->coin_ctl[0]);
  didaq_sched_read_COIN_CTL(dev,1, &dev->coin_ctl[1]);
  didaq_complete(dev);
  dev->selected_adc = -1;
  dev->clock_estimate = 250000000;

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
  if (dev->uart_fd >= 0)
  {
    flock(dev->uart_fd, LOCK_UN);
    close(dev->uart_fd);
  }

  free(dev);

  return ret;
}


int didaq_reset_acq(didaq_dev_t * dev)
{
  //clear capture_ctl bitsfor reset

  dev->capture_ctl.event_clr = 0;
  dev->capture_ctl.run_ctr_rst = 0;
  dev->capture_ctl.sw_trig = 0;

  didaq_reg_capture_ctl_t strobe;
  memcpy(&strobe, &dev->capture_ctl, sizeof(strobe));
  strobe.event_clr =1;
  strobe.run_ctr_rst = 1;

  int ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl); CHECK(ret);
  ret = didaq_sched_write_CAPTURE_CTL(dev, &strobe); CHECK(ret);
  ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl); CHECK(ret);


  return didaq_complete(dev);
}



int didaq_configure_trigger(didaq_dev_t * dev, const didaq_trigger_setup_t * trig)
{
  dev->capture_ctl.ext_en = trig->enable_ext;
  dev->capture_ctl.pps_en = trig->enable_pps;
  int ret = didaq_write_CAPTURE_CTL(dev, &dev->capture_ctl); CHECK(ret);


  dev->phased_ctl.en_trig = trig->phased.enable;
  dev->phased_ctl.en_trig_to_data = trig->phased.enable_readout;
  dev->phased_ctl.req_consec_wins = trig->phased.require_consecutive_windows;
  dev->phased_ctl.divide_by_2 = trig->phased.divide_by_2;
  dev->phased_ctl.channel_mask = ~trig->phased.chan_exclude_mask;
  dev->phased_ctl.beam_mask = ~trig->phased.beam_exclude_mask;

  ret = didaq_write_PHASED_CTL(dev, &dev->phased_ctl); CHECK(ret);

  for (int i = 0; i < DIDAQ_NUM_COINC; i++)
  {
    dev->coin_ctl[i].en_module = trig->coinc[i].enable;
    dev->coin_ctl[i].en_readout = trig->coinc[i].enable_readout;
    dev->coin_ctl[i].quad_mode = trig->coinc[i].quad_mode;
    dev->coin_ctl[i].num_coinc = trig->coinc[i].num_required;
    dev->coin_ctl[i].coin_win = trig->coinc[i].coinc_window;
    dev->coin_ctl[i].include_mask = (~trig->coinc[i].channel_exclude_mask) & 0xfff;
    ret = didaq_write_COIN_CTL(dev, i, &dev->coin_ctl[i]); CHECK(ret);
  }

  return ret;
}


int didaq_force_trigger(didaq_dev_t * dev)
{

  if (!dev) return -ENODEV;
  didaq_reg_capture_ctl_t ctl;
  memcpy(&ctl, &dev->capture_ctl, sizeof(ctl));
  ctl.sw_trig = 1;
  int ret = didaq_sched_write_CAPTURE_CTL(dev, &ctl);
  CHECK(ret);  //this can only fail if complete ran
  dev->capture_ctl.sw_trig = 0;
  ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl);
  CHECK(ret);
  return didaq_complete(dev);
}


int didaq_event_wait(didaq_dev_t * dev, float timeout)
{
  if (!dev) return -ENODEV;
  // use GPIO if we have it

  if (dev->trig_rdy.fd)
  {
    gpios_event_t event = {0};
    int ret =  gpios_wait_val(&dev->trig_rdy, true, &event, timeout); CHECK(ret);

    dev->event_ready = 1;
    if (event.when.tv_sec)
    {
      memcpy(&dev->event_ready_time, &event.when, sizeof(struct timespec));
    }
    else
    {
      clock_gettime(CLOCK_REALTIME, &dev->event_ready_time);
    }

    return 0;
  }

  //otherwise, we'll ahve to keep polling capture_stat

  struct timespec start;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &start);

  while (true)
  {
    didaq_reg_capture_stat_t st = {0};
    if (dev->setup.poll_mutex) pthread_mutex_lock(dev->setup.poll_mutex);
    int ret = didaq_read_CAPTURE_STAT(dev, &st); CHECK(ret);
    if (dev->setup.poll_mutex) pthread_mutex_unlock(dev->setup.poll_mutex);

    if ( st.event_rdy )
    {
      clock_gettime(CLOCK_REALTIME, &dev->event_ready_time);
      dev->event_ready = 1;
      return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (timeout > 0 )
    {
      if (now.tv_sec - start.tv_sec + 1e-9 * (now.tv_nsec - start.tv_nsec) > timeout)
      {
        return -ETIMEDOUT;
      }
    }
    usleep(dev->poll_usleep_amt);
  }
}

int didaq_event_readout(didaq_dev_t * dev, didaq_event_readout_t * rdout)
{
  if (!dev) return -ENODEV;
  if (!rdout) return -EINVAL;
  if (!dev->event_ready) didaq_event_wait(dev, 0);
  memcpy(&rdout->meta.ready_time, &dev->event_ready_time, sizeof(struct timespec));

  //set defaults if nothing provided
  if (!rdout->in.len)
  {
    rdout->in.len = dev->setup.default_len ?: 768;
    rdout->in.start = dev->setup.default_start;
  }

  int ret = 0;
  didaq_reg_pps_counter_t pps_counter = {0};
  didaq_reg_misc0_t misc0 = {0};
  didaq_reg_misc1_t misc1 = {0};
  didaq_reg_meta_trig_t meta_trig =  { 0};

  ret = didaq_sched_read_LAST_EVT_CTR(dev, &rdout->meta.event_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_TRIG_CTR(dev, &rdout->meta.trig_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_DEAD_CTR(dev, &rdout->meta.dead_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_CLK_CTR(dev, &rdout->meta.clk_cycles); CHECK(ret);
  ret = didaq_sched_read_LAST_PPS_CTR(dev, &pps_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_MISC0(dev, &misc0); CHECK(ret);
  ret = didaq_sched_read_LAST_MISC1(dev, &misc1); CHECK(ret);
  ret = didaq_sched_read_LAST_TRIG(dev, &meta_trig); CHECK(ret);

  // NB: the reads above are only scheduled. pps_counter/misc0/misc1/meta_trig stay
  // untouched until didaq_complete() below, so they are copied out after that.

  didaq_reg_rdout_ctl_t rdout_ctl = {.start_rd_addr = rdout->in.start / 4};

  for (int i = 0 ; i < DIDAQ_NUM_CHANNELS ; i++)
  {
    if (!rdout->wfs[i]) continue;
    ret = didaq_sched_write_RDOUT_CTL(dev, &rdout_ctl); CHECK(ret);
    if (dev->setup.pipeline_reads)
    {
      ret = didaq_sched_read_DATA(dev, i, rdout->in.len, &rdout->wfs[i][0]); CHECK(ret);
    }
    else
    {
      for (int j = 0; j < (rdout->in.len & (~0x03)); j+=4)
      {
        ret = didaq_sched_read_DATA(dev, i, 4, &rdout->wfs[i][j]); CHECK(ret);
      }
    }
  }

  //clear the event
  didaq_reg_capture_ctl_t strobe ;
  memcpy(&strobe, &dev->capture_ctl, sizeof(strobe));
  strobe.event_clr = 1;

  ret = didaq_sched_write_CAPTURE_CTL(dev, &strobe); CHECK(ret);
  ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl); CHECK(ret);

  ret = didaq_complete(dev); CHECK(ret);
  clock_gettime(CLOCK_REALTIME, &rdout->meta.readout_time);

  rdout->meta.pps_counter = pps_counter.pps;
  rdout->meta.last_coinc_pattern = misc0.last_coincidence_pattern;
  rdout->meta.last_beam_pattern = misc1.last_beam_pattern;
  rdout->meta.trig_type = meta_trig.trig_type;

  if (dev->dbg)
  {
    // raw is the full 32 bits as received (post byte-swap); decoded is after bitfield
    // extraction. raw == 0 points at the FPGA, raw != 0 with decoded == 0 points here.
    uint32_t raw_pps, raw_misc0, raw_misc1, raw_trig;
    memcpy(&raw_pps, &pps_counter, sizeof(raw_pps));
    memcpy(&raw_misc0, &misc0, sizeof(raw_misc0));
    memcpy(&raw_misc1, &misc1, sizeof(raw_misc1));
    memcpy(&raw_trig, &meta_trig, sizeof(raw_trig));
    fprintf(dev->ferr, " ( META raw: PPS[0x56]=0x%08x MISC0[0x57]=0x%08x MISC1[0x58]=0x%08x TRIG[0x59]=0x%08x )\n",
            raw_pps, raw_misc0, raw_misc1, raw_trig);
    fprintf(dev->ferr, " ( META decoded: pps=%u coin_pat=0x%08x beam_pat=0x%04x trig_type=0x%02x ram_addr=%u )\n",
            rdout->meta.pps_counter, rdout->meta.last_coinc_pattern,
            rdout->meta.last_beam_pattern, rdout->meta.trig_type, meta_trig.ram_addr);
  }

  dev->event_ready = 0;

  return 0;
}

static const didaq_reg_scal_sel_t scal_latch[2] = {  {.latch = 1 }, {.latch = 0}};
static const didaq_reg_scal_sel_t scal_sel[48] =
{
  {.which = 0 }, {.which = 1 }, {.which = 2 }, {.which = 3 }, {.which = 4 }, {.which = 5 },
  {.which = 6 }, {.which = 7 }, {.which = 8 }, {.which = 9 }, {.which = 10 }, {.which = 11 },
  {.which = 12 }, {.which = 13 }, {.which = 14 }, {.which = 15 }, {.which = 16 }, {.which = 17 },
  {.which = 18 }, {.which = 19 }, {.which = 20 }, {.which = 21 }, {.which = 22 }, {.which = 23 },
  {.which = 24 }, {.which = 25 }, {.which = 26 }, {.which = 27 }, {.which = 28 }, {.which = 29 },
  {.which = 30 }, {.which = 31 }, {.which = 32 }, {.which = 33 }, {.which = 34 }, {.which = 35 },
  {.which = 36 }, {.which = 37 }, {.which = 38 }, {.which = 39 }, {.which = 40 }, {.which = 41 },
  {.which = 42 }, {.which = 43 }, {.which = 44 }, {.which = 45 }, {.which = 46 }, {.which = 47 },
};

int didaq_read_scalers(didaq_dev_t *dev, didaq_scalers_t * scal)
{
  didaq_reg_scaler_t raw_scalers[48] = {0};

  int ret = 0;

  ret = didaq_sched_write_SCAL_SEL(dev, &scal_latch[0]); CHECK(ret);
  ret = didaq_sched_write_SCAL_SEL(dev, &scal_latch[1]); CHECK(ret);
  for (int i = 0; i < 48; i++)
  {
    ret = didaq_sched_write_SCAL_SEL(dev, &scal_sel[i]); CHECK(ret);
    ret = didaq_sched_read_SCAL_RD(dev, &raw_scalers[i]); CHECK(ret);
  }

  ret = didaq_complete(dev); CHECK(ret);
  clock_gettime(CLOCK_REALTIME, &scal->readout_time);

  for (int i = 0; i < DIDAQ_NUM_CHANNELS; i++)
  {
    scal->coinc_singles_1Hz[i] = raw_scalers[i/2].scalers[(i)%2];
    scal->coinc_singles_1Hz_gated[i] = raw_scalers[(DIDAQ_NUM_CHANNELS + i)/2].scalers[(i)%2];
  }

  for (int i = 0; i < DIDAQ_NUM_COINC; i++)
  {
    scal->coinc_trig_100mHz[i] = raw_scalers[24].scalers[i];
    scal->coinc_trig_100mHz_gated[i] = raw_scalers[25].scalers[i];
  }

  for (int i = 0; i < DIDAQ_NUM_BEAMS; i++)
  {
    scal->beam_trig_100mHz[i] = raw_scalers[26 + i/2].scalers[(i) % 2];
    scal->beam_trig_100mHz_gated[i] = raw_scalers[31 + i/2].scalers[(i) % 2];
    scal->beam_servo_1Hz[i] = raw_scalers[36 + i/2].scalers[(i) % 2];
  }

  scal->num_pps = raw_scalers[41].scalers[0];
  scal->clk_rate = raw_scalers[47].scalers[1] <<16;
  scal->clk_rate += raw_scalers[47].scalers[0];
  dev->clock_estimate = scal->clk_rate;
  scal->total_beam_100mHz = raw_scalers[42].scalers[0];
  scal->total_beam_100mHz_gated = raw_scalers[42].scalers[1];
  scal->total_beam_1Hz= raw_scalers[43].scalers[0];

  return 0;
}




#define PRINT_ARRAY(F, NAME, LEN, FRMT, PTR, ACCUM)\
  ACCUM +=  fprintf(F, " %s : [", NAME); \
  for ( int ARRAY_I = 0; ARRAY_I < LEN; ARRAY_I ++) ACCUM += fprintf(F, "%s" FRMT, ARRAY_I ? "," : "", PTR[ARRAY_I]);\
  ACCUM +=  fprintf(F, "]\n")

#define PRINT_ARRAY_CSV(F, NAME, LEN, FRMT, PTR, ACCUM)\
  ACCUM +=  fprintf(F, "%s", NAME); \
  for ( int ARRAY_I = 0; ARRAY_I < LEN; ARRAY_I ++) ACCUM += fprintf(F, ","FRMT,  PTR[ARRAY_I]);\
  ACCUM +=  fprintf(F, "\n")



int didaq_dump(didaq_dev_t * dev, FILE * f, int flags)
{
  if (!dev) return -ENODEV;
  (void) flags;
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

  didaq_reg_capture_stat_t capture_stat = {0};
  didaq_reg_capture_ctl_t capture_ctl = {0};

  // should we just use the cached one here? meh. this is an important debug tool.
  if (didaq_sched_read_CAPTURE_CTL(dev,&capture_ctl)) return -1;

  if (didaq_sched_read_CAPTURE_STAT(dev,&capture_stat)) return -1;
  if (didaq_complete(dev)) return -1;

  ret += fprintf(f, "  capture_stat = { .event_busy = %u, .event_rdy = %u }\n", capture_stat.event_bsy, capture_stat.event_rdy);
  ret += fprintf(f, "  capture_ctl = { .sw_trig = %u, .event_clr = %u, .run_ctr_rst = %u, .pps_en = %u, .ext_en = %u }\n", capture_ctl.sw_trig, capture_ctl.event_clr, capture_ctl.run_ctr_rst, capture_ctl.pps_en, capture_ctl.ext_en);
  ret += fprintf(f, "  phased_ctl = { .en_trig = %u, .en_trig_to_data = %u, .req_consec_wins = %u, .divide_by_2 = %u, .channel_mask = 0b%b, .beam_mask = 0b%b  }\n" ,
                        dev->phased_ctl.en_trig, dev->phased_ctl.en_trig_to_data, dev->phased_ctl.req_consec_wins, dev->phased_ctl.divide_by_2, dev->phased_ctl.channel_mask, dev->phased_ctl.beam_mask);
  ret += fprintf(f, "  coin_ctl[2] = {\n "
                    "    { .en_module = %u, .en_readout = %u, .num_coinc = %u, .coinc_win = %u, .include_mask = 0b%b }, \n "
                    "    { .en_module = %u, .en_readout = %u, .num_coinc = %u, .coinc_win = %u, .include_mask = 0b%b }\n  }\n; "
                    , dev->coin_ctl[0].en_module, dev->coin_ctl[0].en_readout, dev->coin_ctl[0].num_coinc, dev->coin_ctl[0].coin_win, dev->coin_ctl[0].include_mask
                    , dev->coin_ctl[1].en_module, dev->coin_ctl[1].en_readout, dev->coin_ctl[1].num_coinc, dev->coin_ctl[1].coin_win, dev->coin_ctl[1].include_mask);

#ifdef DIDAQ_ENABLE_TEMPS
  didaq_core_temps_t temps = {0};
  if (didaq_get_core_temps(dev,&temps)) return -1;
  printf( " Temps: [%f %f %f]\n", temps.T[0], temps.T[1], temps.T[2]);
#endif
  return ret;
}


int didaq_dump_scalers(const didaq_scalers_t * s, FILE * f)
{
  if (!s) return -EINVAL;
  int ret =0;

  ret += fprintf(f,"DIDAQ Scalers @ %ld.%09ld\n", s->readout_time.tv_sec, s->readout_time.tv_nsec);
  ret += fprintf(f," NUM_PPS: %hu, CLK_SPEED: %u Hz\n", s->num_pps, s->clk_rate);
  PRINT_ARRAY(f, "COINC_SINGLE_1HZ", DIDAQ_NUM_CHANNELS, "%05hu", s->coinc_singles_1Hz, ret);
  PRINT_ARRAY(f, "COINC_TRIG_0.1HZ", DIDAQ_NUM_COINC, "%05hu", s->coinc_trig_100mHz, ret);
  PRINT_ARRAY(f, "COINC_GATD_0.1HZ", DIDAQ_NUM_COINC, "%05hu", s->coinc_trig_100mHz_gated, ret);
  PRINT_ARRAY(f, "BEAM_TRIG_0.1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_trig_100mHz, ret);
  PRINT_ARRAY(f, "BEAM_GATD_0.1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_trig_100mHz_gated, ret);
  PRINT_ARRAY(f, "BEAM_SERVOS_1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_servo_1Hz, ret);
  ret += fprintf(f," BEAM_TOTAL_1Hz: %hu, BEAM_TOTAL_0.1Hz: %hu, BEAM_TOTAL_0.1Hz_Gated: %hu\n", s->total_beam_1Hz, s->total_beam_100mHz, s->total_beam_100mHz_gated);

  return ret;
}

int didaq_dump_event_readout(const didaq_event_readout_t *s, FILE *f)
{

  if (!s) return -EINVAL;
  int ret = 0;
  ret += fprintf(f, "DIDAQ EVENT @ %ld.%09ld (readout %ld.%09ld)\n",
      s->meta.ready_time.tv_sec, s->meta.ready_time.tv_nsec, s->meta.readout_time.tv_sec, s->meta.readout_time.tv_nsec);

  ret += fprintf(f, "  EVENT_COUNTER: %u\n", s->meta.event_counter);
  ret += fprintf(f, "  TRIG_COUNTER:  %u\n", s->meta.trig_counter);
  ret += fprintf(f, "  DEAD_COUNTER:  %u\n", s->meta.dead_counter);
  ret += fprintf(f, "  CLK_CYCLES:    %u\n", s->meta.clk_cycles);
  ret += fprintf(f, "  PPS_COUNTER:   %hu\n", s->meta.pps_counter);
  ret += fprintf(f, "  LAST_COIN_PAT: %x\n", s->meta.last_coinc_pattern);
  ret += fprintf(f, "  LAST_BEAM_PAT: %x\n", s->meta.last_beam_pattern);
  ret += fprintf(f, "  TRIG_TYPE:     %hhx\n", s->meta.trig_type);
  ret += fprintf(f, "  START_SAMPLE:  %hu\n", s->in.start);
  ret += fprintf(f, "  NUM_SAMPLES:   %hu\n", s->in.len);

  for (int i = 0 ; i < DIDAQ_NUM_CHANNELS; i++)
  {
    if (s->wfs[i])
    {
      char chname[5];
      sprintf(chname,"CH%02d", i);
      PRINT_ARRAY(f, chname, s->in.len, "%u", s->wfs[i], ret);
    }

  }
  return ret;
}

int didaq_dump_event_readout_csv(const didaq_event_readout_t *s, FILE *f)
{

  if (!s) return -EINVAL;
  if (!f) f = stdout;
  int ret = 0;
  ret += fprintf(f, "DIDAQEVENT\nREADY_TIME, %ld.%09ld\nREADOUT_TIME,%ld.%09ld\n",
      s->meta.ready_time.tv_sec, s->meta.ready_time.tv_nsec, s->meta.readout_time.tv_sec, s->meta.readout_time.tv_nsec);
  ret += fprintf(f, "EVENT_COUNTER, %u\n", s->meta.event_counter);
  ret += fprintf(f, "TRIG_COUNTER, %u\n", s->meta.trig_counter);
  ret += fprintf(f, "DEAD_COUNTER, %u\n", s->meta.dead_counter);
  ret += fprintf(f, "CLK_CYCLES,  %u\n", s->meta.clk_cycles);
  ret += fprintf(f, "PPS_COUNTER, %hu\n", s->meta.pps_counter);
  ret += fprintf(f, "LAST_COIN_PAT, %x\n", s->meta.last_coinc_pattern);
  ret += fprintf(f, "TRIG_TYPE, %hhx\n", s->meta.trig_type);
  ret += fprintf(f, "START_SAMPLE, %hu\n", s->in.start);
  ret += fprintf(f, "NUM_SAMPLES, %hu\n", s->in.len);

  for (int i = 0 ; i < DIDAQ_NUM_CHANNELS; i++)
  {
    if (s->wfs[i])
    {
      char chname[5];
      sprintf(chname,"CH%02d", i);
      PRINT_ARRAY_CSV(f, chname, s->in.len, "%03u", s->wfs[i], ret);
    }

  }
  return ret;
}



int didaq_get_thresholds( didaq_dev_t * dev,
                          didaq_phased_thresholds_t * phased,
                          didaq_coin_thresholds_t * coin,
                          bool force)
{

  int ret = 0;

  if (phased)
  {

    if (force || !dev->cached_phased_init)
    {

      didaq_reg_phas_thresh_t t[DIDAQ_NUM_BEAMS];

      for (int beam = 0; beam < countof(phased->beam_trig_thresholds); beam++)
      {
        ret = didaq_sched_read_BEAM_THRESH(dev, DIDAQ_NUM_BEAMS -1 - beam, &t[beam]);
        CHECK(ret);
      }

      ret = didaq_complete(dev); CHECK(ret);
      for (int beam = 0; beam < countof(phased->beam_trig_thresholds); beam++)
      {
        phased->beam_trig_thresholds[beam] = t[beam].trig;
        phased->beam_servo_thresholds[beam] = t[beam].servo;
      }
    }
    else
    {
      memcpy(phased, &dev->cached_phased_thresholds, sizeof(*phased));
    }
  }

  if (coin)
  {

    if (force || !dev->cached_coin_init)
    {

      didaq_reg_coin_thresh_t t[DIDAQ_NUM_CHANNELS/2];

      for (int chpair = 0; chpair < countof(t); chpair++)
      {
        ret = didaq_sched_read_COIN_THRESH(dev, chpair, &t[chpair]);
        CHECK(ret);
      }

      ret = didaq_complete(dev); CHECK(ret);

      for (int ch = 0; ch < DIDAQ_NUM_CHANNELS; ch+=2)
      {
        coin->coin_thresholds[ch] = t[ch/2].thresh0;
        coin->coin_thresholds[ch+1] = t[ch/2].thresh1;
      }
    }
    else
    {
      memcpy(coin, &dev->cached_coin_thresholds, sizeof(*coin));
    }
  }

  return 0;
}

int didaq_set_thresholds( didaq_dev_t * dev,
                          const didaq_phased_thresholds_t * phased,
                          const didaq_coin_thresholds_t * coin)
{
  if (!dev) return -ENODEV;
  if (!phased && !coin) return 0;

  int ret = 0;

  if (phased)
  {
    memcpy(&dev->cached_phased_thresholds, phased, sizeof(*phased));
    dev->cached_phased_init = true;
    for (int beam = 0; beam < countof(phased->beam_trig_thresholds); beam++)
    {
      ret = didaq_sched_write_BEAM_THRESH(dev, DIDAQ_NUM_BEAMS -1 -beam, // seem to be backwards?
          & (didaq_reg_phas_thresh_t) {
           .trig = phased->beam_trig_thresholds[beam],
           .servo = phased->beam_servo_thresholds[beam]
           });
      CHECK(ret);
    }
  }

  if (coin)
  {
    memcpy(&dev->cached_coin_thresholds, coin, sizeof(*coin));
    dev->cached_coin_init = true;
    for (int chan = 0; chan < countof(coin->coin_thresholds); chan+=2)
    {
       ret = didaq_sched_write_COIN_THRESH(dev, chan / 2,
          & ( didaq_reg_coin_thresh_t) {
            .thresh0 = coin->coin_thresholds[chan],
            .thresh1 = coin->coin_thresholds[chan+1]
          });
      CHECK(ret);
    }
  }

  return didaq_complete(dev);
}


uint32_t didaq_get_clock_rate_estimate(didaq_dev_t * d)
{
  return d->clock_estimate;
}

int didaq_set_fs_gain_codes(didaq_dev_t * dev, uint8_t adc_mask, uint16_t gain_codes[DIDAQ_NUM_ADC])
{
  // set FS RANGE regs
  // DIDAQ_ADC_REG_FS_RANGE maps to the high byte
  // DIDAQ_ADC_REG_FS_RANGE+1 maps to the low byte

  if(dev->uart_fd < 0) return -1;

  int ret = 0;
  for(int adc=0; adc<DIDAQ_NUM_ADC; adc++)
  {
    if (!(adc_mask & (1<<adc))) continue;

    ret = didaq_uart_adc_reg_write(dev, adc, DIDAQ_ADC_REG_FS_RANGE, (gain_codes[adc]&0xff00)>>8); CHECK(ret);
    ret = didaq_uart_adc_reg_write(dev, adc, DIDAQ_ADC_REG_FS_RANGE+1, (gain_codes[adc]&0xff)); CHECK(ret);

  }
  return 0;

}

static double didaq_getrms(int N, uint8_t* X)
{
  double sum = 0;
  double sum2 = 0;
  for (int i = 0; i < N ; i++)
  {
    sum+=X[i];
    sum2+=X[i]*X[i];
  }

  double mean = sum/N;
  return sqrt(sum2/N - mean*mean);
}

int didaq_auto_gain(didaq_dev_t * dev, uint8_t adc_set_mask, float target_rms, float * final_rms, uint16_t * gain_codes_out)
{
  if(dev->uart_fd < 0) return -1;

  // Sets full-scale range setting on each ADC
  // The gain for each ADC core on the ADCs doesn't seem to appreciably change the RMS

  float ch_rms[DIDAQ_NUM_CHANNELS] = {0.};
  float adc_min_rms[DIDAQ_NUM_ADC] = {0.};
  float adc_avg_rms[DIDAQ_NUM_ADC] = {0.};
  uint16_t gain_codes[DIDAQ_NUM_ADC] = {0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff}; //assume default
  uint8_t adc_mask = 0;
  uint8_t adc_done = ~adc_set_mask;
  int gain_step = 10; // maybe move as an arg

  // Setup readout
  int ret = didaq_reset_acq(dev); CHECK(ret);

  static uint8_t wfs[DIDAQ_NUM_CHANNELS][1024];

  didaq_event_readout_t rdout = { .in  = {.len = 1024, .start = 0}, .wfs = 
    { 
      wfs[0], wfs[1], wfs[2], wfs[3], wfs[4], wfs[5],
      wfs[6], wfs[7], wfs[8], wfs[9], wfs[10], wfs[11],
      wfs[12], wfs[13], wfs[14], wfs[15], wfs[16], wfs[17],
      wfs[18], wfs[19], wfs[20], wfs[21], wfs[22], wfs[23]
    }
  };

  // gain equalize adcs using min (or avg) rms per adc. avg just in case a broken channel?
  while((adc_done&0x3f)!=0x3f)
  {

    ret = didaq_set_fs_gain_codes(dev, 0x3f & adc_set_mask, gain_codes); CHECK(ret);
    ret = didaq_usleep(500); CHECK(ret); // some time for adcs to settle and old data to flush

    ret = didaq_force_trigger(dev); CHECK(ret);
    ret = didaq_event_readout(dev, &rdout); CHECK(ret);

    // get min/avg rms values per adc
    for(int ch=0; ch<DIDAQ_NUM_CHANNELS; ch++)
    {
      ch_rms[ch] = didaq_getrms(rdout.in.len, rdout.wfs[ch]);

      adc_avg_rms[ch/4] += ch_rms[ch];
      if(ch_rms[ch] < adc_min_rms[ch/4]) adc_min_rms[ch/4] = ch_rms[ch];
    }
    
    printf("Set mask: %x, ADC mask: %x, Done mask: %x\n", adc_set_mask, adc_mask, adc_done);
    printf("Gain codes and RMS\n");
 
    for(int adc=0; adc<DIDAQ_NUM_ADC; adc++)
    {
      adc_avg_rms[adc] = adc_avg_rms[adc]/6;
      printf("ADC %d: gain code %d, RMS %.3f\n", adc, gain_codes[adc], adc_avg_rms[adc]);

      if((adc_set_mask & (1 << adc) != (1<< adc)) || ((adc_done & (1 << adc)) == (1 << adc))) 
      {
        // not in set mask, set done and ignore
        adc_done |= (1<<adc);
        adc_mask &= ~(1<<adc);
      }
      else if(adc_avg_rms[adc]<target_rms)
      {
        // rms less than goal, adjust step
        if ((gain_codes[adc]-gain_step) < 0x2000)
        {
          // pushes below minimum recommended setting, stop adjusting
          adc_mask &= ~(1<<adc);
          adc_done |= (1<<adc);
        }
        else
        {
          adc_mask |= (1<<adc);
          adc_mask &= adc_set_mask;
          gain_codes[adc] -= gain_step; // TODO: tune step parameter
        }
      }
      else
      {
        adc_mask |= (1<<adc);
        adc_done |= 1<<adc;
      }

      if (final_rms && (adc_done & (1<<adc)))
      {
        final_rms[adc*4] = ch_rms[adc*4];
        final_rms[adc*4+1] = ch_rms[adc*4+1];
        final_rms[adc*4+2] = ch_rms[adc*4+2];
        final_rms[adc*4+3] = ch_rms[adc*4+3];
      }
    }
  }

  if (gain_codes_out) memcpy(gain_codes_out, gain_codes,sizeof(gain_codes));

  return 0;
}


int didaq_get_core_temps(didaq_dev_t * dev,  didaq_core_temps_t * temps)
{
  int ret = didaq_sdm_write(dev, DIDAQ_SDM_COMMAND_ADDR,
                          (didaq_sdm_data_t) { .bytes =  { 0x02, 0x00, 0x10, 0x19} });
  CHECK(ret);


  ret = didaq_sdm_write(dev, DIDAQ_SDM_COMMAND_LAST_WORD_ADDR,
                          (didaq_sdm_data_t) { .bytes =  { 0x00, 0x01, 0x00, 0x3c} });
  CHECK(ret);

  didaq_sdm_data_t result[5];

  ret = didaq_sdm_read_values(dev, 5, result);
  CHECK(ret);

  for (int i = 1; i < 4; i++)
  {
    uint32_t val = result[i].bytes[3] |  (result[i].bytes[2] << 8) | (result[i].bytes[1] << 16);
    temps->T[i-1] = val / 256.;
  }
  clock_gettime(CLOCK_REALTIME, &temps->when);

  return 0;
}
