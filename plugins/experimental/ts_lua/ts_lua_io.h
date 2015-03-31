
#ifndef _TS_LUA_IO_H
#define _TS_LUA_IO_H

#include <ts/ts.h>
#include <string.h>

int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);

#endif
