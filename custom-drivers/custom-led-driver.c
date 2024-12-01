#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include "custom-gpio-driver.h"
#include "custom-errno.h"


/***************    Macros    ***************/

#define LED_DEVICE_NAME         "custom_gpio_led"
#define LED_BLINK_THREAD_NAME   "custom_gpio_led_blink_thread"
#define LED_CLASS               "custom_gpio_led_class"
#define FIRST_LED_PIN           22                        // This is the first pin on the Raspberry Pi 3B that I have dedicated to leds
#define MAX_LED_DEVICES         2
       

// Valid write messages are "on", "off", "toggle" and valid read messages are "on" and "off". 
// '\0' character can be included in these messages since we might view this data as a string.
// Therefore "toggle" is the longest message at 7 characters (6 chars + '\0') and is the largest buffer size we need.
#define MSG_BUF_MAX_SIZE  7


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
  char msg_buffer[MSG_BUF_MAX_SIZE]; // valid write messages are "on", "off", "toggle" and valid read messages are "on" and "off".
  struct cdev c_dev;
  struct device * p_device;
  struct task_struct *p_blink_thread;
} led_dev_t;


/***************    Function declarations    ***************/

// Inline functions
static inline void unregister_leds_cdev_region(void);
static inline led_state_t get_led_state_from_physical_state(led_dev_t *led_dev);
static inline int clear_led_blinking(led_dev_t *led_dev);

// Normal functions
static int __init led_driver_init(void);
static void __exit led_driver_exit(void);
static int led_dev_init(led_dev_t *led_dev, uint32_t led_dev_index);
static int led_dev_uevent(struct device *dev, struct kobj_uevent_env *env);
static int led_blink(void *arg);

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
static struct class *p_led_class = NULL; 

static struct file_operations const led_fops =
{
  .read = led_read,
  .write = led_write,
  .open = led_open,
  .release = led_release
};

static led_dev_t led_dev_array[MAX_LED_DEVICES];
static char *led_write_word_cmds[] =
{
  "OFF",
  "ON",
  "TOGGLE",
  "BLINK"
};

static char *led_write_num_cmds[] =
{
  "0",
  "1",
  "2",
  "3"
};


/***************    Function Definitions    ***************/

static int __init led_driver_init(void)
{
  dev_t dev_id = 0;
  int error = ENONE;

  error = alloc_chrdev_region(&dev_id, 0, MAX_LED_DEVICES, LED_DEVICE_NAME);
  
  if (ENONE != error)
  {
    pr_err("LED driver couldn't allocate device ids for all the necessary devices.\n");
    goto failure_end;
  }

  major_drv_num = MAJOR(dev_id);
  first_minor_drv_num = MINOR(dev_id);

  // Create the device class before the cdev so that led_dev_init can create
  // the actual device for each led when it is called.
  p_led_class = class_create(THIS_MODULE, LED_CLASS);

  if (IS_ERR(p_led_class))
  {
    error = PTR_ERR(p_led_class);
    pr_err("Failed to create class for LEDs! error: %d\n", error);
    goto unregister_led_cdev_region;
  }

  // Now assign the custom dev_uevent function that will run when a new device
  // is created. We use this to set device permissions at creation.
  p_led_class->dev_uevent = led_dev_uevent;

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
      goto delete_led_cdevs_and_devices;
    }
  }

  printk("LED driver successfully initialized\n");
  return ENONE;

delete_led_cdevs_and_devices:
  for (uint32_t i = 0; i < devices_successfully_inited; i++)
  {
    // We should never have a null pointer for p_device
    // if the device was successfully inited, but we
    // will double-check just to be sure.
    if (NULL != led_dev_array[i].p_device)
    {
      device_destroy(p_led_class, led_dev_array[i].c_dev.dev);
    }

    cdev_del(&(led_dev_array[i].c_dev));
  }

delete_led_class:
  class_destroy(p_led_class);

unregister_led_cdev_region:
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
    // If the led device is running on another thread (i.e. the blink thread)
    // then request that thread to stop before trying to destroy the led kernel
    // module.
    if (NULL != led_dev_array[led_num].p_blink_thread)
    {
      kthread_stop(led_dev_array[led_num].p_blink_thread);   // Stop the LED flashing thread
    }

    error = gpio_output_ctl(led_dev_array[led_num].pin_num, false);
    
    if (unlikely(ENONE != error))
    {
      // Just log an event.
      // There isn't much else we can do during runtime if for some reason
      // turning off the led's output failed. This should only occur
      // if the pin number was wrong, which should have been caught elsewhere
      // before getting to this point.
      pr_err("Failed trying to turn output pin for LED off! error: %d\n", error); 
    }

    printk("Destroyed device with device id: %d\n", led_dev_array[led_num].c_dev.dev);
    device_destroy(p_led_class, led_dev_array[led_num].c_dev.dev);
    cdev_del(&(led_dev_array[led_num].c_dev));
  }

  class_destroy(p_led_class);
  unregister_leds_cdev_region();
  printk("LED driver exited\n");
}


static inline void unregister_leds_cdev_region(void)
{
  unregister_chrdev_region(MKDEV(major_drv_num, first_minor_drv_num), MAX_LED_DEVICES);
}


static inline led_state_t get_led_state_from_physical_state(led_dev_t *led_dev)
{
  return (led_dev->is_led_on ? LED_ON : LED_OFF);
}


static inline int clear_led_blinking(led_dev_t *led_dev)
{
  // If the led device is running the blink thread
  // then request that thread to stop before trying to modify the led.
  if (LED_BLINK == led_dev->led_state)
  {
    if (unlikely(NULL == led_dev->p_blink_thread))
    {
      pr_err("led_write() - LED device should never be in the blink state but not have a pointer to the blink thread!");
      return -EINTERNAL;
    }
    else
    {
      kthread_stop(led_dev->p_blink_thread);   // Stop the LED flashing thread
    }
  }

  return ENONE;
}


static int led_dev_init(led_dev_t *led_dev, uint32_t led_dev_index)
{
  int error = ENONE;

  led_dev->p_device = NULL;
  led_dev->pin_num = FIRST_LED_PIN + led_dev_index;

  // Try to set the led pin of the device driver to an output and set it to be off initially
  // If the attempt failed, return the error

  error = gpio_set_pin_to_output(led_dev->pin_num, false);

  if (unlikely(ENONE != error))
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

  // Create the device name for the actual led device
  char led_device_name[24]; // There is nothing special about picking 24 bytes. It just fits the name and a large number of devices.
  snprintf(led_device_name, sizeof(led_device_name), "%s_%d", LED_DEVICE_NAME, led_dev_index);
  printk("Creating device with name: %s\n", led_device_name);

  // Try to create the actual led device
  led_dev->p_device = device_create(p_led_class, NULL, led_dev->c_dev.dev, NULL, led_device_name);

  if (IS_ERR(led_dev))
  {
    error = PTR_ERR(led_dev->p_device);
    pr_err("Creating actual LED device failed! error: %d\n", error);

    // Delete this device's cdev that was added
    cdev_del(&(led_dev->c_dev));
    
    return error;
  }

  return ENONE;
}


static int led_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
  // Look at linux/drivers/base/core.c for an example of add_uevent_var
  add_uevent_var(env, "DEVMODE=%#o", 0666);
  return ENONE;
}

static int led_open(struct inode *p_inode, struct file *p_file)
{
  pr_info("Open was successful\n");

  led_dev_t *led_dev = container_of(p_inode->i_cdev, led_dev_t, c_dev);
  p_file->private_data = led_dev;
	return ENONE;
}

static int led_release(struct inode * p_inode, struct file *p_file)
{
  pr_info("Release was successful\n");
	return ENONE;
}

static ssize_t led_read(struct file *p_file, char *user_buffer, size_t len, loff_t *p_offset)
{
  printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
	return -EINVAL;
}


// Data written can be raw characters (i.e. char arrays without the NUL terminator) or strings (i.e. char arrays with the NUL terminator)
// 
// NOTE: Raw characters or strings ending with a newline char are not supported. (Future support may be added)
//
// NOTE: When writing using the "echo" command from the terminal, the "echo" command will append the '\n' char ("LF" char on Linux).
//       Either type "echo -n" to eliminate the newline or don't include a space after the last argument.
//       (e.g. echo -n "1" > your_device_path, echo -n 1 > your_device_path, echo 1> your_device_path)
static ssize_t led_write(struct file *p_file, const char *user_buffer, size_t len, loff_t *p_offset)
{
  // First check that the message isn't too large
  if (MSG_BUF_MAX_SIZE < len)
  {
    printk(KERN_ERR "led_write() - Length to write is too long! Max msg size: %d", MSG_BUF_MAX_SIZE);
    return -EMSGSIZE;
  }
  // Nothing to write, so say nothing was written
  else if (0 >= len)
  {
    return 0;
  }

  led_dev_t *led_dev = p_file->private_data;

  int error = copy_from_user(&(led_dev->msg_buffer), user_buffer, len);

  if (ENONE != error)
  {
    printk(KERN_ERR "led_write() - Failed to get user_buffer data! error: %d", error);
    return error;
  }

  // NOTE: We use "len" instead of "MSG_BUF_MAX_SIZE" for the max in strncasecmp calls here
  //       because the user argument may or may not be a string which means it would have the '\0' char appended.
  //       We can use "len" instead of "MSG_BUF_MAX_SIZE" because we already checked that the user argument is
  //       not larger than "MSG_BUF_MAX_SIZE" upon entry into this function.
  //
  // NOTE: We don't support having the '\n' char appended to the message, but may support it in the future.  

  // OFF command
  if (   (0 == strncasecmp(led_write_word_cmds[0], led_dev->msg_buffer, len))
      || (0 == strncasecmp(led_write_num_cmds[0], led_dev->msg_buffer, len))   
     )
  {
    clear_led_blinking(led_dev);

    // TODO: Possibly make this its own function
    error = gpio_output_ctl(led_dev->pin_num, false);
    if (unlikely(ENONE != error))
    {
      return error;
    }

    led_dev->is_led_on = false;
    led_dev->led_state = LED_OFF;
  }
  // ON command
  else if (   (0 == strncasecmp(led_write_word_cmds[1], led_dev->msg_buffer, len))
           || (0 == strncasecmp(led_write_num_cmds[1], led_dev->msg_buffer, len))   
          )
  {
    clear_led_blinking(led_dev);

    error = gpio_output_ctl(led_dev->pin_num, true);
    if (unlikely(ENONE != error))
    {
      return error;
    }

    led_dev->is_led_on = true;
    led_dev->led_state = LED_ON;
  }
  // TOGGLE command
  else if (   (0 == strncasecmp(led_write_word_cmds[2], led_dev->msg_buffer, len))
           || (0 == strncasecmp(led_write_num_cmds[2], led_dev->msg_buffer, len))   
          )
  {
    clear_led_blinking(led_dev);

    error = gpio_output_ctl(led_dev->pin_num, !(led_dev->is_led_on));
    if (unlikely(ENONE != error))
    {
      return error;
    }

    led_dev->is_led_on = !(led_dev->is_led_on);
    led_dev->led_state = get_led_state_from_physical_state(led_dev);
  }
  // BLINK command
  else if (   (0 == strncasecmp(led_write_word_cmds[3], led_dev->msg_buffer, len))
           || (0 == strncasecmp(led_write_num_cmds[3], led_dev->msg_buffer, len))   
          )
  {
    clear_led_blinking(led_dev);

    // Create the device name for the actual led device
    char led_thread_name[40]; // There is nothing special about picking 40 bytes. It just fits the name and a large number of devices.
    int led_dev_index = led_dev->pin_num - FIRST_LED_PIN;
    snprintf(led_thread_name, sizeof(led_thread_name), "%s_%d", LED_BLINK_THREAD_NAME, led_dev_index);
    
    led_dev->p_blink_thread = kthread_run(led_blink, led_dev, led_thread_name);
    if (IS_ERR(led_dev->p_blink_thread))
    {                                     // Kthread name is LED_flash_thread
      pr_err("led_write() - failed to create blink thread for led\n");
      return PTR_ERR(led_dev->p_blink_thread);
    }

    led_dev->led_state = LED_BLINK;
  }
  // Unsupported command
  else
  {
    // TODO: Add in some error response
    return -EUNSUPCMD;
  }

  // Note if you return 0 it indicates nothing was written.
  // The standard c library will try rewriting.
  // So if we try to write to this device from a terminal
  // it basically creates an infinite loop of the terminal
  // trying to write, getting a 0 back, and trying again.
  // Essentially impossible to uninstall the driver then since it
  // is almost always processing the write command.

  return len;
}


static int led_blink(void *arg)
{
  int error = ENONE;

  led_dev_t *led_dev = (led_dev_t *)arg;
  
  while (!kthread_should_stop())
  {
    set_current_state(TASK_RUNNING);

    // TODO: probably just create a generic toggle function that can be used here 
    //       and for when the user types toggle.

    error = gpio_output_ctl(led_dev->pin_num, !(led_dev->is_led_on));
    if (unlikely(ENONE != error))
    {
      // Set the led state to not be BLINK anymore, base it on the actual current physical
      // led state.
      led_dev->led_state = get_led_state_from_physical_state(led_dev);
      
      // Clear the thread pointer so that we know
      // the blink thread is not running for the device.
      led_dev->p_blink_thread = NULL;
      return error;
    }

    led_dev->is_led_on = !(led_dev->is_led_on);

    msleep_interruptible(125);    // Blink 4 times a second
  }

  set_current_state(TASK_RUNNING);

  // Try to turn the led off before exiting
  error = gpio_output_ctl(led_dev->pin_num, false);

  if (ENONE == error)
  {
    led_dev->is_led_on = false;
  }

  // Set the led state to not be BLINK anymore, base it on the actual current physical
  // led state.
  led_dev->led_state = get_led_state_from_physical_state(led_dev);

  // Clear the thread pointer so that we know
  // the blink thread is not running for the device.
  led_dev->p_blink_thread = NULL;

  return error;
};


module_init(led_driver_init);
module_exit(led_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trevor Foland");
MODULE_DESCRIPTION("A practice Linux driver that controls LEDs.");
MODULE_VERSION("1.0");