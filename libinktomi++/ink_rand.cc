/** @file

  Mersenne Twister definitions adapted for Traffic Server

  @section license License

  Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. The names of its contributors may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Any feedback is very welcome.
  http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
  email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)

  @section details Details

  A C-program for MT19937, with initialization improved 2002/2/10.
  Coded by Takuji Nishimura and Makoto Matsumoto.  This is a faster
  version by taking Shawn Cokus's optimization, Matthe Bellew's
  simplification, Isaku Wada's real version.

  @see http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html

*/

#include "inktomi++.h"


#define N              (624)    // length of state vector
#define M              (397)    // a period parameter
#define K              (0x9908B0DFU)    // a magic constant
#define hiBit(u)       ((u) & 0x80000000U)      // mask all but highest   bit of u
#define loBit(u)       ((u) & 0x00000001U)      // mask all but lowest    bit of u
#define loBits(u)      ((u) & 0x7FFFFFFFU)      // mask     the highest   bit of u
#define mixBits(u, v)  (hiBit(u)|loBits(v))     // move hi bit of u to hi bit of v


InkRand::InkRand(inku32 d)
{
  seed(d);
  next = state + 1;             // settting next to the same as reload()
}

/**
  Wrapper class around MT functionality.

  We initialize state[0..(N-1)] via the generator:

  @code
  x_new = (69069 * x_old) mod 2^32
  @endcode

  from Line 15 of Table 1, p. 106, Sec. 3.3.4 of Knuth's
  _The Art of Computer Programming_, Volume 2, 3rd ed.

  Notes (SJC): I do not know what the initial state requirements
  of the Mersenne Twister are, but it seems this seeding generator
  could be better.  It achieves the maximum period for its modulus
  (2^30) iff x_initial is odd (p. 20-21, Sec. 3.2.1.2, Knuth); if
  x_initial can be even, you have sequences like 0, 0, 0, ...;
  2^31, 2^31, 2^31, ...; 2^30, 2^30, 2^30, ...; 2^29, 2^29 + 2^31,
  2^29, 2^29 + 2^31, ..., etc. so I force seed to be odd below.

  Even if x_initial is odd, if x_initial is 1 mod 4 then

  @verbatim
  the          lowest bit of x is always 1,
  the  next-to-lowest bit of x is always 0,
  the 2nd-from-lowest bit of x alternates      ... 0 1 0 1 0 1 0 1 ... ,
  the 3rd-from-lowest bit of x 4-cycles        ... 0 1 1 0 0 1 1 0 ... ,
  the 4th-from-lowest bit of x has the 8-cycle ... 0 0 0 1 1 1 1 0 ... ,
  ...
  @endverbatim

  and if x_initial is 3 mod 4 then

  @verbatim
  the          lowest bit of x is always 1,
  the  next-to-lowest bit of x is always 1,
  the 2nd-from-lowest bit of x alternates      ... 0 1 0 1 0 1 0 1 ... ,
  the 3rd-from-lowest bit of x 4-cycles        ... 0 0 1 1 0 0 1 1 ... ,
  the 4th-from-lowest bit of x has the 8-cycle ... 0 0 1 1 1 1 0 0 ... ,
  ...
  @endverbatim

  The generator's potency (min. s>=0 with (69069-1)^s = 0 mod 2^32) is
  16, which seems to be alright by p. 25, Sec. 3.2.1.3 of Knuth.  It
  also does well in the dimension 2..5 spectral tests, but it could be
  better in dimension 6 (Line 15, Table 1, p. 106, Sec. 3.3.4, Knuth).

  Note that the random number user does not see the values generated
  here directly since reloadMT() will always munge them first, so maybe
  none of all of this matters.  In fact, the seed values made here could
  even be extra-special desirable if the Mersenne Twister theory says
  so-- that's why the only change I made is to restrict to odd seeds.

*/
void
InkRand::seed(inku32 d)
{
  register inku32 x = (d | 1U) & 0xFFFFFFFFU, *s = state;
  register int j;

  for (left = 0, *s++ = x, j = N; --j; *s++ = (x *= 69069U) & 0xFFFFFFFFU);
}

inku32 InkRand::random()
{
  inku32
    y;

  if (--left < 0)
    return (reload());

  y = *next++;
  y ^= (y >> 11);
  y ^= (y << 7) & 0x9D2C5680U;
  y ^= (y << 15) & 0xEFC60000U;
  return (y ^ (y >> 18));
}

double
InkRand::drandom()
{
  return ((double) random() * 2.3283064370807974e-10);
  // return ((double) random () / (double) 0xffffffff);
}

inku32 InkRand::reload()
{
  register inku32 *
    p0 = state, *p2 = state + 2, *pM = state + M, s0, s1;
  register int
    j;

  left = N - 1, next = state + 1;

  for (s0 = state[0], s1 = state[1], j = N - M + 1; --j; s0 = s1, s1 = *p2++)
    *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);

  for (pM = state, j = M; --j; s0 = s1, s1 = *p2++)
    *p0++ = *pM++ ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);

  s1 = state[0], *p0 = *pM ^ (mixBits(s0, s1) >> 1) ^ (loBit(s1) ? K : 0U);
  s1 ^= (s1 >> 11);
  s1 ^= (s1 << 7) & 0x9D2C5680U;
  s1 ^= (s1 << 15) & 0xEFC60000U;
  return (s1 ^ (s1 >> 18));
}

int
ink_rand_r(inku32 * p)
{
  return (((*p) = (*p) * 1103515245 + 12345) % ((inku32) 0x7fffffff + 1));
}

