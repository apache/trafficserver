/*
  This algorithm is in the public domain. This code was
  derived from code in the public domain.

  http://www.isthe.com/chongo/tech/comp/fnv/

  Currently implemented FNV-1a 32bit and FNV-1a 64bit
 */

#include "tscore/HashFNV.h"

static const uint32_t FNV_INIT_32 = 0x811c9dc5u;
static const uint64_t FNV_INIT_64 = 0xcbf29ce484222325ull;

// FNV-1a 64bit
ATSHash32FNV1a::ATSHash32FNV1a()
{
  this->clear();
}

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
  hval = FNV_INIT_32;
}

// FNV-1a 64bit
ATSHash64FNV1a::ATSHash64FNV1a()
{
  this->clear();
}
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
  hval = FNV_INIT_64;
}
