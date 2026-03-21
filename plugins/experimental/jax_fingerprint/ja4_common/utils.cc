#include "utils.h"

constexpr int TRUNCATED_HASH_STRING_LENGTH = 6;

void
hash_stringify(char *out, const unsigned char *hash)
{
  for (int i = 0; i < TRUNCATED_HASH_STRING_LENGTH; ++i) {
    unsigned int h = hash[i] >> 4;
    unsigned int l = hash[i] & 0x0F;
    out[i * 2]     = h <= 9 ? ('0' + h) : ('a' + h - 10);
    out[i * 2 + 1] = l <= 9 ? ('0' + l) : ('a' + l - 10);
  }
}
