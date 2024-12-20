#ifndef CUSTOM_DRIVER_SHARED_INFO_H
#define CUSTOM_DRIVER_SHARED_INFO_H

#define BCM2837_PERI_BASE     (0x3F000000)

typedef enum pwm_channel_e
{
  PWM_0,
  PWM_1,
  NOT_PWM
} pwm_channel_t;

#endif