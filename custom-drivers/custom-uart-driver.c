#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <asm/io.h>

#include "custom-driver-shared-info.h"
#include "custom-errno.h"


/***************    Macros    ***************/

// Peripheral addresses
#define UART_BASE                 (BCM2837_PERI_BASE + 0x201000)
#define UART_SIZE                 (0x90)               // Uart peripheral memory area in bytes

#define UART_CLK_RATE             (19200000)  // It is 19.2 MHz by default
#define UART_OVERSAMPLE_RATE      (16)        // Uart oversamples by 16
#define UART_MAX_BAUD_RATE             (UART_CLK_RATE / UART_OVERSAMPLE_RATE)


// UART DR Fields
#define DR_DATA_FIELD             (0xFFU)
#define DR_FE_FIELD               (0x01U << 8)
#define DR_PE_FIELD               (0x01U << 9)
#define DR_BE_FIELD               (0x01U << 10)
#define DR_OE_FIELD               (0x01U << 11)

// UART RSRECR Fields
#define RSRECR_FE_FIELD           (0x01U)
#define RSRECR_PE_FIELD           (0x01U << 1)
#define RSRECR_BE_FIELD           (0x01U << 2)
#define RSRECR_OE_FIELD           (0x01U << 3)

// UART FR Fields
#define FR_CTS_FIELD              (0x01U)
#define FR_DSR_FIELD              (0x01U << 1)
#define FR_DCD_FIELD              (0x01U << 2)
#define FR_BUSY_FIELD             (0x01U << 3)
#define FR_RXFE_FIELD             (0x01U << 4)
#define FR_TXFF_FIELD             (0x01U << 5)
#define FR_RXFF_FIELD             (0x01U << 6)
#define FR_TXFE_FIELD             (0x01U << 7)
#define FR_RI_FIELD               (0x01U << 8)

// UART IBRD Fields
#define IBRD_FIELD                (0xFFFFU)

// UART FBRD Fields
#define FBRD_FIELD                (0x3FU)

// UART LCRH Fields
#define LCRH_BRK_FIELD            (0x01U)
#define LCRH_PEN_FIELD            (0x01U << 1)
#define LCRH_EPS_FIELD            (0x01U << 2)
#define LCRH_STP2_FIELD           (0x01U << 3)
#define LCRH_FEN_FIELD            (0x01U << 4)
#define LCRH_WLEN_FIELD           (0x03U << 5)
#define LCRH_SPS_FIELD            (0x01U << 7)

#define LCRH_WLEN_5_BIT           (0x00U << 5)
#define LCRH_WLEN_6_BIT           (0x01U << 5)
#define LCRH_WLEN_7_BIT           (0x02U << 5) 
#define LCRH_WLEN_8_BIT           (0x03U << 5)

// UART CR Fields
#define CR_UARTEN_FIELD           (0x01U)
#define CR_SIREN_FIELD            (0x01U << 1)
#define CR_SIRLP_FIELD            (0x01U << 2)
#define CR_LBE_FIELD              (0x01U << 7)
#define CR_TXE_FIELD              (0x01U << 8)
#define CR_RXE_FIELD              (0x01U << 9)
#define CR_DTR_FIELD              (0x01U << 10)
#define CR_RTS_FIELD              (0x01U << 11)
#define CR_OUT1_FIELD             (0x01U << 12)
#define CR_OUT2_FIELD             (0x01U << 13)
#define CR_RTSEN_FIELD            (0x01U << 14)
#define CR_CTSEN_FIELD            (0x01U << 15)

// UART IFLS Fields
#define IFLS_TXIFLSEL_FIELD       (0x07U)
#define IFLS_RXIFLSEL_FIELD       (0x07U << 3)

#define IFLS_FIFO_1_8             (0x00U)
#define IFLS_FIFO_1_4             (0x01U)
#define IFLS_FIFO_1_2             (0x02U)
#define IFLS_FIFO_3_4             (0x03U)
#define IFLS_FIFO_7_8             (0x04U)

#define IFLS_RXIFLSEL_BIT_OFFSET  (3)

// UART IMSC Fields
#define IMSC_RIMIM_FIELD          (0x01U)
#define IMSC_CTSMIM_FIELD         (0x01U << 1)
#define IMSC_DCDMIM_FIELD         (0x01U << 2)
#define IMSC_DSRMIM_FIELD         (0x01U << 3)
#define IMSC_RXIM_FIELD           (0x01U << 4)
#define IMSC_TXIM_FIELD           (0x01U << 5)
#define IMSC_RTIM_FIELD           (0x01U << 6)
#define IMSC_FEIM_FIELD           (0x01U << 7)
#define IMSC_PEIM_FIELD           (0x01U << 8)
#define IMSC_BEIM_FIELD           (0x01U << 9)
#define IMSC_OEIM_FIELD           (0x01U << 10)

// UART RIS Fields
#define RIS_RIRMIS_FIELD          (0x01U)
#define RIS_CTSRMIS_FIELD         (0x01U << 1)
#define RIS_DCDRMIS_FIELD         (0x01U << 2)
#define RIS_DSRRMIS_FIELD         (0x01U << 3)
#define RIS_RXRIS_FIELD           (0x01U << 4)
#define RIS_TXRIS_FIELD           (0x01U << 5)
#define RIS_RTRIS_FIELD           (0x01U << 6)
#define RIS_FERIS_FIELD           (0x01U << 7)
#define RIS_PERIS_FIELD           (0x01U << 8)
#define RIS_BERIS_FIELD           (0x01U << 9)
#define RIS_OERIS_FIELD           (0x01U << 10)

// UART MIS Fields
#define MIS_RIMMIS_FIELD          (0x01U)
#define MIS_CTSMMIS_FIELD         (0x01U << 1)
#define MIS_DCDMMIS_FIELD         (0x01U << 2)
#define MIS_DSRMMIS_FIELD         (0x01U << 3)
#define MIS_RXMIS_FIELD           (0x01U << 4)
#define MIS_TXMIS_FIELD           (0x01U << 5)
#define MIS_RTMIS_FIELD           (0x01U << 6)
#define MIS_FEMIS_FIELD           (0x01U << 7)
#define MIS_PEMIS_FIELD           (0x01U << 8)
#define MIS_BEMIS_FIELD           (0x01U << 9)
#define MIS_OEMIS_FIELD           (0x01U << 10)

// UART ICR Fields
#define ICR_RIMIC_FIELD           (0x01U)
#define ICR_CTSMIC_FIELD          (0x01U << 1)
#define ICR_DCDMIC_FIELD          (0x01U << 2)
#define ICR_DSRMIC_FIELD          (0x01U << 3)
#define ICR_RXIC_FIELD            (0x01U << 4)
#define ICR_TXIC_FIELD            (0x01U << 5)
#define ICR_RTIC_FIELD            (0x01U << 6)
#define ICR_FEIC_FIELD            (0x01U << 7)
#define ICR_PEIC_FIELD            (0x01U << 8)
#define ICR_BEIC_FIELD            (0x01U << 9)
#define ICR_OEIC_FIELD            (0x01U << 10)

// UART ITCR Fields
#define ITCR_ITCR0_FIELD          (0x01U)
#define ITCR_ITCR1_FIELD          (0x01U << 1)

// UART ITIP Fields
#define ITIP_ITIP0_FIELD          (0x01U)
#define ITIP_ITIP3_FIELD          (0x01U << 3)

// UART ITOP Fields
#define ITOP_ITIP0_FIELD          (0x01U)
#define ITOP_ITIP3_FIELD          (0x01U << 3)
#define ITOP_ITIP6_FIELD          (0x01U << 6)
#define ITOP_ITOP7_FIELD          (0x01U << 7)
#define ITOP_ITOP8_FIELD          (0x01U << 8)
#define ITOP_ITOP9_FIELD          (0x01U << 9)
#define ITOP_ITOP10_FIELD         (0x01U << 10)
#define ITOP_ITOP11_FIELD         (0x01U << 11)

// UART TDR Fields
#define TDR_TDR10_0_FIELD         (0x7FFU)


/***************    Type definitions    ***************/

// Datasheet calls the channels 0 and 1 but puts 1 and 2 as the register names.
// I stuck with 1 and 2 since it makes the doc easier to search.
typedef struct uart_s
{
  uint32_t volatile dr;
  uint32_t volatile rsrecr;
  uint32_t volatile reserved_block_0[4];
  uint32_t volatile fr;
  uint32_t volatile reserved_0;
  uint32_t volatile ilpr;
  uint32_t volatile ibrd;
  uint32_t volatile fbrd;
  uint32_t volatile lcrh;
  uint32_t volatile cr;
  uint32_t volatile ifls;
  uint32_t volatile imsc;
  uint32_t volatile ris;
  uint32_t volatile mis;
  uint32_t volatile icr;
  uint32_t volatile dmacr;
  uint32_t volatile reserved_block_1[13];
  uint32_t volatile itcr;
  uint32_t volatile itip;
  uint32_t volatile itop;
  uint32_t volatile tdr;
} uart_t;


/***************    Function declarations    ***************/

// Inline functions


// Static functions
static int __init uart_driver_init(void);
static void __exit uart_driver_exit(void);
static int uart_set_baud_rate(uint32_t baud_rate);


/***************    Private variables    ***************/

uart_t *uart = NULL;
static DEFINE_MUTEX(uart_mutex);

/***************    Function Definitions    ***************/

static int __init uart_driver_init(void)
{
  // Attempt to map the PWM peripheral
  uart = (uart_t *)(ioremap(UART_BASE, UART_SIZE));  // Note, a page size always has to be allocated, so even if it is under a page, it still takes up a page of memory.

  // For some reason the mapping failed
  if (NULL == uart)
  {
    // Exit immediately
    pr_err("UART driver couldn't map the io space!\n");
    return -EMAPPING;
  }
  else
  {
    printk("UART successfully mapped\n");
  }

  mutex_init(&uart_mutex);

  printk("UART driver successfully initialized\n");

  uart_set_baud_rate(9600);

  return ENONE;
}

static void __exit uart_driver_exit(void)
{
  // If the gpio was successfully mapped
  if (NULL != uart)
  {
    // // Reset the pwm channels to inital values before unmapping
    // pwm_reset_pwm_channels();

    // Release the UART mapping
    printk("Released UART mapping\n");
    iounmap(uart);
  }
  
  mutex_destroy(&uart_mutex);

  printk("UART driver exited\n");
}

static int uart_set_baud_rate(uint32_t baud_rate)
{
  if (unlikely(UART_MAX_BAUD_RATE < baud_rate))
  {
    pr_err("Requested uart baud rate of %u is greater than the uart maximum baud rate of %u!", baud_rate, UART_MAX_BAUD_RATE);
    return -EINVFUNC;
  }
  else if (unlikely(0 == baud_rate))
  {
    pr_err("Requested uart baud rate cannot be 0!");
    return -EINVFUNC;
  }

  

  uint32_t baud_rate_divsor_integer = 1;
  uint32_t baud_rate_divsor_fractional = 0;   // The fractional part is out of 64. So baud_rate_divsor_fractional / 64.

  // We can't use floats in the kernel, so to do our calculations we will use uint64_t.
  // Since the highest the clock is by default is 19.2 MHz we can multiply everything up by 100000 to get 6 decimal places of precision.
  // This is plenty considering that the fractional part is out of 64 (so precision of 1/64 = 0.015625). 
  // Therefore, by default of the baud rate fractional divisor, the maximum we will ever be off for our baud rate is up to 1.56% 
  // (when the integer part is 1 and if the next fractional part was just under the threshold for the next numerator value and we truncated it 
  // (i.e. we did 1/64 instead of 2/64 for decimal of 0.031245))
  // We try to round so hopefully that error should be reduced to a maximum of 0.78%.
  // Of course this all assuming a perfect clock, but even so, a reasonable estimate I have seen for tolerance
  // of baud rate between 2 uarts is 5% total, so hopefully we have enough wiggle room.
  // Of course this error rate should be very small if we use standard baud rates with the default uart clock rate of 19.2 MHz.
  
  // I will use the example of passing in a baud rate of 115200

  uint32_t const PRECISION_BOOST = 1000000;
  uint32_t const ROUNDING_HELPER = PRECISION_BOOST / 2;
  uint32_t decimal_portion = 0;

  uint64_t precision_calculation_val = div_u64(mul_u32_u32(UART_CLK_RATE, PRECISION_BOOST), (UART_OVERSAMPLE_RATE * baud_rate));  // E.g. ((19,200,000 * 1,000,000) / (16 * 115,200)) 
                                                                                                                                  //      = 10,416,666 
                                                                                                                          
  baud_rate_divsor_integer = div_u64_rem(precision_calculation_val, PRECISION_BOOST, &decimal_portion);   // 10,416,666 / 1,000,000 = 10
                                                                                                          // 10,416,666 % 1,000,000 = 416,666

  baud_rate_divsor_fractional = (((decimal_portion * 64) + ROUNDING_HELPER) / PRECISION_BOOST);   //    ((416,666 * 64) + 500,000) / 1,000,000)
                                                                                                  // =  (26,666,624 + 500,000) / 1,000,000)  (we add the 500,000 to account for rounding when dividing by 1,000,000)
                                                                                                  // =  (27,166,624 / 1,000,000)
                                                                                                  // =  (27)
                                                                                                  // Therefore our fractional component will be 27 (i.e. 27/64)

  // In the baud rate example of 115200 this would give us a baud rate of (19,200,000 / (16 * (10 + (27 / 64 )))).
  // This is a calculated baud rate of 115142.429.
  // This gives us an error of ((115142 - 115200) / 115200)
  // i.e. -5.03472e^-4 error
  // i.e. -0.05% error

  // If the fractional is 64 (or greater but greater should never happen) because
  // we rounded up, then set the fractional part to zero and increase the integer part
  // by 1.
  if (64 <= baud_rate_divsor_fractional)
  {
    baud_rate_divsor_integer++;
    baud_rate_divsor_fractional = 0;
  }


  // Assign the proper registers with correct values

  mutex_lock(&uart_mutex);

  uart->ibrd = (baud_rate_divsor_integer & IBRD_FIELD);
  uart->fbrd = (baud_rate_divsor_fractional & FBRD_FIELD);

  mutex_unlock(&uart_mutex);
 
  printk("UART baud rate set to %u\n", baud_rate);
                                                                      
  return ENONE;
}

module_init(uart_driver_init);
module_exit(uart_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver for UART communications.");
MODULE_VERSION("1.0");