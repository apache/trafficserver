#ifndef _TS_CRC32_H_INCLUDED_
#define _TS_CRC32_H_INCLUDED_

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

extern uint32_t *ts_crc32_table_short;
extern uint32_t ts_crc32_table256[];

int ts_crc32_table_init(void);

static inline uint32_t
ts_crc32_short(unsigned char *p, size_t len)
{
  unsigned char c;
  uint32_t crc;

  crc = 0xffffffff;

  if (ts_crc32_table_short == NULL) {
    ts_crc32_table_init();
  }

  while (len--) {
    c   = *p++;
    crc = ts_crc32_table_short[(crc ^ (c & 0xf)) & 0xf] ^ (crc >> 4);
    crc = ts_crc32_table_short[(crc ^ (c >> 4)) & 0xf] ^ (crc >> 4);
  }

  return crc ^ 0xffffffff;
}

static inline uint32_t
ts_crc32_long(unsigned char *p, size_t len)
{
  uint32_t crc;

  crc = 0xffffffff;

  while (len--) {
    crc = ts_crc32_table256[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  }

  return crc ^ 0xffffffff;
}

#define ts_crc32_init(crc) crc = 0xffffffff

static inline void
ts_crc32_update(uint32_t *crc, unsigned char *p, size_t len)
{
  uint32_t c;

  c = *crc;

  while (len--) {
    c = ts_crc32_table256[(c ^ *p++) & 0xff] ^ (c >> 8);
  }

  *crc = c;
}

#define ts_crc32_final(crc) crc ^= 0xffffffff

#endif /* _TS_CRC32_H_INCLUDED_ */
