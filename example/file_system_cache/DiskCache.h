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

#include <InkAPI.h>
#include <inttypes.h>
#include <sys/types.h>
#include <openssl/md5.h>
#include <string>
#include <iostream>
#include <math.h>
#include <sys/stat.h>
#include <map>
#include <aio.h>
#include <queue>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

using namespace std;


/**
 * The basic type used for keys and values in cache.
 */
class datum
{
public:
  int operator<(const datum & x) const
  {
    uint64_t min = 0;
    if (x.dsize < dsize)
    {
      min = x.dsize;
    } else
    {
      min = dsize;
    }

    int rval = strncmp(dptr, x.dptr, min);

    if (rval > 0) {
      return 0;
    } else if (rval < 0) {
      return 1;
    } else {
      return (dsize < x.dsize);
    }

  }

  const char *dptr;
  uint64_t dsize;
};


/**
 * Object that maintains state of an open file in cache.
 */
class openFile
{
public:
  openFile():fd(-1), refcount(0), exclusive(false), truncated(false)
  {
    fd = -1;
  }
  openFile(int tmpFd, bool excl):fd(tmpFd), refcount(1), exclusive(excl), truncated(false)
  {
  }

  int fd;
  int32_t refcount;
  bool exclusive;
  bool truncated;
};


/**
 * Allocator template to new and return objects.
 */
template<class T> class Allocator {
public:
  Allocator() {
    pthread_mutex_init(&_mutex, NULL);
  }

  T *pop()
  {
    if (pthread_mutex_lock(&_mutex) != 0) {
      return 0;
    }
    T *x;
    if (_queue.size() == 0) {
      x = new T();
      //cout << "new" << endl;
    } else {
      x = _queue.front();
      _queue.pop();
      //cout << "old" << endl;
    }
    if (pthread_mutex_unlock(&_mutex) != 0) {
      abort();
    }
    return x;
  }

  void push(T * x)
  {
    if (pthread_mutex_lock(&_mutex) != 0) {
      abort();
    }
    _queue.push(x);
    if (pthread_mutex_unlock(&_mutex) != 0) {
      abort();
    }
  }


private:
  queue<T *>_queue;
  pthread_mutex_t _mutex;
};


/**
 * Simple filesystem cache.
 * A disk cache that uses the filesystem to cache objects.  There should only be one
 * cache object in use for a directory.
 */
class DiskCache
{
public:
  DiskCache();

  // synchronous methods
  int32_t read(const datum & key, datum & value, const uint64_t size, const uint64_t offset = 0);
  int32_t write(const datum & key, const datum & value);
  int32_t remove(const datum & key);
  int32_t lock(const datum & key, bool exclusive);
  int32_t unlock(const datum & key);

  // aio methods
  int32_t aioRead(const datum & key, datum & value, const uint64_t size, const uint64_t offset = 0);
  int32_t aioWrite(const datum & key, const datum & value);
  static void aioReadDone(sigval_t x);
  static void aioWriteDone(sigval_t x);

  // setters and getters
  void setTopDirectory(const string & directory)
  {
    _topDirectory = directory;
  }
  string getTopDirectory(const string & directory)
  {
    return _topDirectory;
  }
  void setNumberDirectories(const uint32_t directories);
  uint32_t getNumberDirectories() const
  {
    return _totalDirectories;
  }
  int64_t getSize(const datum & key);

  /// Makes the entire directory hierarchy
  int makeDirectories()
  {
    if (mkdir(_topDirectory.c_str(), 0755) != 0) {
      if (errno != EEXIST) {
        INKDebug("cache_plugin", "Couldn't create the top cache directory: %s", _topDirectory.c_str());
        INKError("cache_plugin", "Couldn't create the top cache directory: %s", _topDirectory.c_str());
        return 1;
      }
    }

    return _makeDirectoryRecursive(_topDirectory, _totalDirectories);
  }

private:
  // private helper methods
  int _getFileDescriptor(const datum & key, const bool exclusive, const bool truncating = false);
  void _makePath(const datum & key, string & path) const;
  int _makeDirectoryRecursive(const string & path, uint32_t numberDirectories) const;

  // private data members for keeping track of that cache
  map<datum, openFile> _openFiles;
  static const uint32_t _directoryWidth = 256;  /// XXX - *MUST* be a power of 2
  string _topDirectory;
  uint32_t _totalDirectories;
  uint32_t _directoryDepth;
  pthread_mutex_t _mutex;
  static Allocator<struct aiocb>_aioRequests;
};
