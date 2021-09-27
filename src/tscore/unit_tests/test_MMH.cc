/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "tscore/MMH.h"
#include "tscore/ink_memory.h"
#include <catch.hpp>

#define TEST_COLLISIONS 10000000

static int
xxcompar(const void *a, const void *b)
{
  int *const *x = static_cast<int *const *>(a);
  int *const *y = static_cast<int *const *>(b);
  for (int i = 0; i < 4; i++) {
    if (x[i] > y[i]) {
      return 1;
    }
    if (x[i] < y[i]) {
      return -1;
    }
  }
  return 0;
}

typedef uint32_t i4_t[4];
i4_t *xxh;
double *xf;

TEST_CASE("MMH", "[libts][MMH]")
{
  union {
    unsigned char hash[16];
    uint32_t h[4];
  } h;

  xxh = (i4_t *)ats_malloc(4 * sizeof(uint32_t) * TEST_COLLISIONS);
  xf  = (double *)ats_malloc(sizeof(double) * TEST_COLLISIONS);

  printf("test collisions\n");
  const char *sc1 = "http://npdev:19080/1.6664000000/4000";
  const char *sc2 = "http://npdev:19080/1.8666000000/4000";
  const char *sc3 = "http://:@npdev/1.6664000000/4000;?";
  const char *sc4 = "http://:@npdev/1.8666000000/4000;?";
  ink_code_MMH((unsigned char *)sc1, strlen(sc1), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc2, strlen(sc2), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc3, strlen(sc3), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH((unsigned char *)sc4, strlen(sc4), h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  srand48(time(nullptr));
  for (int xx = 0; xx < TEST_COLLISIONS; xx++) {
    char xs[256];
    xf[xx] = drand48();
    sprintf(xs, "http://@npdev/%16.14f/4000;?", xf[xx]);
    ink_code_MMH((unsigned char *)xs, strlen(xs), (unsigned char *)&xxh[xx]);
  }
  qsort(xxh, TEST_COLLISIONS, 16, xxcompar);
  for (int xy = 0; xy < TEST_COLLISIONS - 1; xy++) {
    if (xxh[xy][0] == xxh[xy + 1][0] && xxh[xy][1] == xxh[xy + 1][1] && xxh[xy][2] == xxh[xy + 1][2] &&
        xxh[xy][3] == xxh[xy + 1][3])
      printf("********** collision %d\n", xy);
  }

  ats_free(xf);
  ats_free(xxh);

  unsigned char *s       = (unsigned char *)MMH_x;
  int l                  = sizeof(MMH_x);
  unsigned char *s1      = (unsigned char *)ats_malloc(l + sizeof(uint32_t));
  unsigned char *free_s1 = s1;
  s1 += 1;
  memcpy(s1, s, l);
  unsigned char *s2      = (unsigned char *)ats_malloc(l + sizeof(uint32_t));
  unsigned char *free_s2 = s2;
  s2 += 2;
  memcpy(s2, s, l);
  unsigned char *s3      = (unsigned char *)ats_malloc(l + sizeof(uint32_t));
  unsigned char *free_s3 = s3;
  s3 += 3;
  memcpy(s3, s, l);

  printf("test alignment\n");
  ink_code_MMH(s, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s1, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s2, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  ink_code_MMH(s3, l, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  int i = 0;
  MMH_CTX c;
  unsigned char *t = s;
  printf("test chunking\n");
  ink_code_incr_MMH_init(&c);
  for (i = 0; i < 24; i++) {
    ink_code_incr_MMH_update(&c, (char *)t, i);
    t += i;
  }
  ink_code_incr_MMH_final((uint8_t *)h.hash, &c);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);
  int q = t - s;
  ink_code_MMH(s, q, h.hash);
  printf("%X %X %X %X\n", h.h[0], h.h[1], h.h[2], h.h[3]);

  FILE *fp = fopen("/dev/urandom", "r");
  char x[4096];
  int hist[256];
  memset(hist, 0, sizeof(hist));

  size_t total = 0;
  for (size_t xx = 0; ((xx = fread(x, 1, 128, fp)) == 128) && total < 1048576; total += xx) {
    ink_code_MMH((unsigned char *)x, 128, h.hash);
    hist[h.h[0] & 255]++;
    total += xx;
  }
  for (int z = 0; z < 256; z++) {
    printf("%6d ", hist[z]);
    if (!(z % 7))
      printf("\n");
  }
  printf("\n");

  ats_free(free_s1);
  ats_free(free_s2);
  ats_free(free_s3);
}
