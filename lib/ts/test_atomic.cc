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

#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <poll.h>
#include <pthread.h>

#include "ts/ink_atomic.h"
#include "ts/ink_queue.h"
#include "ts/ink_thread.h"

#ifndef LONG_ATOMICLIST_TEST

#define MAX_ALIST_TEST 10
#define MAX_ALIST_ARRAY 100000
InkAtomicList al[MAX_ALIST_TEST];
void *al_test[MAX_ALIST_TEST][MAX_ALIST_ARRAY];
volatile int al_done = 0;

void *
testalist(void *ame)
{
  int me = (int)(uintptr_t)ame;
  int j, k;
  for (k = 0; k < MAX_ALIST_ARRAY; k++) {
    ink_atomiclist_push(&al[k % MAX_ALIST_TEST], &al_test[me][k]);
  }
  void *x;
  for (j = 0; j < 1000000; j++) {
    if ((x = ink_atomiclist_pop(&al[me]))) {
      ink_atomiclist_push(&al[rand() % MAX_ALIST_TEST], x);
    }
  }
  ink_atomic_increment((int *)&al_done, 1);
  return nullptr;
}
#endif // !LONG_ATOMICLIST_TEST

#ifdef LONG_ATOMICLIST_TEST
/************************************************************************/
#define MAX_ATOMIC_LISTS (4 * 1024)
#define MAX_ITEMS_PER_LIST (1 * 1024)
#define MAX_TEST_THREADS 64
static InkAtomicList alists[MAX_ATOMIC_LISTS];
struct listItem *items[MAX_ATOMIC_LISTS * MAX_ITEMS_PER_LIST];

struct listItem {
  int data1;
  int data2;
  void *link;
  int data3;
  int data4;
  int check;
};

void
init_data()
{
  int j;
  int ali;
  struct listItem l;
  struct listItem *plistItem;

  for (ali = 0; ali < MAX_ATOMIC_LISTS; ali++)
    ink_atomiclist_init(&alists[ali], "alist", ((char *)&l.link - (char *)&l));

  for (ali = 0; ali < MAX_ATOMIC_LISTS; ali++) {
    for (j = 0; j < MAX_ITEMS_PER_LIST; j++) {
      plistItem        = (struct listItem *)malloc(sizeof(struct listItem));
      items[ali + j]   = plistItem;
      plistItem->data1 = ali + j;
      plistItem->data2 = ali + rand();
      plistItem->link  = 0;
      plistItem->data3 = j + rand();
      plistItem->data4 = ali + j + rand();
      plistItem->check = (plistItem->data1 ^ plistItem->data2 ^ plistItem->data3 ^ plistItem->data4);
      ink_atomiclist_push(&alists[ali], plistItem);
    }
  }
}

void
cycle_data(void *d)
{
  InkAtomicList *l;
  struct listItem *pli;
  struct listItem *pli_next;
  int iterations;
  int me;

  me         = (int)d;
  iterations = 0;

  while (1) {
    l = &alists[(me + rand()) % MAX_ATOMIC_LISTS];

    pli = (struct listItem *)ink_atomiclist_popall(l);
    if (!pli)
      continue;

    // Place listItems into random queues
    while (pli) {
      ink_assert((pli->data1 ^ pli->data2 ^ pli->data3 ^ pli->data4) == pli->check);
      pli_next  = (struct listItem *)pli->link;
      pli->link = 0;
      ink_atomiclist_push(&alists[(me + rand()) % MAX_ATOMIC_LISTS], (void *)pli);
      pli = pli_next;
    }
    iterations++;
    poll(0, 0, 10); // 10 msec delay
    if ((iterations % 100) == 0)
      printf("%d ", me);
  }
}

/************************************************************************/
#endif // LONG_ATOMICLIST_TEST

int
main(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
#ifndef LONG_ATOMICLIST_TEST
  int32_t m = 1, n = 100;
  // int64 lm = 1LL, ln = 100LL;
  const char *m2 = "hello";
  char *n2;

  printf("sizeof(int32_t)==%d   sizeof(void *)==%d\n", (int)sizeof(int32_t), (int)sizeof(void *));

  printf("CAS: %d == 1  then  2\n", m);
  n = ink_atomic_cas(&m, 1, 2);
  printf("changed to: %d,  result=%s\n", m, n ? "true" : "false");

  printf("CAS: %d == 1  then  3\n", m);
  n = ink_atomic_cas(&m, 1, 3);
  printf("changed to: %d,  result=%s\n", m, n ? "true" : "false");

  printf("CAS pointer: '%s' == 'hello'  then  'new'\n", m2);
  n = ink_atomic_cas(&m2, "hello", "new");
  printf("changed to: %s, result=%s\n", m2, n ? (char *)"true" : (char *)"false");

  printf("CAS pointer: '%s' == 'hello'  then  'new2'\n", m2);
  n = ink_atomic_cas(&m2, m2, "new2");
  printf("changed to: %s, result=%s\n", m2, n ? "true" : "false");

  n = 100;
  printf("Atomic Inc of %d\n", n);
  m = ink_atomic_increment((int *)&n, 1);
  printf("changed to: %d,  result=%d\n", n, m);

  printf("Atomic Fetch-and-Add 2 to pointer to '%s'\n", m2);
  n2 = (char *)ink_atomic_increment((pvvoidp)&m2, (void *)2);
  printf("changed to: %s,  result=%s\n", m2, n2);

  printf("Testing atomic lists\n");
  {
    int ali;
    srand(time(nullptr));
    printf("sizeof(al_test) = %d\n", (int)sizeof(al_test));
    memset(&al_test[0][0], 0, sizeof(al_test));
    for (ali = 0; ali < MAX_ALIST_TEST; ali++) {
      ink_atomiclist_init(&al[ali], "foo", 0);
    }
    for (ali = 0; ali < MAX_ALIST_TEST; ali++) {
      ink_thread tid;
      pthread_attr_t attr;

      pthread_attr_init(&attr);
#if !defined(freebsd)
      pthread_attr_setstacksize(&attr, 1024 * 1024);
#endif
      ink_assert(pthread_create(&tid, &attr, testalist, (void *)((intptr_t)ali)) == 0);
    }
    while (al_done != MAX_ALIST_TEST) {
      sleep(1);
    }
  }
#endif // !LONG_ATOMICLIST_TEST

#ifdef LONG_ATOMICLIST_TEST
  printf("Testing atomic lists (long version)\n");
  {
    int id;

    init_data();
    for (id = 0; id < MAX_TEST_THREADS; id++) {
      ink_assert(thr_create(NULL, 0, cycle_data, (void *)id, THR_NEW_LWP, NULL) == 0);
    }
  }
  while (1) {
    poll(0, 0, 10); // 10 msec delay
  }
#endif // LONG_ATOMICLIST_TEST

  return 0;
}
