#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

static int __init gpio_driver_init(void)
{
  printk("GPIO driver successfully initialized.\n");
  return 0;
}

static void __exit gpio_driver_exit(void)
{
  printk("GPIO driver exited.\n");
  return;
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("A practice Linux driver that controls GPIO.");
MODULE_VERSION("1.0");