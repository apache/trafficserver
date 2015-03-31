
#include "ts_lua_io.h"

int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
  int64_t avail, need, n;
  const char *start;
  TSIOBufferBlock blk;

  n = 0;
  blk = TSIOBufferReaderStart(readerp);

  while (blk) {
    start = TSIOBufferBlockReadStart(blk, readerp, &avail);
    need = length < avail ? length : avail;

    if (need > 0) {
      memcpy((char *)buf + n, start, need);
      length -= need;
      n += need;
    }

    if (length == 0)
      break;

    blk = TSIOBufferBlockNext(blk);
  }

  return n;
}
