#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

#define USE_OPTIMIZE_PRINTF

#ifndef LOCAL_CONFIG_AVAILABLE
#error Please copy user_config.sample.h to user_config.local.h and modify your configurations
#else
#include "user_config.local.h"
#endif

#endif

