#ifndef CUSTOM_PWM_DRIVER_H
#define CUSTOM_PWM_DRIVER_H

typedef enum pwm_cycle_freq_e
{
  PWM_FREQ_4_kHZ = 4000,
  PWM_INVALID_FREQ = 0,
} pwm_cycle_freq_t;

int pwm_init_user_device(pwm_channel_t pwm_channel, int duty_cycle, pwm_cycle_freq_t cycle_freq, bool is_enabled_initially);
int pwm_set_duty_cycle(pwm_channel_t pwm_channel, int duty_cycle);
int pwm_enable(pwm_channel_t pwm_channel, bool do_enable);

#endif