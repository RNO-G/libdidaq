#include "didaq.h"
#include "didaq_internal.h"
#include "didaq_regs.h"
#include "didaq_helpers.h"
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include <errno.h>


#define CHECK(RET)  if (RET) return RET;

didaq_dev_t * didaq_open(const didaq_setup_t * setup)
{
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

  if ( setup->trig_ready_gpio_label)
  {
    int ret = gpios_get_line_by_label(setup->trig_ready_gpio_label, &trig_gpio, ( setup->trig_ready_active_low ? GPIOS_ACTIVE_LOW : 0)  | GPIOS_POLL_RISING | GPIOS_POLL_CLOCK_REALTIME);
    if (ret)
    {
      fprintf(ferr, "Could't load GPIO with label %s. Will have to poll register.\n", setup->trig_ready_gpio_label);
    }
  }


  if (setup->spi_speed)
  {
    int speed_ret = ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &setup->spi_speed);

    if (speed_ret < 0)
    {
      fprintf(ferr,"Trouble setting speed to  %d: (%d, %s)\n", setup->spi_speed, errno, strerror(errno));
    }
  }

  //set up for 8 bits per word for now (to do 16 eventually) and mode 0. These shouldn't fail. Probably.
  uint8_t mode =1 ;
  uint8_t bpw = 8;
  ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
  ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bpw);


  //allocate memory for dev
  dev = calloc(sizeof(didaq_dev_t),1);
  dev->spi_fd = spi_fd;
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

  // enable spi enable
  gpios_set_value(&dev->spi_en, true);
  didaq_sched_read_REVISION(dev, &dev->revision);
  didaq_sched_read_BOARD_ID(dev, &dev->board_id);
  didaq_sched_read_CAPTURE_CTL(dev, &dev->capture_ctl);
  didaq_complete(dev);

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


int didaq_reset_acq(didaq_dev_t * dev)
{
  //clear capture_ctl bitsfor reset

  dev->capture_ctl.event_clr = 0;
  dev->capture_ctl.run_ctr_rst = 0;

  didaq_reg_capture_ctl_t strobe ; 
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
  (void) trig;
  dev->capture_ctl.ext_en = trig->enable_ext;
  dev->capture_ctl.pps_en = trig->enable_pps;

  return didaq_write_CAPTURE_CTL(dev, &dev->capture_ctl);
}


int didaq_force_trigger(didaq_dev_t * dev)
{

  didaq_reg_capture_ctl_t ctl;
  memcpy(&ctl, &dev->capture_ctl, sizeof(ctl));
  ctl.sw_trig = 1;
  int ret = didaq_sched_write_CAPTURE_CTL(dev, &ctl);
  if (!ret) return ret;
  //write the version with sw_trig cleared
  dev->capture_ctl.sw_trig = 0;
  ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl);
  if (!ret) return ret;
  return didaq_complete(dev);
}


int didaq_event_wait(didaq_dev_t * dev, float timeout)
{
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
    int ret = didaq_read_CAPTURE_STAT(dev, &st); CHECK(ret);

    if ( st.event_rdy ) 
    {
      clock_gettime(CLOCK_REALTIME, &dev->event_ready_time);
      dev->event_ready = 1;
      return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (now.tv_sec - start.tv_sec + 1e-9 * (now.tv_nsec - start.tv_nsec) > timeout)
    {
      return -ETIMEDOUT;
    }
    usleep(dev->poll_usleep_amt);
  }
}

int didaq_event_readout(didaq_dev_t * dev, didaq_event_readout_t * rdout)
{
  if (!rdout) return -EINVAL;
  if (!dev->event_ready) didaq_event_wait(dev, 0);
  memcpy(&rdout->meta.ready_time, &dev->event_ready_time, sizeof(struct timespec));

  //set defaults if nothing provided
  if (!rdout->in.len) rdout->in.len = 1024;
  if (!rdout->in.start) rdout->in.start = 1536;

  int ret = 0;
  ret = didaq_sched_read_LAST_EVT_CTR(dev, &rdout->meta.event_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_TRIG_CTR(dev, &rdout->meta.trig_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_DEAD_CTR(dev, &rdout->meta.dead_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_CLK_CTR(dev, &rdout->meta.clk_cycles); CHECK(ret);
  ret = didaq_sched_read_LAST_PPS_CTR(dev, &rdout->meta.pps_counter); CHECK(ret);
  ret = didaq_sched_read_LAST_MISC0(dev, &rdout->meta.last_coinc_pattern); CHECK(ret);
  didaq_reg_meta_trig_t meta_trig =  { 0};
  ret = didaq_sched_read_LAST_TRIG(dev, &meta_trig); CHECK(ret);


  didaq_reg_rdout_ctl_t rdout_ctl = {.start_rd_addr = rdout->in.start / 4};

  for (int i = 0 ; i < DIDAQ_NUM_CHANNELS ; i++)
  {
    if (!rdout->wfs[i]) continue;
    ret = didaq_sched_write_RDOUT_CTL(dev, &rdout_ctl); CHECK(ret);
    ret = didaq_sched_read_DATA(dev, i, rdout->in.len & (~0x3), rdout->wfs[i]); CHECK(ret);
  }

  //clear the event
  didaq_reg_capture_ctl_t strobe ; 
  memcpy(&strobe, &dev->capture_ctl, sizeof(strobe));
  strobe.event_clr =1;

  ret = didaq_sched_write_CAPTURE_CTL(dev, &strobe); CHECK(ret);
  ret = didaq_sched_write_CAPTURE_CTL(dev, &dev->capture_ctl); CHECK(ret);

  ret = didaq_complete(dev); CHECK(ret);
  clock_gettime(CLOCK_REALTIME, &rdout->meta.readout_time);
  rdout->meta.trig_type = meta_trig.trig_type;
  dev->event_ready =0;

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
    scal->coinc_singles_1Hz[i] = raw_scalers[i/2].scalers[i%2];
    scal->coinc_singles_1Hz_gated[i] = raw_scalers[(DIDAQ_NUM_CHANNELS + i)/2].scalers[i%2];
  }

  for (int i = 0; i < DIDAQ_NUM_COINC; i++)
  {
    scal->coinc_trig_100mHz[i] = raw_scalers[24].scalers[i];
    scal->coinc_trig_100mHz_gated[i] = raw_scalers[25].scalers[i];
  }

  for (int i = 0; i < DIDAQ_NUM_BEAMS; i++)
  {
    scal->beam_trig_100mHz[i] = raw_scalers[26 + i/2].scalers[i % 2];
    scal->beam_trig_100mHz_gated[i] = raw_scalers[31 + i/2].scalers[i % 2];
    scal->beam_servo_1Hz[i] = raw_scalers[36 + i/2].scalers[i % 2];
  }

  scal->num_pps = raw_scalers[41].scalers[0];
  scal->clk_rate = raw_scalers[47].scalers[0];
  scal->clk_rate += (raw_scalers[47].scalers[1] << 16);

  return 0;
}




#define PRINT_ARRAY(F, NAME, LEN, FRMT, PTR, ACCUM)\
  ACCUM +=  fprintf(F, " %s : [", NAME); \
  for ( int ARRAY_I = 0; ARRAY_I < LEN; ARRAY_I ++) ACCUM += fprintf(F, "%s" FRMT, ARRAY_I ? "," : "", PTR[ARRAY_I]);\
  ACCUM +=  fprintf(F, "]\n")


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


int didaq_dump_scalers(const didaq_scalers_t * s, FILE * f)
{
  int ret =0;

  ret += fprintf(f,"DIDAQ Scalers @ %ld.%09ld\n", s->readout_time.tv_sec, s->readout_time.tv_nsec);
  ret += fprintf(f," NUM_PPS: %hu, CLK_SPEED: %u Hz\n", s->num_pps, s->clk_rate);
  PRINT_ARRAY(f, "COINC_SINGLE_1HZ", DIDAQ_NUM_CHANNELS, "%05hu", s->coinc_singles_1Hz, ret);
  PRINT_ARRAY(f, "COINC_TRIG_0.1HZ", DIDAQ_NUM_COINC, "%05hu", s->coinc_trig_100mHz, ret);
  PRINT_ARRAY(f, "COINC_GATD_0.1HZ", DIDAQ_NUM_COINC, "%05hu", s->coinc_trig_100mHz_gated, ret);
  PRINT_ARRAY(f, "BEAM_TRIG_0.1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_trig_100mHz, ret);
  PRINT_ARRAY(f, "BEAM_GATD_0.1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_trig_100mHz_gated, ret);
  PRINT_ARRAY(f, "BEAM_SERVOS_1HZ", DIDAQ_NUM_BEAMS, "%05hu", s->beam_servo_1Hz, ret);

  return ret;
}

int didaq_dump_event_readout(const didaq_event_readout_t *s, FILE *f)
{

  int ret = 0;
  ret += fprintf(f, "DIDAQ EVENT @ %ld.%09ld (readout %ld.%09ld)\n", 
      s->meta.ready_time.tv_sec, s->meta.ready_time.tv_nsec, s->meta.readout_time.tv_sec, s->meta.readout_time.tv_nsec);

  ret += fprintf(f, "  EVENT_COUNTER: %u\n", s->meta.event_counter);
  ret += fprintf(f, "  TRIG_COUNTER:  %u\n", s->meta.trig_counter);
  ret += fprintf(f, "  DEAD_COUNTER:  %u\n", s->meta.dead_counter);
  ret += fprintf(f, "  CLK_CYCLES:    %u\n", s->meta.clk_cycles);
  ret += fprintf(f, "  PPS_COUNTER:   %hu\n", s->meta.pps_counter);
  ret += fprintf(f, "  LAST_COIN_PAT: %x\n", s->meta.last_coinc_pattern);
  ret += fprintf(f, "  TRIG_TYPE:     %hhx\n", s->meta.trig_type);
  ret += fprintf(f, "  START_SAMPLE:  %hu\n", s->in.start);
  ret += fprintf(f, "  NUM_SAMPLES:   %hu\n", s->in.len);

  for (int i = 0 ; i < DIDAQ_NUM_CHANNELS; i++)
  {
    if (s->wfs[i])
    {
      char chname[5];
      sprintf(chname,"CH%02d", i);
      PRINT_ARRAY(f, chname, s->in.len, "%03u", s->wfs[i], ret);
    }

  }
  return ret;
}


