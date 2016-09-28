/*
  This algorithm is in the public domain. This code was
  derived from code in the public domain.

  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#include "ts/HashFNV.h"

static const uint32_t FNV_INIT_32 = 0x811c9dc5u;
static const uint64_t FNV_INIT_64 = 0xcbf29ce484222325ull;

// FNV-1a 64bit
ATSHash32FNV1a::ATSHash32FNV1a(void)
{
  this->clear();
}

bool
ATSHash32FNV1a::final(void)
{
  return true;
}

uint32_t
ATSHash32FNV1a::get(void) const
{
  return hval;
}

bool
ATSHash32FNV1a::clear(void)
{
  hval = FNV_INIT_32;
  return true;
}

// FNV-1a 64bit
ATSHash64FNV1a::ATSHash64FNV1a(void)
{
  this->clear();
}

bool
ATSHash64FNV1a::final(void)
{
  return true;
}

uint64_t
ATSHash64FNV1a::get(void) const
{
  return hval;
}

bool
ATSHash64FNV1a::clear(void)
{
  hval = FNV_INIT_64;
  return true;
}
