#ifndef _DIDAQ_REGS_H
#define _DIDAQ_REGS_H

/** Low-level access to DIDAQ registers. Most users should use didaq.h
 *
 * Unless you enjoy preprocessor soup?
 *
 * Functions are generated from the below X-macro.
 *
 */
#include <stdint.h>
#include <unistd.h>
#include "didaq.h"



/**
 * Completes all scheduled transactions  that were scheduled using the sched form of register functions.
 * @param dev the didaq device
 * */
int didaq_complete(didaq_dev_t * dev);

/** X-Macro to define DIDAQ Registers  and access functions
 *
 *  name:  name of the register
 *  baseaddr: base address, only relevant for naddr > 1
 *  naddr: number of similar adderresses (for when there is an offset)
 *  rw:  RW register (0 is readonly)
 *  varlen: if non-zero, may be read multiple times in a row up to varlen times.
 *          this will prepopulate a buffer
 *  type:  the type we read to. the sizeof this type is relevant for endianess.
 *
 * */
#define DIDAQ_REGS(REG)\
      /** name      , baseaddr , naddr,   rw, varlen,  type */\
  REG(REVISION      , 0x0000,    1,     0,   0,    uint32_t)\
  REG(BOARD_ID      , 0x0001,    1,     0,   0,    uint32_t)\
  REG(LED_SWITCH    , 0x0002,    1,     1,   0,    uint32_t)\
  REG(IRQ_STAT      , 0x0003,    1,     0,   0,    uint32_t)\
  REG(IRQ_EN        , 0x0004,    1,     1,   0,    uint32_t)\
  REG(WATCHDOG      , 0x0005,    1,     0,   0,    uint32_t)\
  REG(SYSACC_ADDR   , 0x0006,    1,     1,   0,    uint32_t)\
  REG(SYSACC_WRDA   , 0x0007,    1,     1,   0,    uint32_t)\
  REG(SYSACC_RDDA   , 0x0008,    1,     0,   0,    uint32_t)\
  REG(SYSACC_CTL    , 0x0009,    1,     0,   0,    uint32_t)\
  REG(ADC_PD_CTL    , 0x000c,    1,     1,   0,    didaq_reg_pd_ctl_t )\
  REG(ADC_SPI_SEL   , 0x000d,    1,     1,   0,    didaq_reg_spi_sel_t )\
  REG(CAPTURE_CTL   , 0x000e,    1,     1,   0,    didaq_reg_capture_ctl_t )\
  REG(CAPTURE_STAT  , 0x000f,    1,     0,   0,    didaq_reg_capture_stat_t )\
  REG(DATA          , 0x0014,    24,    0,   4096, uint8_t )\
  REG(COINCTL_0_11  , 0x0038,    1,     1,   0,    didaq_reg_coin_trig_ctl_t )\
  REG(COINCTL_12_23 , 0x0039,    1,     1,   0,    didaq_reg_coin_trig_ctl_t )\
  REG(COIN_THRESH   , 0x003a,    12,    1,   0,    didaq_reg_coin_thresh_t )\
  REG(PHASED_CTL    , 0x0046,    1,     1,   0,    didaq_reg_phas_trig_ctl_t )\
  REG(BEAM_THRESH   , 0x0047,    10,    1,   0,    didaq_reg_phas_thresh_t)\
  REG(LAST_EVT_CTR  , 0x0052,    1,     0,   0,    uint32_t)\
  REG(LAST_TRIG_CTR , 0x0053,    1,     0,   0,    uint32_t)\
  REG(LAST_DEAD_CTR , 0x0054,    1,     0,   0,    uint32_t)\
  REG(LAST_CLK_CTR  , 0x0055,    1,     0,   0,    uint32_t)\
  REG(LAST_PPS_CTR  , 0x0056,    1,     0,   0,    uint16_t)\
  REG(LAST_MISC0    , 0x0057,    1,     0,   0,    uint32_t)\
  REG(LAST_TRIG     , 0x0059,    1,     0,   0,    didaq_reg_meta_trig_t)\
  REG(RDOUT_CTL     , 0x005B,    1,     1,   0,    didaq_reg_rdout_ctl_t)\
  REG(SCAL_RD       , 0x005C,    1,     0,   0,    didaq_reg_scaler_t)\
  REG(SCAL_SEL      , 0x005D,    1,     1,   0,    didaq_reg_scal_sel_t)\
  REG(CORE_TEMPS    , 0x0060,    5,     0,   0,    uint32_t)



#define DIDAQ_DEFINE_ENUM(NAME, ADDR, NADDR, RW, VAR, T) DIDAQ_##NAME,
/** Enum of all didaq registers */

enum didaq_regs
{
  DIDAQ_REGS(DIDAQ_DEFINE_ENUM)
};


typedef struct
{
  uint32_t adc_power : 6;
} didaq_reg_pd_ctl_t;

typedef struct
{
  uint16_t  ram_addr: 10;
  uint8_t   trig_type;
}didaq_reg_meta_trig_t;

typedef struct
{
  uint32_t spi_sel : 3;
} didaq_reg_spi_sel_t;

typedef struct
{
  uint16_t start_rd_addr: 10;
} didaq_reg_rdout_ctl_t;


typedef struct
{
  uint8_t sw_trig            : 1;
  uint8_t __sw_trig_pad      : 7;
  uint8_t event_clr          : 1;
  uint8_t __event_clr_pad    : 7;
  uint8_t run_ctr_rst        : 1;
  uint8_t ___run_ctr_rst_pad : 7;
  uint8_t pps_en             : 1;
  uint8_t ext_en             : 1;
} didaq_reg_capture_ctl_t;


typedef struct
{
  uint32_t event_bsy : 1;
  uint32_t event_rdy : 1;
} didaq_reg_capture_stat_t;

typedef struct
{
  uint32_t en_module   : 1;
  uint32_t en_readout  : 1;
  uint32_t num_coinc   : 3;
  uint32_t __pad0      : 3;
  uint32_t coin_win    : 4;
  uint32_t __pad1      : 4;
  uint32_t include_mask: 12;
} didaq_reg_coin_trig_ctl_t;


typedef struct
{
  uint32_t en_trig         : 1;
  uint32_t en_trig_to_data : 1;
  uint32_t __pad0          : 2;
  uint32_t req_consec_wins : 1;
  uint32_t __pad1          : 3;
  uint32_t divide_by_2     : 1;
  uint32_t __pad2          : 3;
  uint32_t channel_mask    : 4;
  uint32_t beam_mask       : 12;
} didaq_reg_phas_trig_ctl_t;

typedef struct
{
  uint8_t thresh0;
  uint8_t __pad0; 
  uint8_t thresh1;
  uint8_t __pad1; 
} didaq_reg_coin_thresh_t;

typedef struct
{
  uint16_t trig;
  uint16_t servo;
} didaq_reg_phas_thresh_t;


typedef struct
{
  uint16_t scalers[2];
} didaq_reg_scaler_t;


typedef struct
{
  uint32_t which : 8;
  uint32_t __pad : 8;
  uint32_t latch : 1;
} didaq_reg_scal_sel_t;




/* Tiny preprocessor detection toolkit to detect special values without enumerating everything. */
#define DIDAQ_CAT(a, b)         DIDAQ_CAT_(a, b)
#define DIDAQ_CAT_(a, b)        a ## b
#define DIDAQ_PROBE(x)          x, 1,                         /* x is always placeholder */
#define DIDAQ_CHECK(...)        DIDAQ_CHECK_(__VA_ARGS__, 0,) /* 1 if a probe fired, else 0 */
#define DIDAQ_CHECK_(x, n, ...) n
#define DIDAQ_IIF(c)            DIDAQ_CAT(DIDAQ_IIF_, c)      /* IIF(1)(t,f)=t  IIF(0)(t,f)=f */
#define DIDAQ_IIF_1(t, ...)     t
#define DIDAQ_IIF_0(t, ...)     __VA_ARGS__

/* varlen: 0 is special (fixed length -> no num_rbytes arg), anything else gets it */
#define DIDAQ_IS_ZERO(X)       DIDAQ_CHECK(DIDAQ_CAT(DIDAQ_IS_ZERO_, X))
#define DIDAQ_IS_ZERO_0        DIDAQ_PROBE(~)
#define DIDAQ_VARREAD_PARAM(X)  DIDAQ_IIF(DIDAQ_IS_ZERO(X))(/*fixed*/, /*var*/ size_t num_rbytes,)
#define DIDAQ_DOC_VARREAD_PARAM(X)  DIDAQ_IIF(DIDAQ_IS_ZERO(X))("" , "size_t num_rbytes, ")

/* naddr: 1 is special (single address -> no offset arg), anything else gets it */
#define DIDAQ_IS_ONE(X)      DIDAQ_CHECK(DIDAQ_CAT(DIDAQ_IS_ONE_, X))
#define DIDAQ_IS_ONE_1       DIDAQ_PROBE(~)
#define DIDAQ_NUM_ADDR_PARAM(X) DIDAQ_IIF(DIDAQ_IS_ONE(X))(/*scalar*/, /*multi*/ size_t offset,)
#define DIDAQ_DOC_NUM_ADDR_PARAM(X) DIDAQ_IIF(DIDAQ_IS_ONE(X))("", "size_t offset, ")



// generates read functions
#define DIDAQ_READ_FNS(NAME,ADDR,NADDR,RW,VAR,T) \
  int didaq_read_##NAME(didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) DIDAQ_VARREAD_PARAM(VAR) T * val);\
  int didaq_sched_read_##NAME(didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) DIDAQ_VARREAD_PARAM(VAR) T * val);


// generates API documentation for read functions (just as a string)
#define DIDAQ_DOC_READ_FNS(NAME,ADDR,NADDR,RW,VAR,T) \
  "/** Read " #NAME " register, synchronously*/"\
  "int didaq_read_" #NAME "(didaq_dev_t * dev, " DIDAQ_DOC_NUM_ADDR_PARAM(NADDR) DIDAQ_DOC_VARREAD_PARAM(VAR) " T * val);\n"\
  " /** Schedule a read of the  " #NAME " register. May happen immediately if spidev buffer is full. */"\
  "int didaq_sched_read_" #NAME "(didaq_dev_t * dev, " DIDAQ_DOC_NUM_ADDR_PARAM(NADDR) DIDAQ_DOC_VARREAD_PARAM(VAR) " T * val);\n\n";



DIDAQ_REGS(DIDAQ_READ_FNS)

//generates write functions for registers not marked read only
#define DIDAQ_WRITE_FNS(NAME, ADDR, NADDR, RW, VAR, T) \
    DIDAQ_IIF(RW) (\
    int didaq_write_##NAME(didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) const T * val);\
    int didaq_sched_write_##NAME(didaq_dev_t * dev, DIDAQ_NUM_ADDR_PARAM(NADDR) const T * val);\
    , /* no write for you */)

//and the API docs
#define DIDAQ_DOC_WRITE_FNS(NAME, ADDR, NADDR, RW, VAR, T) \
    DIDAQ_IIF(RW) (\
    "/** Writes to " #NAME " immediately */\n"\
    "int didaq_write_" #NAME "(didaq_dev_t * dev, " DIDAQ_DOC_NUM_ADDR_PARAM(NADDR) " const T * val);\n"\
    "/** Writes to " #NAME " eventually */\n"\
    "int didaq_sched_write_" #NAME "(didaq_dev_t * dev, " DIDAQ_DOC_NUM_ADDR_PARAM(NADDR) " const T * val);\n\n"\
    , /* no write for you */)


DIDAQ_REGS(DIDAQ_WRITE_FNS)



#endif
