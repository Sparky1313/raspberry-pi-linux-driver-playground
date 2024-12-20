# About

Located in this directory are custom kernel drivers/modules that I am working on.

Below are instructions on how to interact with these modules.

# Installing a Kernel Module

To install a kernel module:
  
1. Open a terminal and navigate to the `custom-drivers` directory.
  
2. Create the kernel objects needed for the drivers by running typing and entering `make` in the terminal. You should see *.ko* files created with names matching the *.c* files in this directory.

3. To install the kernel modules you can use the command `sudo insmod <name_of_file>.ko`.
      
    - So for the `custom-gpio-driver.ko` module you would enter `sudo insmod custom-gpio-driver.ko`.

    - You need to install modules in a specific order since some have dependencies on other modules.

    - It is not dangerous if you enter the wrong module since a module will not load if it is dependent on another module that has not been loaded yet.

    - Installation order of modules is detailed here: [Module Installation Order](#module-installation-order)

4. If no error is displayed, you can confirm the module loaded by entering the command `lsmod | grep <name_of_file>`
    - Because of the naming convention I have used, you should be able to use the command `lsmod | grep custom` to see all of the modules you've installed.
  
5. You can view any messages from installing the kernel module by entering the command `dmesg`.

6. To view devices that you can access you can enter `ls -l /dev | grep <device_name>`.

    - Because of the naming convention I have used, you should be able to use the command `ls -l /dev | grep custom` to see all of the devices you've created.

# Removing a Kernel Module

To Remove a kernel module:

1. Open a terminal and navigate to the `custom-drivers` directory.
  
2. Remove the desired module by entering `sudo rmmod <name_of_module>`.
  
    - So for the `custom-gpio-driver.ko` module you would enter `sudo rmmod custom-gpio-driver.ko`.
      
    - You need to remove modules in the specific order (typically the opposite order that you installed them) since some modules have dependencies on other modules.

    - It is not dangerous if you enter the wrong module since a module will not be removed if another module is dependent on it.

    - Installation order of modules is detailed here: [Module Installation Order](#module-installation-order)

3. If no error is displayed, you can confirm the module was removed by entering the command `lsmod | grep <name_of_module>`. You should see no results.
      
    - Because of the naming convention I have used, you should be able to use the command `lsmod | grep custom` to see any remaining modules that you have installed.
  
4. You can view any messages from removing the kernel module by entering the command `dmesg`.

5. You can confirm that devices created by a driver have been removed by entering `ls -l /dev | grep <device_name>`. You should see no results.

    - Because of the naming convention I have used, you should be able to use the command `ls -l /dev | grep custom` to see any remaining devices you've created.

# Troubleshooting Kernel Modules

One of the most useful tools you will have for troubleshooting a module is using the command `dmesg` to display kernel messages.

Use this to troubleshoot or gather more info about the modules you've installed.

# Reading from a Kernel Device

Currently no kernel modules support this.

To read from a kernel device from the terminal you can use the command `cat /dev/<device>`

# Writing to a Kernel Device

To write a kernel device from the terminal you can use the command `echo -n "<message_to_write>" > /dev/<device>`

  - For instance, to toggle the first custom led device created by a driver, you could enter `echo -n "toggle" > /dev/custom_gpio_led_0`

# Kernel Modules

## GPIO Module

### Code 

- Code located here: [custom-gpio-driver](custom-gpio-driver.c)

### Devices

- No devices available to interact with directly.

## PWM Module

### Code 

- Code located here: [custom-pwm-driver](custom-pwm-driver.c)

### Devices

- No devices available to interact with directly.

## LED Module

### Code

- Code located here: [custom-led-driver](custom-led-driver.c)

### Devices

#### Device Names

- custom_gpio_led_0

- custom_gpio_led_1

- custom_gpio_led_2

- custom_gpio_led_3

#### Device Interactions

1. Read commands:

    - Not supported yet.

2. Write commands:
  
    1. Turn LED off.
        
        1. Write a 0
        
            - From the terminal enter command `echo -n 0 > /dev/<device>`
        
        2. Write *off* (case-insensitive)

            - From the terminal enter command `echo -n "off" > /dev/<device>`
      
    1. Turn LED on.
        
        1. Write a 1
        
            - From the terminal enter command `echo -n 1 > /dev/<device>`
        
        2. Write *on* (case-insensitive)

            - From the terminal enter command `echo -n "on" > /dev/<device>`

    3. Toggle LED.
        
        1. Write a 2
        
            - From the terminal enter command `echo -n 2 > /dev/<device>`
        
        2. Write *toggle* (case-insensitive)

            - From the terminal enter command `echo -n "toggle" > /dev/<device>`
    
    4. Blink LED.
        
        1. Write a 3
        
            - From the terminal enter command `echo -n 3 > /dev/<device>`
        
        2. Write *blink* (case-insensitive)

            - From the terminal enter command `echo -n "blink" > /dev/<device>`
    
    5. Change LED brightness (if LED has capability).
        
        1. Write a 4 with a space and value between 0 and 100 (inclusive) for brightness value (percentage)
        
            - From the terminal enter command `echo -n 4 <value> > /dev/<device>`
        
        2. Write *br* (case-insensitive) with a space and value between 0 and 100 (inclusive) for brightness value (percentage)

            - From the terminal enter command `echo -n "br <value>" > /dev/<device>`
  
## Module Installation Order

1. `custom-gpio-driver.ko`

2. `custom-pwm-driver.ko`

3. `custom-led-driver.ko`