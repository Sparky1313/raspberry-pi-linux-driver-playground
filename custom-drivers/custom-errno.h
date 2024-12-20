#ifndef CUSTOM_ERRNO_H
#define CUSTOM_ERRNO_H

// Error defines (we use numbers not included in errno.h and less than 4095 or "MAX_ERRNO" in err.h)
// This means we try to pick numbers between 1000 and 4095
#define ENONE         0         /* No error */
#define EINTERNAL     1000      /* Internal error */
#define EINVPIN       1001      /* Invalid pin */
#define EINVREG       1002      /* Invalid register access */
#define EMAPPING      1003      /* Issue with mapping of memory */
#define EUNSUPCMD     1004      /* Unsupported command */
#define ECBFULL       1005      /* Callbacks full */
#define EINVFUNC      1006      /* Invalid functionality requested */

#endif