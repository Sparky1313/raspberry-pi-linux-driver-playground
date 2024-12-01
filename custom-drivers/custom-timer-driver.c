#include <linux/init.h>
#include <linux/module.h>
// #include <linux/sort.h>
#include <linux/bsearch.h>
#include <asm/io.h>

#include "custom-timer-driver.h"
// #include "custom-errno.h"


/***************    Macros    ***************/
#define MAX_TIMER_CALLBACKS               20
#define LOWEST_CALLBACK_PRIORITY_NUM      (MAX_TIMER_CALLBACKS - 1)

/***************    Type definitions    ***************/

typedef struct timer_device_callback_s
{
  // The actual device number this callback should apply to.
  dev_t dev_id;
  
  // A function id determined by a kernel module. 
  // Future use is to use dev_id and func_id as keys for
  // setting priority of functions to run.
  int func_id;

  // The priority for a callback to be run at. Used
  // in the future to support an orchestrator that can manage the
  // priorities of the functions to run at.
  //
  // Priorities can be shared, in which case, no specific order is guaranteed
  // between the numbers that are shared.
  //
  // A negative value means just add to callbacks at any open priority.
  //
  // Lower the number (down to zero, the higher the priority)
  // 
  // If the number is greater than the number of callbacks allowed
  // internally or the number is negative, then the priority is set to lowest priority possible 
  // (NOTE *possible* not *available*).
  // For example, if the max number of callbacks is 20, and the priority provided is -5 or 22,
  // then the priority will ultimately be assigned to 20.
  int priority;            
  
  // The actual pointer to the callback function.
  timer_callback_t callback;
} timer_device_callback_t;

typedef struct timer_dev_cbs_wrapper_s
{
  uint32_t const max_cb_cnt;
  uint32_t const lowest_priority_num;
  uint32_t registered_cb_cnt;
  uint32_t first_cb_index;    // The one with the highest priority
  timer_device_callback_t * const * const p_timer_dev_cbs;    // A pointer to an array of callbacks
} timer_dev_cbs_wrapper_t;


/***************    Function declarations    ***************/

inline static bool are_timer_cbs_full(timer_dev_cbs_wrapper_t * dev_cbs_wrapper);
inline static void add_timer_callback(dev_t dev_id, int func_id, int priority,
                                      timer_callback_t callback);
inline static void delete_timer_callback(timer_device_callback_t *dev_cb);

static void init_timer_device_callbacks(void);



/***************    Private variables    ***************/

static timer_device_callback_t timer_dev_callbacks[MAX_TIMER_CALLBACKS];

timer_dev_cbs_wrapper_t dev_cbs_wrapper =
{
  .max_cb_cnt = MAX_TIMER_CALLBACKS;
  .lowest_priority_num = LOWEST_CALLBACK_PRIORITY_NUM;
  .registered_cb_cnt = 0;
  .first_cb_index = MAX_TIMER_CALLBACKS - 1;
  .p_timer_dev_cbs = &timer_dev_callbacks;   // Point to the start of the array of callbacks
};


/***************    Function Definitions    ***************/

// NOTE: This function is not thread-safe by itself.
//       It is a helper function that is meant to be used inside functions 
//       that already protect the data with a mutex.
//       
//       Mutexes are not recursive in the kernel, so you simply can't "add" a mutex to it
//       to protect it when it acts as a helper function.
//
//       I chose to to leave the function in since it is convenient instead of hard coding it
//       in spots throughout the code.
// 
//       Make sure you know what you are doing and use at your own risk.  
static void init_timer_device_callbacks(void)
{
  timer_device_callback_t *dev_cb = NULL;

  for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++)
  {
    dev_cb = &(dev_cbs_wrapper.p_timer_dev_cbs[i]);

    dev_cb->dev_id = 0;
    dev_cb->func_id = -1;
    dev_cb->priority = -1;
    dev_cb->callback = NULL;
  }
}

// NOTE: This function is not thread-safe by itself.
//       It is a helper function that is meant to be used inside functions 
//       that already protect the data with a mutex.
//       
//       Mutexes are not recursive in the kernel, so you simply can't "add" a mutex to it
//       to protect it when it acts as a helper function.
//
//       I chose to to leave the function in since it is convenient instead of hard coding it
//       in spots throughout the code.
// 
//       Make sure you know what you are doing and use at your own risk.      
inline static int add_timer_callback(dev_t dev_id, int func_id, int priority,
                                      timer_callback_t callback)
{
  // Return an error indicating that this callback couldn't be added
  // because the limit for registered callbacks has been reached.
  if (are_timer_cbs_full(&dev_cbs_wrapper))
  {
    return -ECBFULL;
  }
  
  // Get the next free callback slot
  timer_device_callback_t *dev_cb = &(dev_cbs_wrapper.p_timer_dev_cbs[dev_cbs_wrapper.registered_cb_cnt]);
  dev_cb->dev_id = dev_id;
  dev_cb->func_id = func_id;
  dev_cb->priority = priority;
  dev_cb->callback = callback;

  dev_cbs_wrapper.registered_cb_cnt++;
  
  // TODO: call a sort here

  dev_cbs_wrapper.first_cb_index++;

  return ENONE;
}

// NOTE: This function is not thread-safe by itself.
//       It is a helper function that is meant to be used inside functions 
//       that already protect the data with a mutex.
//       
//       Mutexes are not recursive in the kernel, so you simply can't "add" a mutex to it
//       to protect it when it acts as a helper function.
//
//       I chose to to leave the function in since it is convenient instead of hard coding it
//       in spots throughout the code.
// 
//       Make sure you know what you are doing and use at your own risk. 
inline static void delete_timer_callback(timer_device_callback_t *dev_cb)
{
  dev_cb->dev_id = 0;
  dev_cb->func_id = -1;
  dev_cb->priority = -1;
  dev_cb->callback = NULL;

  dev_cbs_wrapper.registered_cb_cnt++;
  
  // TODO: call a sort here

  dev_cbs_wrapper.first_cb_index++;

  return ENONE;
}

// NOTE: This function is not thread-safe by itself.
//       It is a helper function that is meant to be used inside functions 
//       that already protect the data with a mutex.
//       
//       Mutexes are not recursive in the kernel, so you simply can't "add" a mutex to it
//       to protect it when it acts as a helper function.
//
//       I chose to to leave the function in since it is convenient instead of hard coding it
//       in spots throughout the code.
// 
//       Make sure you know what you are doing and use at your own risk.  
inline static bool are_timer_cbs_full(timer_dev_cbs_wrapper_t * cbs_wrapper)
{
  return (cbs_wrapper.max_cb_cnt <= cbs_wrapper->registered_cb_cnt);
}


int register_timer_dev_cb(dev_t dev_id, int func_id, int priority,
                           timer_callback_t callback)
{
  // TODO: Need some sort of mutex around this
  //       for when this is updated while the timer is running
  //       and a callback function could be executed in a bottom-half irq thread.
  
  // Validate the arguments
  if (0 >= dev_id)
  {
    return -EINVAL;
  }

  if (0 > func_id)
  {
    return -EINVAL;
  }

  if (NULL == callback)
  {
    return -EINVAL;
  }

  // Set priority to lowest if a valid priority was not passed in
  if ( (0 > priority) || (dev_cbs_wrapper.lowest_priority_num < priority))
  {
    priority = dev_cbs_wrapper.lowest_priority_num;
  }

  int error = add_timer_callback(dev_id, func_id, priority, callback);

  return error;
}

int unregister_timer_dev_cb(dev_t dev_id, int func_id)
{

}

