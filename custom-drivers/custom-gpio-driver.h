#ifndef CUSTOM_GPIO_DRIVER_H
#define CUSTOM_GPIO_DRIVER_H

#include "custom-driver-shared-info.h"

int gpio_output_ctl(uint32_t pin_num, bool do_set);
int gpio_set_pin_to_output(uint32_t pin_num, bool is_on_initially);
pwm_channel_t gpio_is_pin_pwm(uint32_t pin_num);
int gpio_set_pin_to_pwm(uint32_t pin_num);

#endif