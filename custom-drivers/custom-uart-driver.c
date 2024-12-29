#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include <linux/delay.h> // TODO: Remove
#include <linux/fs.h>
#include <linux/cdev.h>

#include "custom-driver-shared-info.h"
#include "custom-errno.h"
#include "custom-gpio-driver.h"
#include "custom-uart-driver.h"

// TODO: Look at possibly getting rid of some of the mutex usage and replacing it with simple locks or atomic accesses (especially in cases)
//       where basically I am just modifying a register.


/***************    Macros    ***************/

// Peripheral addresses
#define UART_BASE                 (BCM2837_PERI_BASE + 0x201000)
#define UART_SIZE                 (0x90)               // Uart peripheral memory area in bytes

#define UART_CLK_RATE             (48000000)  // It is 48 MHz by default
#define UART_OVERSAMPLE_RATE      (16)        // Uart oversamples by 16
#define UART_MAX_BAUD_RATE        (UART_CLK_RATE / UART_OVERSAMPLE_RATE)

#define UART_IRQ_CHANNEL          (114)  // By entering the command "cat /proc/interrupts" and looking at the 
                                        // fourth column of the results, we can find the number that is the interrupt channel
                                        // that has been assigned in the OS for the uart peripheral

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
static inline void uart_write_byte(uint8_t data_byte);

// Static functions
static int __init uart_driver_init(void);
static void __exit uart_driver_exit(void);
static int uart_set_baud_rate(uint32_t baud_rate);
static int uart_set_data_bit_size(uart_data_bit_size_t data_bit_size);
static int uart_set_parity(uart_parity_t parity);
static int uart_set_stop_bits(uart_stop_bits_t stop_bits);
static void uart_enable_fifos(bool do_enable);
static void uart_enable(bool do_enable);
static irqreturn_t uart_interrupt_handler(int irq_num, void *dev_id);
static int uart_dev_uevent(struct device *dev, struct kobj_uevent_env *env);
static inline void unregister_uart_cdev_region(void);


/***************    Private variables    ***************/

uart_t *uart = NULL;
static DEFINE_MUTEX(uart_mutex);
// On a normal Linux system I would use DEFINE_SEMAPHORE(name, n)
// but unfortunately the support to give an initial value in the macro wasn't added unti
// 2023. So we have to do it the long way.
static struct semaphore uart_write_data_sem;
static struct semaphore uart_read_data_sem;
static bool is_irq_installed = false;

struct cdev c_dev;
struct device * p_device;
static int major_drv_num = 0;
static int first_minor_drv_num = 0;
static struct class *p_uart_class = NULL; 

static struct file_operations const uart_fops =
{
  // .read = led_read,
  // .write = led_write,
  // .open = led_open,
  // .release = led_release
};

/***************    Function Definitions    ***************/

static int __init uart_driver_init(void)
{
  dev_t dev_id = 0;
  int error = ENONE;

  error = alloc_chrdev_region(&dev_id, 0, 1, "CUSTOM_UART");
  
  if (ENONE != error)
  {
    pr_err("UART driver couldn't allocate device ids for all the necessary devices.\n");
    goto failure_end;
  }

  major_drv_num = MAJOR(dev_id);
  first_minor_drv_num = MINOR(dev_id);

  // Create the device class before the cdev so that led_dev_init can create
  // the actual device for each led when it is called.
  p_uart_class = class_create(THIS_MODULE, "custom_uart_class");

  if (IS_ERR(p_uart_class))
  {
    error = PTR_ERR(p_uart_class);
    pr_err("Failed to create class for UART! error: %d\n", error);
    goto unregister_uart_cdev_region;
  }

  // Now assign the custom dev_uevent function that will run when a new device
  // is created. We use this to set device permissions at creation.
  p_uart_class->dev_uevent = uart_dev_uevent;

  cdev_init(&(c_dev), &uart_fops);
  c_dev.owner = THIS_MODULE;
  
  // Try to add the character device
  error = cdev_add(&(c_dev), dev_id, 1);

  if (ENONE != error)
  {
    goto delete_uart_class;
  }

  // Try to create the actual led device
  p_device = device_create(p_uart_class, NULL, c_dev.dev, NULL, "uart_device_name");

  if (IS_ERR(p_device))
  {
    error = PTR_ERR(p_device);
    pr_err("Creating actual UART device failed! error: %d\n", error);

    // Delete this device's cdev that was added
    cdev_del(&(c_dev));
    
    goto delete_uart_class;
  }



  // Attempt to map the UART peripheral
  uart = (uart_t *)(ioremap(UART_BASE, UART_SIZE));  // Note, a page size always has to be allocated, so even if it is under a page, it still takes up a page of memory.

  // For some reason the mapping failed
  if (NULL == uart)
  {
    // Exit immediately
    pr_err("UART driver couldn't map the io space!\n");
    // return -EMAPPING;
    error = -EMAPPING;
    goto delete_uart_cdevs_and_devices;
  }
  else
  {
    printk("UART successfully mapped\n");
  }

  mutex_init(&uart_mutex);
  sema_init(&uart_write_data_sem, 2);
  sema_init(&uart_read_data_sem, 2);

  printk("UART driver successfully initialized\n");

  if (ENONE == request_irq(UART_IRQ_CHANNEL, uart_interrupt_handler, IRQF_SHARED, "custom_uart", p_device))
  {
    is_irq_installed = true;
  }
  else
  {
    pr_err("Requesting irq failed");
    // free_irq (UART_IRQ_CHANNEL, NULL);
    // //
    // if (ENONE == request_irq(UART_IRQ_CHANNEL, uart_interrupt_handler, IRQF_SHARED, "custom_uart", p_device))
    // {
    //   is_irq_installed = true;
    // }
    // //
    // pr_err("Requesting irq failed AGAIN");
  }

  // TODO: Remove this, just using this setting for testing
  uart_init(9600, UART_DATA_8_BITS, UART_NO_PARITY, UART_STOP_BITS_1);
  gpio_set_pin_to_uart(14);
  gpio_set_pin_to_uart(15);
  uart->icr |= ICR_TXIC_FIELD | ICR_RXIC_FIELD;

  uart->imsc |= IMSC_TXIM_FIELD;
  uart_enable(true);

  printk("Masked register value is %u. Mask setting is %u. Raw values are %u\n", uart->mis, uart->imsc, uart->ris);

  uint32_t j = 0;

  // for (int i = 0; i < 100; i++)
  // {
    
  //   while (0 != (uart->fr & FR_TXFF_FIELD))
  //   {
  //     j++;
      
  //     if (0 != (uart->fr & FR_TXFE_FIELD))
  //     {
  //       printk("Fifo finally empty after %d times\n", j);
  //       break;
  //     }
  //   }

  //   uart_write_byte('C');

  //   // msleep(1);
  // }
  // while (0 != (uart->fr & FR_TXFF_FIELD))
  // {
  //   j++;
  // }
  // uart_write_byte('\0');

  msleep(1000);

  uart_write_byte('A');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  uart_write_byte('T');
  // uart_write_byte('+');
  // uart_write_byte('I');
  // uart_write_byte('M');
  // uart_write_byte('M');
  // uart_write_byte('E');
  // uart_write_byte('?');
  // uart_write_byte('\0');
  
  return ENONE;


delete_uart_cdevs_and_devices:

  // We should never have a null pointer for p_device
  // if the device was successfully inited, but we
  // will double-check just to be sure.
  if (NULL != p_device)
  {
    device_destroy(p_uart_class, c_dev.dev);
  }

  cdev_del(&(c_dev));

delete_uart_class:
  class_destroy(p_uart_class);

unregister_uart_cdev_region:
  unregister_uart_cdev_region();

failure_end:
  pr_err("UART failed initialization!\n");

  return error;
}

static void __exit uart_driver_exit(void)
{
  // If the gpio was successfully mapped
  if (NULL != uart)
  {
    if (is_irq_installed)
    {
      free_irq (UART_IRQ_CHANNEL, p_device);
    }
    // // Reset the pwm channels to inital values before unmapping
    uart_enable(false);
    uart_enable_fifos(false);

    // Release the UART mapping
    printk("Released UART mapping\n");
    iounmap(uart);
        
    printk("Destroyed device with device id: %d\n", c_dev.dev);
    device_destroy(p_uart_class, c_dev.dev);
    cdev_del(&(c_dev));
    class_destroy(p_uart_class);
    unregister_uart_cdev_region();
  }
  
  // TODO: Need to add checks on these destroy calls
  mutex_destroy(&uart_mutex);
  // It looks like semaphores are destroyed once they go out of scope
  // so no destroy function exists in the kernel

  printk("UART driver exited\n");
}

int uart_init(uint32_t baud_rate, uart_data_bit_size_t data_bit_size, uart_parity_t parity, uart_stop_bits_t stop_bits)
{
  int error = ENONE;

  // Reset the LCRH register to initial values
  mutex_lock(&uart_mutex);

  uart->lcrh = 0;

  mutex_unlock(&uart_mutex);

  error = uart_set_baud_rate(baud_rate);
  if (ENONE != error)
  {
    return error;
  }

  error = uart_set_data_bit_size(data_bit_size);
  if (ENONE != error)
  {
    return error;
  }

  error = uart_set_parity(parity);
  if (ENONE != error)
  {
    return error;
  }

  error = uart_set_stop_bits(stop_bits);
  if (ENONE != error)
  {
    return error;
  }

  uart_enable_fifos(true);

  return ENONE;
}

static int uart_set_baud_rate(uint32_t baud_rate)
{
  if (unlikely(UART_MAX_BAUD_RATE < baud_rate))
  {
    pr_err("Requested uart baud rate of %u is greater than the uart maximum baud rate of %u!\n", baud_rate, UART_MAX_BAUD_RATE);
    return -EINVCONFIG;
  }
  else if (unlikely(0 == baud_rate))
  {
    pr_err("Requested uart baud rate cannot be 0!\n");
    return -EINVCONFIG;
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

  uint64_t precision_calculation_val = div_u64(mul_u32_u32(UART_CLK_RATE, PRECISION_BOOST), (UART_OVERSAMPLE_RATE * baud_rate));  // E.g. ((48,000,000 * 1,000,000) / (16 * 115,200)) 
                                                                                                                                  //      = 26,041,666 
                                                                                                                          
  baud_rate_divsor_integer = div_u64_rem(precision_calculation_val, PRECISION_BOOST, &decimal_portion);   // 26,041,666 / 1,000,000 = 26
                                                                                                          // 26,041,666 % 1,000,000 = 41,666

  baud_rate_divsor_fractional = (((decimal_portion * 64) + ROUNDING_HELPER) / PRECISION_BOOST);   //    ((41,666 * 64) + 500,000) / 1,000,000)
                                                                                                  // =  (2,666,624 + 500,000) / 1,000,000)  (we add the 500,000 to account for rounding when dividing by 1,000,000)
                                                                                                  // =  (3,166,624 / 1,000,000)
                                                                                                  // =  (3)
                                                                                                  // Therefore our fractional component will be 3 (i.e. 3/64)

  // In the baud rate example of 115200 this would give us a baud rate of (48,000,000 / (16 * (26 + (3 / 64 )))).
  // This is a calculated baud rate of 115176.965.
  // This gives us an error of ((115176 - 115200) / 115200)
  // i.e. -2.08333e^-4 error
  // i.e. -0.02% error

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

static int uart_set_data_bit_size(uart_data_bit_size_t data_bit_size)
{
  int error = ENONE;

  uint32_t wlen_val = 0;

  switch (data_bit_size)
  {
    case UART_DATA_5_BITS:
      wlen_val = LCRH_WLEN_5_BIT;
      break;
    case UART_DATA_6_BITS:
      wlen_val = LCRH_WLEN_6_BIT;
      break;
    case UART_DATA_7_BITS:
      wlen_val = LCRH_WLEN_7_BIT;
      break;
    case UART_DATA_8_BITS:
      wlen_val = LCRH_WLEN_8_BIT;
      break;
    default:
      error = -EINVCONFIG;
      break;
  }

  if (ENONE != error)
  {
    pr_err("UART data bits size %d not valid!\n", data_bit_size);
    return error;
  }

  // Lock the register while we read it and then write to it.
  mutex_lock(&uart_mutex);
  
  uart->lcrh = ((uart->lcrh & ~(LCRH_WLEN_FIELD)) | wlen_val);

  mutex_unlock(&uart_mutex);

  return error;
}

static int uart_set_parity(uart_parity_t parity)
{
  int error = ENONE;

  // Lock the register while we read it and then write to it.
  mutex_lock(&uart_mutex);

  switch (parity)
  {
    case UART_EVEN_PARITY:
      uart->lcrh |= (LCRH_EPS_FIELD | LCRH_PEN_FIELD);
      break;
    case UART_ODD_PARITY:
      uart->lcrh = ((uart->lcrh & ~(LCRH_EPS_FIELD)) | LCRH_PEN_FIELD);
      break;
    case UART_NO_PARITY:
      uart->lcrh &= ~(LCRH_PEN_FIELD);
      break;
    default:
      pr_err("UART parity %d not valid!\n", parity);
      error = -EINVCONFIG;
      break;
  }

  mutex_unlock(&uart_mutex);

  return error;
}

static int uart_set_stop_bits(uart_stop_bits_t stop_bits)
{
  int error = ENONE;

  // Lock the register while we read it and then write to it.
  mutex_lock(&uart_mutex);

  switch (stop_bits)
  {
    case UART_STOP_BITS_1:
      uart->lcrh &= ~(LCRH_STP2_FIELD);
      break;
    case UART_STOP_BITS_2:
      uart->lcrh |= LCRH_STP2_FIELD;
      break;
    default:
      pr_err("UART stop bits %d not valid!\n", stop_bits);
      error = -EINVCONFIG;
      break;
  }

  mutex_unlock(&uart_mutex);

  return error;
}

static void uart_enable_fifos(bool do_enable)
{

  // Lock the register while we read it and then write to it.
  mutex_lock(&uart_mutex);
  
  if (do_enable)
  {
    uart->lcrh |= LCRH_FEN_FIELD;
  }
  else
  {
    uart->lcrh &= ~(LCRH_FEN_FIELD);
  }

  mutex_unlock(&uart_mutex);
}

static void uart_enable(bool do_enable)
{

  // Lock the register while we read it and then write to it.
  mutex_lock(&uart_mutex);
  
  if (do_enable)
  {
    uart->cr |= CR_UARTEN_FIELD;
  }
  else
  {
    uart->cr &= ~(CR_UARTEN_FIELD);
  }

  mutex_unlock(&uart_mutex);
}

static inline void uart_write_byte(uint8_t data_byte)
{

  // atomic_set(data_byte, &(uart->dr));
  uart->dr = data_byte;
}

static irqreturn_t uart_interrupt_handler(int irq_num, void *dev_id)
{
  printk("Hey, we saw a uart interrupt. Masked register value is %u\n", uart->mis);
  uart->icr |= ICR_TXIC_FIELD | ICR_RXIC_FIELD;
  return IRQ_HANDLED;
}

static int uart_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
  // Look at linux/drivers/base/core.c for an example of add_uevent_var
  add_uevent_var(env, "DEVMODE=%#o", 0666);
  return ENONE;
}

static inline void unregister_uart_cdev_region(void)
{
  unregister_chrdev_region(MKDEV(major_drv_num, first_minor_drv_num), 1);
}

module_init(uart_driver_init);
module_exit(uart_driver_exit);

EXPORT_SYMBOL(uart_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver for UART communications.");
MODULE_VERSION("1.0");