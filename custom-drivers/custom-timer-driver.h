/***************    Macros    ***************/


/***************    Type definitions    ***************/
typedef int (*timer_callback_t)(int);

// typedef struct timer_device_callback_s
// {
//   dev_t dev_id;
//   timer_callback_t callback;
// } timer_device_callback_t;

// This isn't a perfect concept, but it is good enough (and probably far more than enough)
// for what I am doing.



/***************    Function declarations    ***************/
int register_timer_dev_cb(timer_device_callback_t);
int unregister_timer_dev_cb(dev_t dev_id, int func_id);

