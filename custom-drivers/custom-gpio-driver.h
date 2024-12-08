#include <linux/kernel.h>

typedef enum
{
  PWM_0,
  PWM_1,
  NOT_PWM
} pwm_t;

int gpio_output_ctl(uint32_t pin_num, bool do_set);
int gpio_set_pin_to_output(uint32_t pin_num, bool is_on_initially);
pwm_t gpio_is_pin_pwm(uint32_t pin_num);
int gpio_set_pin_to_pwm(uint32_t pin_num, uint32_t initial_val);