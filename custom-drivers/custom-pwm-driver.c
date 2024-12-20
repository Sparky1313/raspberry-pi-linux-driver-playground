#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <asm/io.h>

#include "custom-driver-shared-info.h"
#include "custom-pwm-driver.h"
#include "custom-errno.h"


// NOTE: The PWM doesn't have the best documentation. Therefore, I had to do a lot of searching of forums to find decent documentation. Even then
//       the documentation isn't great, but you can slowly piece it together.
//       That said much information came from the Raspberry Pi forums itself and elinux.org (links the forums suggested.)
//       The most helpful links are:
//          - https://elinux.org/The_Undocumented_Pi (this helps understand the clock tree which isn't documented and tells us that 19.2 MHz is what the PWM peripheral runs at).
//          - https://elinux.org/BCM2835_registers#CM (this shows different registers of the BCM2835 that you might not find in the original peripheral documentation)
//
//       What you can gather from these 2 resources is that peripheral clocks run at 19.2 MHz (for Raspberry Pi 3) and that the clock registers for the PWM peripheral
//       function similarly to the GPIO clock registers. Again, there really isn't any good documentation available for the clocks on the Raspberry Pi and it honestly
//       is very annoying.
//
//       For now I just use the default 19.2 MHz clock rate for the PWM peripheral and adapt it to what I need using the PWM range registers.
//       I won't mess with the clock divisors right now since I don't have time to verify that the undocumented registers work like I hope they do.
//
//       To get the ultimate pwm cycle rate you can use the calculation: pwm_cycle_rate = (clock_rate / clock_divisor / pwm_range_val).
//       Since I just use the default for now and will only be dealing with percentages with no fractions I will use the formula: 4000 Hz = (19.2 MHz / 1 / (19200 / 4))
//       or more simply 4000 Hz = (19.2 MHz / 4800)
//
//       I picked 4 kHz since the research I did showed that above 1 kHz most people can't perceive flickering LEDs while moving. (I've read that 300 Hz for 
//       stationary LEDs works well for preventing the perception of flickering for most people as well, but if moving, people can identify flickering more from the peripherals
//       of their eyes, so 1000 Hz is recommended.) I also saw that some people were still bothered up to 1500 Hz, so I bumped it up to 4 kHz since supposedly that is still low
//       enough that the LED will still function well with PWM control.  


/***************    Macros    ***************/

// Peripheral addresses
#define PWM_BASE              (BCM2837_PERI_BASE + 0x20C000)
#define PWM_SIZE              (0x28)               // PWM peripheral memory area in bytes

#define PWM_CLK_RATE          (19200000)  // It is 19.2 MHz by default

// PWM CTL Fields
// These fields are to be used with the pwm_ctl_field_t as a sort of enum. We don't use
// an actual enum because C doesn't support specifying enum integer type like C++ does.
// Datasheet calls the channels 0 and 1 but puts 1 and 2 as the register names.
// I stuck with 1 and 2 since it makes the doc easier to search.
#define PWEN_1_FIELD          (1U)
#define MODE_1_FIELD          (1U << 1)
#define RPTL_1_FIELD          (1U << 2)
#define SBIT_1_FIELD          (1U << 3)
#define POLA_1_FIELD          (1U << 4)
#define USEF_1_FIELD          (1U << 5)
#define CLRF_1_FIELD          (1U << 6)
#define MSEN_1_FIELD          (1U << 7)
#define PWEN_2_FIELD          (1U << 8)
#define MODE_2_FIELD          (1U << 9)
#define RPTL_2_FIELD          (1U << 10)
#define SBIT_2_FIELD          (1U << 11)
#define POLA_2_FIELD          (1U << 12)
#define USEF_2_FIELD          (1U << 13)
#define RESERVED_FIELD        (1U << 14)
#define MSEN_2_FIELD          (1U << 15)


/***************    Type definitions    ***************/


typedef uint32_t pwm_ctl_field_t;

// Datasheet calls the channels 0 and 1 but puts 1 and 2 as the register names.
// I stuck with 1 and 2 since it makes the doc easier to search.
typedef struct pwm_perph_s
{
  uint32_t volatile ctl;
  uint32_t volatile sta;
  uint32_t volatile dmac;
  uint32_t volatile rng_1;
  uint32_t volatile dat_1;
  uint32_t volatile fif_1;
  uint32_t volatile rng_2;
  uint32_t volatile dat_2;
} pwm_perph_t;


/***************    Function declarations    ***************/

// Inline functions
static inline bool validate_cycle_freq(pwm_cycle_freq_t cycle_freq);
static inline bool validate_pwm_channel(pwm_channel_t pwm_channel);
static inline uint32_t calc_pwm_range_val_from_cycle_freq(pwm_cycle_freq_t cycle_freq);
static inline uint32_t calc_pwm_data_val_from_percent(int percent, uint32_t pwm_range_val);
static inline void pwm_reset_pwm_channels(void);
static inline int pwm_get_channel_range_val(pwm_channel_t pwm_channel, uint32_t *range_val);
static inline void pwm_set_channel_data_val(pwm_channel_t pwm_channel, uint32_t data_val);

// Static functions
static int __init pwm_driver_init(void);
static void __exit pwm_driver_exit(void);
static int pwm_init_pwm_channel(pwm_channel_t pwm_channel, uint32_t initial_data_value, uint32_t initial_range_value, bool is_enabled_initially);

/***************    Private variables    ***************/

static pwm_perph_t * pwm_perph = NULL;
static DEFINE_MUTEX(pwm_mutex);


/***************    Function Definitions    ***************/

static int __init pwm_driver_init(void)
{
  // Attempt to map the PWM peripheral
  pwm_perph = (pwm_perph_t *)(ioremap(PWM_BASE, PWM_SIZE));  // Note, a page size always has to be allocated, so even if it is under a page, it still takes up a page of memory.

  // For some reason the mapping failed
  if (NULL == pwm_perph)
  {
    // Exit immediately
    pr_err("PWM driver couldn't map the io space!\n");
    return -EMAPPING;
  }
  else
  {
    printk("PWM successfully mapped\n");
  }

  mutex_init(&pwm_mutex);

  printk("PWM driver successfully initialized\n");
  return ENONE;
}

static void __exit pwm_driver_exit(void)
{
  // If the gpio was successfully mapped
  if (NULL != pwm_perph)
  {
    // Release the GPIO mapping
    printk("Released PWM mapping\n");
    iounmap(pwm_perph);
  }

  pwm_reset_pwm_channels();
  
  mutex_destroy(&pwm_mutex);

  printk("PWM driver exited\n");
}

static int pwm_init_pwm_channel(pwm_channel_t pwm_channel, uint32_t initial_data_value, uint32_t initial_range_value, bool is_enabled_initially)
{
  int error = ENONE;

  mutex_lock(&pwm_mutex);

  switch (pwm_channel)
  {
    case PWM_0:
      // Clear the lower 8 bits of the register since these are all
      // for PWM_0
      pwm_perph->ctl &= 0xFFFFFF00;
      pwm_perph->dat_1 = initial_data_value;
      pwm_perph->rng_1 = initial_range_value;

      if (is_enabled_initially)
      {
        pwm_perph->ctl |= PWEN_1_FIELD;
      }

      break;

    case PWM_1:
      // Clear the second lowest 8 bits of the register since these are all
      // for PWM_1
      pwm_perph->ctl &= 0xFFFF00FF;
      pwm_perph->dat_2 = initial_data_value;
      pwm_perph->rng_2 = initial_range_value;

      if (is_enabled_initially)
      {
        pwm_perph->ctl |= PWEN_2_FIELD;
      }

      break;

    default:
      error = -EINVFUNC;
      goto exit_release_mutex;
      break;
  }

exit_release_mutex:
  mutex_unlock(&pwm_mutex);
  return error;
}

static inline void pwm_reset_pwm_channels(void)
{
  // Set the pwm channel channels we have modified back to their
  // reset values listed in the peripheral data sheet.
  pwm_init_pwm_channel(PWM_0, 0, 0x20, false);
  pwm_init_pwm_channel(PWM_1, 0, 0x20, false);
}

static inline bool validate_cycle_freq(pwm_cycle_freq_t cycle_freq)
{
  switch (cycle_freq)
  {
    case PWM_FREQ_4_kHZ:
      return true;
      break;

    default:
      return false;
      break;
  }

  return false;
}

static inline bool validate_pwm_channel(pwm_channel_t pwm_channel)
{
  switch (pwm_channel)
  {
    case PWM_0:
    case PWM_1:
      return true;
      break;
    
    default:
      return false;
      break;
  }

  return false;
}

static inline uint32_t calc_pwm_range_val_from_cycle_freq(pwm_cycle_freq_t cycle_freq)
{
  if (!validate_cycle_freq(cycle_freq))
  {
    return 0;
  }

  return (PWM_CLK_RATE / cycle_freq);
}

static inline uint32_t calc_pwm_data_val_from_percent(int percent, uint32_t pwm_range_val)
{
  if (!validate_cycle_freq(pwm_range_val))
  {
    return 0;
  }

  // Validate percent argument
  if (100 <= percent)
  {
    // PWM is fully on so the data val equals the pwm range val
    return pwm_range_val;
  }
  else if (0 >= percent)
  {
    // PWM is fully off so the data val equals zero
    return 0;
  }

  return ((pwm_range_val / 100) * percent);
}

static inline int pwm_get_channel_range_val(pwm_channel_t pwm_channel, uint32_t *range_val)
{
  switch (pwm_channel)
  {
    case PWM_0:
      *range_val = pwm_perph->rng_1;
      break;

    case PWM_1:
      *range_val = pwm_perph->rng_2;
      break;
    
    default:
      return -EINVFUNC;
      break;
  }

  return ENONE;
}

int pwm_init_user_device(pwm_channel_t pwm_channel, int duty_cycle, pwm_cycle_freq_t cycle_freq, bool is_enabled_initially)
{
  uint32_t range_val = calc_pwm_range_val_from_cycle_freq(cycle_freq);

  // Unsupported cycle_freq provided
  if (0 == range_val)
  {
    return -EINVFUNC;
  }

  uint32_t data_val = calc_pwm_data_val_from_percent(duty_cycle, range_val);

  return pwm_init_pwm_channel(pwm_channel, data_val, range_val, is_enabled_initially);
}

static inline void pwm_set_channel_data_val(pwm_channel_t pwm_channel, uint32_t data_val)
{
  switch (pwm_channel)
  {
    case PWM_0:
      pwm_perph->dat_1 = data_val;
      break;
    case PWM_1:
      pwm_perph->dat_2 = data_val;
      break;
  }
}

int pwm_set_duty_cycle(pwm_channel_t pwm_channel, int duty_cycle)
{
  if (validate_pwm_channel(pwm_channel))
  {
    return -EINVFUNC;
  }

  int error = ENONE;
  uint32_t range_val = 0;
  uint32_t data_val = 0;

  mutex_lock(&pwm_mutex);

  error = pwm_get_channel_range_val(pwm_channel, &range_val);

  if (ENONE != error)
  {
    goto exit_release_mutex;
  }

  data_val = calc_pwm_data_val_from_percent(duty_cycle, range_val);

  pwm_set_channel_data_val(pwm_channel, data_val);

exit_release_mutex:
  mutex_unlock(&pwm_mutex);

  return error;
}

int pwm_enable(pwm_channel_t pwm_channel, bool do_enable)
{
  int error = ENONE;
  pwm_ctl_field_t ctl_field;

  switch (pwm_channel)
  {
    case PWM_0:
      ctl_field = PWEN_1_FIELD;
      break;
    
    case PWM_1:
      ctl_field = PWEN_2_FIELD;
      break;

    default:
      return -EINVFUNC;
      break;
  }

  mutex_lock(&pwm_mutex);

  if (do_enable)
  {
    pwm_perph->ctl |= ctl_field;
  }
  else
  {
    pwm_perph->ctl &= ~(ctl_field);
  }

exit_release_mutex:
  mutex_unlock(&pwm_mutex);
  
  return ENONE;
}

module_init(pwm_driver_init);
module_exit(pwm_driver_exit);

EXPORT_SYMBOL(pwm_init_user_device);
EXPORT_SYMBOL(pwm_set_duty_cycle);
EXPORT_SYMBOL(pwm_enable);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver that controls pwm (specifically for LEDs right now).");
MODULE_VERSION("1.0");