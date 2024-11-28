#include <linux/kernel.h>

int gpio_output_ctl(uint32_t pin_num, bool do_set);
int gpio_set_pin_to_output(uint32_t pin_num, bool is_on_initially);