#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>

#include "gpio-test-driver.h"

// Peripheral addresses
#define BCM2837_PERI_BASE     (0x3F000000)
#define GPIO_BASE             (BCM2837_PERI_BASE + 0x200000)
#define GPIO_SIZE             (0xB1)               // GPIO peripheral memory area in bytes

// Microcontroller way of doing it
// #define GPFSEL_BASE           (GPIO_BASE)
// #define GPSET_BASE            (GPIO_BASE + 0x1C)
// #define GPCLR_BASE            (GPIO_BASE + 0x28)
// #define GPLEV_BASE            (GPIO_BASE + 0x2C)

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

// Output Control register defines
#define OUTPUT_CTL_WRT_VAL    (0x01U)                 // Output control requires writing a 1 to the appropriate GPCLR or GPSET register
#define GPSET_SET_OUTPUT      (OUTPUT_CTL_WRT_VAL)    // Setting an output requires a 1 to be written (to the appropriate GPSET register)
#define GPCLR_CLEAR_OUTPUT    (OUTPUT_CTL_WRT_VAL)    // Clearing an output requires a 1 to be written (to the appropriate GPCLR register)

static uint32_t volatile * gpio_base_addr = NULL;   // All registers are 32 bit for gpio so use a uint32_t pointer

static int __init gpio_test_driver_init(void)
{
  // Attempt to map the GPIO
  gpio_base_addr = (uint32_t *)(ioremap(GPIO_BASE, GPIO_SIZE));  // Note, a page size always has to be allocated, so even if it is under a page, it still takes up a page of memory.

  // For some reason the mapping failed
  if (NULL == gpio_base_addr)
  {
    // Exit immediately
    pr_err("GPIO driver couldn't map the io space!\n");
    return 1;
  }
  else
  {
    printk("GPIO successfully mapped\n");
  }

  printk("GPIO driver successfully initialized\n");
  return 0;
}

static void __exit gpio_test_driver_exit(void)
{
  // If the gpio was successfully mapped
  if (NULL != gpio_base_addr)
  {
    // Release the GPIO mapping
    printk("Released GPIO mapping\n");
    iounmap(gpio_base_addr);
  }
  printk("GPIO driver exited\n");
}

static bool gpio_is_valid_pin(uint32_t pin_num)
{
  return ((MIN_PIN_NUM <= pin_num) && (MAX_PIN_NUM >= pin_num));
}

static void gpio_set_pin_to_input(uint32_t pin_num, bool is_active_high)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return;
  }


}

// Ret values:  false - output was not set, invalid pin_num argument
//
bool gpio_output_ctl(uint32_t pin_num, bool do_set)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return false;
  }
  
  // All registers are 32 bit and we only use the first set or clear register since the Raspberry Pi B
  // only has up to GPIO pin 27 accessible (so 28 pins total). Therefore they all can be accessed in the
  // first register of the corresponding set or clear registers. Pick whether we use the set or clear registers
  // based on the "do_set" function argument.
  uint32_t gpio_base_offset_reg_cnt = ((do_set ? GPSET_OFFSET : GPCLR_OFFSET ) / sizeof(uint32_t));

  uint32_t volatile * const output_pin_ctl_register = gpio_base_addr + gpio_base_offset_reg_cnt;

  *output_pin_ctl_register = (OUTPUT_CTL_WRT_VAL << pin_num);  
  return true;
}


// Ret values:  0 - success
//              1 - invalid pin_num argument
//              2 - other internal failure
int gpio_set_pin_to_output(uint32_t pin_num, bool is_on_initially)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!\n");
    return 1;
  }

  uint32_t register_offset = pin_num / GPFSEL_GPIO_PINS_PER_REG;  // Each GPFSEL register contains the alternate function select for 10 pins
  
  // Even though we verified the pin number above, do another check
  // to make sure we only access valid GPFSEL registers
  if (GPFSEL_MAX_REG_OFFSET < register_offset)
  {
    pr_err("Tried to access an invalid register during function select of pin!\n");
    return 2;
  }

  // Clear or set the pin so that when it is changed to an output, it will immediately be at the correct initial value
  gpio_output_ctl(pin_num, is_on_initially);

  uint32_t volatile * const pin_GPFSELx_reg = gpio_base_addr + (GPFSEL_OFFSET / sizeof(uint32_t)) + register_offset;

  uint32_t fsel_field_num = pin_num % GPFSEL_GPIO_PINS_PER_REG;
  uint32_t reg_value_to_write = (*pin_GPFSELx_reg) & (~(GPFSEL_FIELD_MASK << (fsel_field_num * GPFSEL_FIELD_BIT_WIDTH))); // First clear the alternative function field for that pin without affecting other pins.
  reg_value_to_write |= (GPFSEL_OUTPUT << (fsel_field_num * GPFSEL_FIELD_BIT_WIDTH));  // Next set that field to be an output
  
  printk("gpio_set_pin_to_output() - gpio_base_addr: %X, pin_GPFSELx_reg: %X, fsel_field_num: %d\n", gpio_base_addr, pin_GPFSELx_reg, fsel_field_num);
  printk("gpio_set_pin_to_output() - value of pin_GPFSELx_reg before write:%u, reg_value_to_write: %u\n", (*pin_GPFSELx_reg), reg_value_to_write);
  *pin_GPFSELx_reg = reg_value_to_write;
  return 0;
}

module_init(gpio_test_driver_init);
module_exit(gpio_test_driver_exit);

EXPORT_SYMBOL(gpio_output_ctl);
EXPORT_SYMBOL(gpio_set_pin_to_output);

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver that controls GPIO.");
MODULE_VERSION("1.0");