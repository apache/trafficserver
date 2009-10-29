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


#include "I_EventSystem.h"
#include "diags.i"
/**
  * some results
SunOS tsdev 5.6 Generic_105181-19 sun4u sparc SUNW,Ultra-4
1,000,000 random numbers
1960 msec (radix sort)
sorting nums qsort
3930 msec (qsort)

10,000,000 random numbers
21090 msec (radix sort)
47010 msec (qsort)

100,000 random numbers
80 msec (radix sort)
390 msec (qsort)

*/

#include <sys/time.h>

void
test1()
{
  int i;
  RadixSort s(4, 1000);
#define ADD(x) i=x; s.add((char *)&i)
  ADD(0);
  ADD(10);
  ADD(5);
  ADD(3);
  s.sort();
  s.iterStart();
#define CHECK(x) ink_assert(0 != s.iter((char*)&i) && i == x)
  CHECK(0);
  CHECK(3);
  CHECK(5);
  CHECK(10);
}

int
sort(void *a, void *b)
{
  int *ia = (int *) a;
  int *ib = (int *) b;
  if (*ia > *ib)
    return 1;
  if (*ia < *ib)
    return -1;
  return 0;
}

void
test2()
{
  int i;
#define NTEST2 300
  int tosort[NTEST2 + 1];

  RadixSort s(4, 1000);
  for (i = 0; i < NTEST2; i++) {
    tosort[i] = lrand48();
    s.add((char *) &tosort[i]);
  }
  //printf("Before sorting\n");
  //s.dumpBuckets();
  s.sort();
  //printf("After sorting\n");
  //s.dumpBuckets();

  // quicksort tosort and compare.

  qsort((char *) &tosort[0], NTEST2, 4, sort);
  s.iterStart();
  for (i = 0; i < NTEST2; i++) {
    int c;
    ink_assert(s.iter((char *) &c) != 0);
    ink_assert(c == tosort[i]);
  }

}

#define BENCH_NUM 1000000
int *tosort;
void
test3()
{
  // "benchmark"
  RadixSort s(4, BENCH_NUM + 3);
  tosort = (int *) xmalloc(sizeof(int) * BENCH_NUM);

  printf("adding nums\n");
  for (int i = 0; i < BENCH_NUM; i++) {
    int n = lrand48();
    tosort[i] = n;
    s.add((char *) &n);
  }
  printf("sorting nums\n");
  hrtime_t start = gethrvtime();
  s.sort();
  hrtime_t end = gethrvtime();
  printf("%lld msec (radix sort)\n", ink_hrtime_to_msec(end - start));

  printf("sorting nums qsort\n");
  start = gethrvtime();
  qsort((char *) tosort, BENCH_NUM, 4, sort);
  end = gethrvtime();
  printf("%lld msec (qsort)\n", ink_hrtime_to_msec(end - start));

}

int
main(int argc, char *argv[])
{
  (void) argc;
  (void) argv;
  int num_net_threads = ink_number_of_processors();
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  srand48(time(NULL));

  test1();
  test2();
  test3();
}
