#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
// #include <linux/mutex.h>
#include <asm/io.h>

#include "custom-driver-shared-info.h"
#include "custom-pwm-driver.h"
// #include "custom-gpio-driver.h"
// #include "custom-errno.h"


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


/***************    Type definitions    ***************/

// Datasheet calls the channels 0 and 1 but puts 1 and 2 as the register names.
// I stuck with 1 and 2 since it makes the doc easier to search.
typedef enum pwm_ctl_field_e
{
  PWEN_1_FIELD    = (1),
  MODE_1_FIELD    = (1 << 1),
  RPTL_1_FIELD    = (1 << 2),
  SBIT_1_FIELD    = (1 << 3),
  POLA_1_FIELD    = (1 << 4),
  USEF_1_FIELD    = (1 << 5),
  CLRF_1_FIELD    = (1 << 6),
  MSEN_1_FIELD    = (1 << 7),
  PWEN_2_FIELD    = (1 << 8),
  MODE_2_FIELD    = (1 << 9),
  RPTL_2_FIELD    = (1 << 10),
  SBIT_2_FIELD    = (1 << 11),
  POLA_2_FIELD    = (1 << 12),
  USEF_2_FIELD    = (1 << 13),
  RESERVED_FIELD  = (1 << 14),
  MSEN_2_FIELD    = (1 << 15)
} pwm_ctl_field_t;

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
static inline bool gpio_is_valid_pin(uint32_t pin_num);
static inline bool gpio_is_valid_pin_func(gpio_func_type_t gpio_func_type);

// Static functions
static int __init gpio_driver_init(void);
static void __exit gpio_driver_exit(void);
static void gpio_set_pin_to_input(uint32_t pin_num, bool is_active_high);
static gpio_func_type_t gpio_determine_pwm_alt_func(uint32_t pin_num);

/***************    Private variables    ***************/

static pwm_perph_t * pwm_perph = NULL;
static DEFINE_MUTEX(pwm_mutex);

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

static inline bool validate_cycle_freq(pwm_cycle_freq_t cycle_freq)
{
  switch (cycle)
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

static inline uint32_t calc_pwm_range_val_from_cycle_freq(pwm_cycle_freq_t cycle_freq)
{
  // TODO: Validate cycle_freq
  return (PWM_CLK_RATE / cycle_freq);
}

static inline uint32_t calc_pwm_data_val_from_percent(int percent, uin32_t pwm_range_val)
{
  // TODO: Validate cycle_freq

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

int pwm_init_user_device(pwm_channel_t pwm_channel, int duty_cycle, pwm_cycle_freq_t cycle_freq, bool is_enabled_initially)
{
  uint32_t range_val = calc_pwm_range_val_from_cycle_freq(cycle_freq);

  // Unsupported cycle_freq provided
  if (0 == range_val)
  {
    return -EINVFUNC;
  }

  uint32_t data_val = calc_pwm_data_val_from_percent(duty_cycle, range_val);

  pwm_init_pwm_channel(pwm_channel, data_val, range_val, is_enabled_initially);
}