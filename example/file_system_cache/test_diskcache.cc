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


#include <iostream>
#include "DiskCache.h"
#include <assert.h>

#define SIZE 65536
#define LOOP 1000

void
INKDebug(const char *tag, const char *format_str, ...)
{
}

void
INKError(const char *format_str, ...)
{
}


char *
createRandomBuffer(uint32_t x)
{
  char *buffer = (char *) malloc(x);

  for (int i = 0; i < x; ++i) {
    int number = rand();
    buffer[x] = (char) number;
  }
  return buffer;
}


//----------------------------------------------------------------------------
void
cacheWrite(DiskCache & cache)
{
  datum key;
  key.dptr = "foo";
  key.dsize = 3;
  datum value;
  value.dptr = createRandomBuffer(SIZE);
  value.dsize = SIZE;

  for (int i = 0; i < LOOP; ++i) {
    assert(cache.lock(key, 1) == 0);
    assert(cache.remove(key) == 0);
    assert(cache.write(key, value) == 0);
    assert(cache.unlock(key) == 0);
  }
}


//----------------------------------------------------------------------------
void
cacheRead(DiskCache & cache)
{
  datum key;
  key.dptr = "foo";
  key.dsize = 3;
  datum value;
  value.dptr = (char *) malloc(SIZE);
  value.dsize = SIZE;

  for (int i = 0; i < LOOP; ++i) {
    cache.lock(key, 0);
    cache.read(key, value, SIZE, 0);
    assert(value.dsize == SIZE);
    cache.unlock(key);
  }
}


//----------------------------------------------------------------------------
void
cacheAioWrite(DiskCache & cache)
{
  datum key;
  key.dptr = "foo";
  key.dsize = 3;
  datum value;
  value.dptr = createRandomBuffer(SIZE);
  value.dsize = SIZE;

  for (int i = 0; i < LOOP; ++i) {
    cache.lock(key, 1);
    cache.remove(key);
    cache.aioWrite(key, value);
    cache.unlock(key);
  }
}


//----------------------------------------------------------------------------
void
cacheAioRead(DiskCache & cache)
{
  datum key;
  key.dptr = "foo";
  key.dsize = 3;
  datum value;
  value.dptr = (char *) malloc(SIZE);
  value.dsize = SIZE;

  for (int i = 0; i < LOOP; ++i) {
    cache.lock(key, 0);
    cache.aioRead(key, value, SIZE, 0);
    assert(value.dsize == SIZE);
    cache.unlock(key);
  }
}



//----------------------------------------------------------------------------
void
exclusiveLock(DiskCache & cache)
{
  datum key;
  key.dptr = "foo";
  key.dsize = 3;
  datum value;
  value.dptr = "bar";
  value.dsize = 3;

  cache.lock(key, true);
  assert(cache.lock(key, true) == -1);

  assert(cache.remove(key) == 0);
  assert(cache.write(key, value) == 0);
  assert(cache.write(key, value) == 0);
  assert(cache.unlock(key) == 0);
  assert(cache.unlock(key) == -1);
}


//----------------------------------------------------------------------------
void
setDirs(DiskCache & cache)
{
  cache.setNumberDirectories(2);
  assert(cache.getNumberDirectories() == 256);
}


//----------------------------------------------------------------------------
int
main()
{
  DiskCache cache;
  setDirs(cache);
  cache.setTopDirectory("/tmp/cache");
  //  cache.makeDirectories();

  time_t start = time(NULL);
  cout << "start:     " << start << endl;

  cacheAioWrite(cache);
  time_t stop = time(NULL);
  cout << "aio write: " << stop - start << endl;

  start = stop;
  cacheWrite(cache);
  stop = time(NULL);
  cout << "write:     " << stop - start << endl;

  start = stop;
  cacheAioRead(cache);
  stop = time(NULL);
  cout << "aio read:  " << stop - start << endl;

  start = stop;
  cacheRead(cache);
  stop = time(NULL);
  cout << "read:      " << stop - start << endl;

  return 0;
}
