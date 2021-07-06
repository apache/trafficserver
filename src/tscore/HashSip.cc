/**

Algorithm Info:
https://131002.net/siphash/

Based off of implementation:
https://github.com/floodyberry/siphash

 */

#include "tscore/HashSip.h"
#include <cstring>

using namespace std;

#define SIP_BLOCK_SIZE 8

#define ROTL64(a, b) (((a) << (b)) | ((a) >> (64 - b)))

#define U8TO64_LE(p) *(const uint64_t *)(p)

#define SIPCOMPRESS(x0, x1, x2, x3) \
  x0 += x1;                         \
  x2 += x3;                         \
  x1 = ROTL64(x1, 13);              \
  x3 = ROTL64(x3, 16);              \
  x1 ^= x0;                         \
  x3 ^= x2;                         \
  x0 = ROTL64(x0, 32);              \
  x2 += x1;                         \
  x0 += x3;                         \
  x1 = ROTL64(x1, 17);              \
  x3 = ROTL64(x3, 21);              \
  x1 ^= x2;                         \
  x3 ^= x0;                         \
  x2 = ROTL64(x2, 32);

ATSHash64Sip24::ATSHash64Sip24()
{
  this->clear();
}

ATSHash64Sip24::ATSHash64Sip24(const unsigned char key[16]) : k0(U8TO64_LE(key)), k1(U8TO64_LE(key + sizeof(k0)))
{
  this->clear();
}

ATSHash64Sip24::ATSHash64Sip24(uint64_t key0, uint64_t key1) : k0(key0), k1(key1)
{
  this->clear();
}

void
ATSHash64Sip24::update(const void *data, size_t len)
{
  size_t i, blocks;
  unsigned char *m;
  uint64_t mi;
  uint8_t block_off = 0;

  if (!finalized) {
    m = (unsigned char *)data;
    total_len += len;

    if (len + block_buffer_len < SIP_BLOCK_SIZE) {
      memcpy(block_buffer + block_buffer_len, m, len);
      block_buffer_len += len;
    } else {
      if (block_buffer_len > 0) {
        block_off = SIP_BLOCK_SIZE - block_buffer_len;
        memcpy(block_buffer + block_buffer_len, m, block_off);

        mi = U8TO64_LE(block_buffer);
        v3 ^= mi;
        SIPCOMPRESS(v0, v1, v2, v3);
        SIPCOMPRESS(v0, v1, v2, v3);
        v0 ^= mi;
      }

      for (i = block_off, blocks = ((len - block_off) & ~(SIP_BLOCK_SIZE - 1)); i < blocks; i += SIP_BLOCK_SIZE) {
        mi = U8TO64_LE(m + i);
        v3 ^= mi;
        SIPCOMPRESS(v0, v1, v2, v3);
        SIPCOMPRESS(v0, v1, v2, v3);
        v0 ^= mi;
      }

      block_buffer_len = (len - block_off) & (SIP_BLOCK_SIZE - 1);
      memcpy(block_buffer, m + block_off + blocks, block_buffer_len);
    }
  }
}

void
ATSHash64Sip24::final()
{
  uint64_t last7;
  int i;

  if (!finalized) {
    last7 = static_cast<uint64_t>(total_len & 0xff) << 56;

    for (i = block_buffer_len - 1; i >= 0; i--) {
      last7 |= static_cast<uint64_t>(block_buffer[i]) << (i * 8);
    }

    v3 ^= last7;
    SIPCOMPRESS(v0, v1, v2, v3);
    SIPCOMPRESS(v0, v1, v2, v3);
    v0 ^= last7;
    v2 ^= 0xff;
    SIPCOMPRESS(v0, v1, v2, v3);
    SIPCOMPRESS(v0, v1, v2, v3);
    SIPCOMPRESS(v0, v1, v2, v3);
    SIPCOMPRESS(v0, v1, v2, v3);
    hfinal    = v0 ^ v1 ^ v2 ^ v3;
    finalized = true;
  }
}

uint64_t
ATSHash64Sip24::get() const
{
  if (finalized) {
    return hfinal;
  } else {
    return 0;
  }
}

void
ATSHash64Sip24::clear()
{
  v0               = k0 ^ 0x736f6d6570736575ull;
  v1               = k1 ^ 0x646f72616e646f6dull;
  v2               = k0 ^ 0x6c7967656e657261ull;
  v3               = k1 ^ 0x7465646279746573ull;
  finalized        = false;
  total_len        = 0;
  block_buffer_len = 0;
}
