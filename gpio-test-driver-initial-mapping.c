#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

// Peripheral addresses
#define BCM2837_PERI_BASE     (0x3F000000)
#define GPIO_BASE             (BCM2708_PERI_BASE + 0x200000)
#define GPFSEL_BASE           (GPIO_BASE)
#define GPSET_BASE            (GPIO_BASE + 0x1C)
#define GPCLR_BASE            (GPIO_BASE + 0x28)
#define GPLEV_BASE            (GPIO_BASE + 0x2C)

// GPSEL register defines
#define MIN_PIN_NUM           (2)  // lowest gpio pin that is usable (inclusive)
#define MAX_PIN_NUM           (27)  // highest gpio pin that is usable (inclusive)
#define GPFSEL_MAX_REG_OFFSET (MAX_PIN_NUM / 10)
#define GPFSEL_INPUT          (0x00)
#define GPFSEL_OUTPUT         (0x01)
#define GPFSEL_ALT_FUNC_0     (0x04)
#define GPFSEL_ALT_FUNC_1     (0x05)
#define GPFSEL_ALT_FUNC_2     (0x06)
#define GPFSEL_ALT_FUNC_3     (0x07)
#define GPFSEL_ALT_FUNC_4     (0x03)
#define GPFSEL_ALT_FUNC_5     (0x02)


static int __init gpio_test_driver_init(void)
{
  printk("GPIO driver successfully initialized.\n");
  return 0;
}

static void __exit gpio_test_driver_exit(void)
{
  printk("GPIO driver exited.\n");
  return;
}

static bool gpio_is_valid_pin(uint32_t pin_num)
{
  return ((MIN_PIN_NUM <= pin_num) && (MAX_PIN_NUM >= pin_num));
}

static void gpio_set_pin_to_input(uint32_t pin_num, bool is_active_high)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!");
    return;
  }


}

static void gpio_set_pin_to_output(uint32_t pin_num)
{
  if (!gpio_is_valid_pin(pin_num))
  {
    pr_err("GPIO pin provided is outside valid pin range!");
    return;
  }

  uint32_t register_offset = pin_num / 10;  // Each GPFSEL register contains the alternate function select for 10 pins
  
  // Even though we verified the pin number above, do another check
  // to make sure we only access valid GPFSEL registers
  if (GPFSEL_MAX_REG_OFFSET < register_offset)
  {
    pr_err("Tried to access an invalid register during function select of pin!");
    return;
  }

  uint32_t volatile * const pin_GPFSELx_reg = ((uint32_t *)(GPFSEL_BASE)) + register_offset;

  uint32_t fsel_field_num = pin_num % 10;
  uint32_t reg_value_to_write = (*pin_GPFSELx_reg) & ((~(0x07)) << (fsel_field_num * 3)); // First clear the alternative function field for that pin.
  reg_value_to_write &= (GPFSEL_OUTPUT << (fsel_field_num * 3))
  
  *pin_GPFSELx_reg = reg_value_to_write;
}

module_init(gpio_test_driver_init);
module_exit(gpio_test_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("A practice Linux driver that controls GPIO.");
MODULE_VERSION("1.0");