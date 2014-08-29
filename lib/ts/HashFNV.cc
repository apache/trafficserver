/*
  This algorithm is in the public domain. This code was
  derived from code in the public domain.

  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#include "HashFNV.h"

#define FNV_INIT_32 ((uint32_t)0x811c9dc5)
#define FNV_INIT_64 ((uint64_t)0xcbf29ce484222325ULL)

// FNV-1a 64bit
ATSHash32FNV1a::ATSHash32FNV1a(void)
{
  this->clear();
}

void
ATSHash32FNV1a::update(const void *data, size_t len)
{
  uint8_t *bp = (uint8_t *) data;
  uint8_t *be = bp + len;

  while (bp < be) {
    hval ^= (uint32_t) *bp++;
    hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
  }
}

void
ATSHash32FNV1a::final(void)
{
}

uint32_t
ATSHash32FNV1a::get(void) const
{
  return hval;
}

void
ATSHash32FNV1a::clear(void)
{
  hval = FNV_INIT_32;
}

// FNV-1a 64bit
ATSHash64FNV1a::ATSHash64FNV1a(void)
{
  this->clear();
}

void
ATSHash64FNV1a::update(const void *data, size_t len)
{
  uint8_t *bp = (uint8_t *) data;
  uint8_t *be = bp + len;

  while (bp < be) {
    hval ^= (uint64_t) *bp++;
    hval += (hval << 1) + (hval << 4) + (hval << 5) + (hval << 7) + (hval << 8) + (hval << 40);
  }
}

void
ATSHash64FNV1a::final(void)
{
}

uint64_t
ATSHash64FNV1a::get(void) const
{
  return hval;
}

void
ATSHash64FNV1a::clear(void)
{
  hval = FNV_INIT_64;
}
