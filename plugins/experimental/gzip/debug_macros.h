#ifndef _DBG_MACROS_H
#define _DBG_MACROS_H

#include <ts/ts.h>

#define TAG "gzip"

#define debug(fmt, args...) do {                                    \
  TSDebug(TAG, "DEBUG: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  } while (0)

#define info(fmt, args...) do {                                    \
  TSDebug(TAG, "INFO: " fmt, ##args ); \
  } while (0)

#define warning(fmt, args...) do {                                    \
  TSDebug(TAG, "WARNING: " fmt, ##args ); \
} while (0)

#define error(fmt, args...) do {                                    \
  TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  TSDebug(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
} while (0)

#define fatal(fmt, args...) do {                                    \
  TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  TSDebug(TAG, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__ , ##args ); \
  exit(-1); \
} while (0)


#endif //_DBG_MACROS_H
