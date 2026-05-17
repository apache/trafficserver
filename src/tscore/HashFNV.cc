/*
  This algorithm is in the public domain. This code was
  derived from code in the public domain.

  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#include "tscore/HashFNV.h"

// FNV-1a 32bit
ATSHash32FNV1a::ATSHash32FNV1a() = default;

void
ATSHash32FNV1a::final()
{
}

uint32_t
ATSHash32FNV1a::get() const
{
  return hval;
}

void
ATSHash32FNV1a::clear()
{
  hval = fnv_init;
}

// FNV-1a 64bit
ATSHash64FNV1a::ATSHash64FNV1a() = default;
void
ATSHash64FNV1a::final()
{
}

uint64_t
ATSHash64FNV1a::get() const
{
  return hval;
}

void
ATSHash64FNV1a::clear()
{
  hval = fnv_init;
}
