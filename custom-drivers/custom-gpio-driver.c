#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <asm/io.h>

#include "custom-gpio-driver.h"
#include "custom-errno.h"


/***************    Macros    ***************/

// Peripheral addresses
#define GPIO_BASE             (BCM2837_PERI_BASE + 0x200000)
#define GPIO_SIZE             (0xB1)               // GPIO peripheral memory area in bytes

// All offsets are defined in bytes
#define GPFSEL_OFFSET           (0x00)
#define GPSET_OFFSET            (0x1C)
#define GPCLR_OFFSET            (0x28)
#define GPLEV_OFFSET            (0x2C)

// GPSEL register defines
#define MIN_PIN_NUM               (2)   // lowest gpio pin that is usable (inclusive)
#define MAX_PIN_NUM               (27)  // highest gpio pin that is usable (inclusive)
#define GPFSEL_GPIO_PINS_PER_REG  (10)
#define GPFSEL_MAX_REG_OFFSET     (MAX_PIN_NUM / GPFSEL_GPIO_PINS_PER_REG)
#define GPFSEL_FIELD_BIT_WIDTH    (3)       // Each GPFSEL pin/field has a width of 3 bits in its register.
#define GPFSEL_FIELD_MASK         (0x07U)   // Each GPFSEL pin/field has a width of 3 bits so 0x07U is the mask for a field.
#define GPFSEL_INPUT              (0x00U)
#define GPFSEL_OUTPUT             (0x01U)
#define GPFSEL_ALT_FUNC_0         (0x04U)
#define GPFSEL_ALT_FUNC_1         (0x05U)
#define GPFSEL_ALT_FUNC_2         (0x06U)
#define GPFSEL_ALT_FUNC_3         (0x07U)
#define GPFSEL_ALT_FUNC_4         (0x03U)
#define GPFSEL_ALT_FUNC_5         (0x02U)

#define GPIO_INPUT_FUNC           (GPFSEL_INPUT)
#define GPIO_OUTPUT_FUNC          (GPFSEL_OUTPUT)
#define GPIO_ALT_FUNC_0           (GPFSEL_ALT_FUNC_0)
#define GPIO_ALT_FUNC_1           (GPFSEL_ALT_FUNC_1)
#define GPIO_ALT_FUNC_2           (GPFSEL_ALT_FUNC_2)
#define GPIO_ALT_FUNC_3           (GPFSEL_ALT_FUNC_3)
#define GPIO_ALT_FUNC_4           (GPFSEL_ALT_FUNC_4)
#define GPIO_ALT_FUNC_5           (GPFSEL_ALT_FUNC_5)
#define GPIO_INVALID_FUNC         (0xFFU) // Picked max byte value

// Output Control register defines
#define OUTPUT_CTL_WRT_VAL    (0x01U)                 // Output control requires writing a 1 to the appropriate GPCLR or GPSET register
#define GPSET_SET_OUTPUT      (OUTPUT_CTL_WRT_VAL)    // Setting an output requires a 1 to be written (to the appropriate GPSET register)
#define GPCLR_CLEAR_OUTPUT    (OUTPUT_CTL_WRT_VAL)    // Clearing an output requires a 1 to be written (to the appropriate GPCLR register)

/***************    Type definitions    ***************/

// The enum value is also the FSEL value for that function type.
typedef uint32_t gpio_func_type_t;


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

static uint32_t volatile * gpio_base_addr = NULL;   // All registers are 32 bit for gpio so use a uint32_t pointer
static DEFINE_MUTEX(gpio_func_mutex);


/***************    Function Definitions    ***************/

static int __init gpio_driver_init(void)
{
  // Attempt to map the GPIO
  gpio_base_addr = (uint32_t *)(ioremap(GPIO_BASE, GPIO_SIZE));  // Note, a page size always has to be allocated, so even if it is under a page, it still takes up a page of memory.

  // For some reason the mapping failed
  if (NULL == gpio_base_addr)
  {
    // Exit immediately
    pr_err("GPIO driver couldn't map the io space!\n");
    return -EMAPPING;
  }
  else
  {
    printk("GPIO successfully mapped\n");
  }

  mutex_init(&gpio_func_mutex);

  printk("GPIO driver successfully initialized\n");
  return ENONE;
}

static void __exit gpio_driver_exit(void)
{
  // If the gpio was successfully mapped
  if (NULL != gpio_base_addr)
  {
    // Release the GPIO mapping
    printk("Released GPIO mapping\n");
    iounmap(gpio_base_addr);
  }
  
  mutex_destroy(&gpio_func_mutex);

  printk("GPIO driver exited\n");
}

static inline bool gpio_is_valid_pin(uint32_t pin_num)
{
  return ((MIN_PIN_NUM <= pin_num) && (MAX_PIN_NUM >= pin_num));
}

static inline bool gpio_is_valid_pin_func(gpio_func_type_t gpio_func_type)
{
  switch (gpio_func_type)
  {
    case GPIO_INPUT_FUNC:
    case GPIO_OUTPUT_FUNC:
    case GPIO_ALT_FUNC_0:
    case GPIO_ALT_FUNC_1:
    case GPIO_ALT_FUNC_2:
    case GPIO_ALT_FUNC_3:
    case GPIO_ALT_FUNC_4:
    case GPIO_ALT_FUNC_5:
      return true;
      break;
    
    default:
      return false;
      break;
  }

  return false;
}

// Ret values:  ENONE       - success
//              -EINVPIN    - failure, invalid pin_num argument
//              -EINVREG    - failure, invalid register access
//              -EINVFUNC   - failure, invalid gpio_func_type
//              -EINTERNAL  - failure, other internal failure 
static int gpio_set_pin_function(uint32_t pin_num, gpio_func_type_t gpio_func_type)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return -EINVPIN;
  }

  if (!gpio_is_valid_pin_func(gpio_func_type))
  {
    pr_err("GPIO function provided is not valid!\n");
    return -EINVFUNC;
  }

  uint32_t register_offset = pin_num / GPFSEL_GPIO_PINS_PER_REG;  // Each GPFSEL register contains the alternate function select for 10 pins
  
  // Even though we verified the pin number above, do another check
  // to make sure we only access valid GPFSEL registers
  if (GPFSEL_MAX_REG_OFFSET < register_offset)
  {
    pr_err("Tried to access an invalid register during function select of pin!\n");
    return -EINVREG;
  }

  uint32_t volatile * const pin_GPFSELx_reg = gpio_base_addr + (GPFSEL_OFFSET / sizeof(uint32_t)) + register_offset;
  uint32_t fsel_field_num = pin_num % GPFSEL_GPIO_PINS_PER_REG;

  // Lock this section since we are modifying gpio function values and we have to
  // read out the value of the register before writing it.
  mutex_lock(&gpio_func_mutex);

  uint32_t reg_value_to_write = (*pin_GPFSELx_reg) & (~(GPFSEL_FIELD_MASK << (fsel_field_num * GPFSEL_FIELD_BIT_WIDTH))); // First clear the alternative function field for that pin without affecting other pins.
  reg_value_to_write |= (gpio_func_type << (fsel_field_num * GPFSEL_FIELD_BIT_WIDTH));  // Next set that field to be an output
  
  printk("gpio_set_pin_to_output() - gpio_base_addr: %X, pin_GPFSELx_reg: %X, fsel_field_num: %d\n", gpio_base_addr, pin_GPFSELx_reg, fsel_field_num);
  printk("gpio_set_pin_to_output() - value of pin_GPFSELx_reg before write:%u, reg_value_to_write: %u\n", (*pin_GPFSELx_reg), reg_value_to_write);
  *pin_GPFSELx_reg = reg_value_to_write;
  
  mutex_unlock(&gpio_func_mutex);

  return ENONE;
}


// TODO: Fully implement this eventually
static void gpio_set_pin_to_input(uint32_t pin_num, bool is_active_high)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return;
  }
}

// Ret values:  ENONE     - success
//              -EINVPIN  - failure, invalid pin_num argument
//
int gpio_output_ctl(uint32_t pin_num, bool do_set)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return -EINVPIN;
  }
  
  // All registers are 32 bit and we only use the first set or clear register since the Raspberry Pi B
  // only has up to GPIO pin 27 accessible (so 28 pins total). Therefore they all can be accessed in the
  // first register of the corresponding set or clear registers. Pick whether we use the set or clear registers
  // based on the "do_set" function argument.
  uint32_t gpio_base_offset_reg_cnt = ((do_set ? GPSET_OFFSET : GPCLR_OFFSET ) / sizeof(uint32_t));

  uint32_t volatile * const output_pin_ctl_register = gpio_base_addr + gpio_base_offset_reg_cnt;

  *output_pin_ctl_register = (OUTPUT_CTL_WRT_VAL << pin_num);  
  return ENONE;
}


// Ret values:  ENONE       - success
//              -EINVPIN    - failure, invalid pin_num argument
//              -EINVREG    - failure, invalid register access
//              -EINTERNAL  - failure, other internal failure
int gpio_set_pin_to_output(uint32_t pin_num, bool is_on_initially)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return -EINVPIN;
  }

  // Clear or set the pin so that when it is changed to an output, it will immediately be at the correct initial value
  int error = gpio_output_ctl(pin_num, is_on_initially);

  if (ENONE != error)
  {
    return error;
  }

  error = gpio_set_pin_function(pin_num, GPIO_OUTPUT_FUNC);

  if (ENONE != error)
  {
    return error;
  }

  return ENONE;
}

pwm_channel_t gpio_is_pin_pwm(uint32_t pin_num)
{
  pwm_channel_t pwm_channel = NOT_PWM;

  switch (pin_num)
  {
    case 12:
    case 18:
      pwm_channel = PWM_0;
      break;

    case 13:
    case 19:
      pwm_channel = PWM_1;
      break;

    default:
      pwm_channel = NOT_PWM;
      break;
  }

  return pwm_channel;
}

static gpio_func_type_t gpio_determine_pwm_alt_func(uint32_t pin_num)
{
  gpio_func_type_t gpio_func_type = GPIO_INVALID_FUNC;

  switch (pin_num)
  {
    case 12:
    case 13:
      gpio_func_type = GPIO_ALT_FUNC_0;
      break;

    case 18:
    case 19:
      gpio_func_type = GPIO_ALT_FUNC_5;
      break;

    default:
      gpio_func_type = GPIO_INVALID_FUNC;
      break;
  }

  return gpio_func_type;
}

// Ret values:  ENONE       - success              
//              -EINVPIN    - failure, invalid pin_num argument
//              -EINVREG    - failure, invalid register access
//              -EINVFUNC   - failure, invalid gpio_func_type
//              -EINTERNAL  - failure, other internal failure
int gpio_set_pin_to_pwm(uint32_t pin_num)
{
  if (NOT_PWM == gpio_is_pin_pwm(pin_num))
  {
    return -EINVPIN;
  }

  if (GPIO_INVALID_FUNC == gpio_determine_pwm_alt_func(pin_num))
  {
    return -EINVFUNC;
  }

  int error = gpio_set_pin_function(pin_num, GPIO_OUTPUT_FUNC);

  if (ENONE != error)
  {
    return error;
  }

  return ENONE;
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

EXPORT_SYMBOL(gpio_output_ctl);
EXPORT_SYMBOL(gpio_set_pin_to_output);
EXPORT_SYMBOL(gpio_is_pin_pwm);
EXPORT_SYMBOL(gpio_set_pin_to_pwm);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver that controls GPIO.");
MODULE_VERSION("1.0");