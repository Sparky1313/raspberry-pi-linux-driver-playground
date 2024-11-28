#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/string.h>

#include "gpio-test-driver.h"
#include "playground-errno.h"
// #include <asm/io.h>


/***************    Macros    ***************/

#define LED_DEVICE_NAME   "gpio_led"
#define FIRST_LED_PIN     22    // This is the first pin on the Raspberry Pi 3B that I have dedicated to leds
#define MAX_LED_DEVICES   2
       

// Valid write messages are "on", "off", "toggle" and valid read messages are "on" and "off". 
// '\0' character is not included in these messages since we don't view this data as a string.
// Therefore "toggle" is the longest message at 6 characters and is the largest buffer size we need.
#define MSG_BUF_MAX_SIZE  6


/***************    Type definitions    ***************/

typedef enum led_state_s
{
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK = 2
} led_state_t;

typedef struct led_dev_s
{
  // TODO: Add in a mutex or semaphore for access protection
  uint32_t pin_num;
  bool is_led_on;
  led_state_t led_state;
  char msg_buffer[MSG_BUF_MAX_SIZE]; // valid write messages are "on", "off", "toggle" and valid read messages are "on" and "off". '\0' character is not included in these messages since we don't view this data as a string.
  struct cdev c_dev;
} led_dev_t;


/***************    Function declarations    ***************/

static int __init led_driver_init(void);
static void __exit led_driver_exit(void);
static inline void unregister_leds_cdev_region(void);
static int led_dev_init(led_dev_t * led_dev, uint32_t led_dev_index);

// File operation functions
static int led_open(struct inode *, struct file *);
static int led_release(struct inode *, struct file *);
static ssize_t led_read(struct file *, char *, size_t, loff_t *);
static ssize_t led_write(struct file *, const char *, size_t, loff_t *);


/***************    Private variables    ***************/

static int major_drv_num = 0;
static int first_minor_drv_num = 0;
static bool is_led_dev_0_open = false;
static bool is_led_dev_1_open = false;

static struct file_operations const led_fops =
{
  .read = led_read,
  .write = led_write,
  .open = led_open,
  .release = led_release
};

static led_dev_t led_dev_array[MAX_LED_DEVICES];


/***************    Functions    ***************/

static int __init led_driver_init(void)
{
  dev_t dev_id = 0;
  int error = ENONE;

  error = alloc_chrdev_region(&dev_id, 0, MAX_LED_DEVICES, LED_DEVICE_NAME);
  
  if (ENONE != error)
  {
    pr_err("LED driver couldn't allocate device ids for all the necessary devices.");
    goto failure_end;
  }

  major_drv_num = MAJOR(dev_id);
  first_minor_drv_num = MINOR(dev_id);

  int devices_successfully_inited = 0;

  for (uint32_t led_num = 0; led_num < MAX_LED_DEVICES; led_num++)
  {
    error = led_dev_init(&(led_dev_array[led_num]), led_num);

    if (ENONE == error)
    {
      devices_successfully_inited++;
    }
    else
    {
      goto delete_cdevs;
    }
  }

  printk("LED driver successfully initialized\n");
  return ENONE;

delete_cdevs:
  for (uint32_t i = 0; i < devices_successfully_inited; i++)
  {
    cdev_del(&(led_dev_array[i].c_dev));
  }

unregister_cdevs:
  unregister_leds_cdev_region();

failure_end:
  pr_err("LED failed initialization!\n");

  return error;
}

static void __exit led_driver_exit(void)
{
  int error = ENONE;

  for (uint32_t led_num = 0; led_num < MAX_LED_DEVICES; led_num++)
  {
    error = gpio_output_ctl(led_dev_array[led_num].pin_num, false);
    
    if (ENONE != error)
    {
      // Just log an event.
      // There isn't much else we can do during runtime if for some reason
      // turning off the led's output failed. This should only occur
      // if the pin number was wrong, which should have been caught elsewhere
      // before getting to this point.
      pr_err("Failed trying to turn output pin for LED off! error: %d\n", error); 
    }

    cdev_del(&(led_dev_array[led_num].c_dev));
  }
  
  unregister_leds_cdev_region();
  printk("LED driver exited\n");
}

static inline void unregister_leds_cdev_region(void)
{
  unregister_chrdev_region(MKDEV(major_drv_num, first_minor_drv_num), MAX_LED_DEVICES);
}

static int led_dev_init(led_dev_t * led_dev, uint32_t led_dev_index)
{
  int error = ENONE;

  led_dev->pin_num = FIRST_LED_PIN + led_dev_index;

  // Try to set the led pin of the device driver to an output and set it to be off initially
  // If the attempt failed, return the error

  error = gpio_set_pin_to_output(led_dev->pin_num, false);
  if (ENONE != error)
  {
    return error;
  }

  led_dev->led_state = LED_OFF;
  led_dev->is_led_on = false;

  // Note there is no need to clear the msg_buffer since different
  // message lengths will be in it anyway.

  // Setup the cdev
  int dev_id = MKDEV(major_drv_num, first_minor_drv_num + led_dev_index);

  cdev_init(&(led_dev->c_dev), &led_fops);
  led_dev->c_dev.owner = THIS_MODULE;
  
  // Try to add the character device
  error = cdev_add(&(led_dev->c_dev), dev_id, 1);
  if (ENONE != error)
  {
    // TODO: Add some error message
    return error;
  }

  return ENONE;
}

static int led_open(struct inode *p_inode, struct file *p_file)
{
  pr_info("Open was successful\n");
	return 0;
}

static int led_release(struct inode * p_inode, struct file *p_file)
{
  pr_info("Release was successful\n");
	return 0;
}

static ssize_t led_read(struct file *p_file, char *user_buffer, size_t len, loff_t *p_offset)
{
  printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
	return -EINVAL;
}

static ssize_t led_write(struct file *p_file, const char *user_buffer, size_t len, loff_t *p_offset)
{
  printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
	return -EINVAL;
}


module_init(led_driver_init);
module_exit(led_driver_exit);

MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver that controls LEDs.");
MODULE_VERSION("1.0");