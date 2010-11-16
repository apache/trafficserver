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

#include "DiskCache.h"
#include <fcntl.h>
#include <sys/file.h>
#include <aio.h>
#include <ts/ts.h>
#include <string.h>


Allocator<struct aiocb>
  DiskCache::_aioRequests;
//static char lookupTable[] = "0123456789ABCDEF";
static const char *
lookupTable[] =
  { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0A", "0B", "0C", "0D", "0E", "0F", "10", "11", "12",
"13", "14", "15", "16", "17", "18", "19", "1A", "1B", "1C", "1D", "1E", "1F", "20", "21", "22", "23", "24", "25", "26", "27",
"28", "29", "2A", "2B", "2C", "2D", "2E", "2F", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3A", "3B", "3C",
"3D", "3E", "3F", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4A", "4B", "4C", "4D", "4E", "4F", "50", "51",
"52", "53", "54", "55", "56", "57", "58", "59", "5A", "5B", "5C", "5D", "5E", "5F", "60", "61", "62", "63", "64", "65", "66",
"67", "68", "69", "6A", "6B", "6C", "6D", "6E", "6F", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7A", "7B",
"7C", "7D", "7E", "7F", "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8A", "8B", "8C", "8D", "8E", "8F", "90",
"91", "92", "93", "94", "95", "96", "97", "98", "99", "9A", "9B", "9C", "9D", "9E", "9F", "A0", "A1", "A2", "A3", "A4", "A5",
"A6", "A7", "A8", "A9", "AA", "AB", "AC", "AD", "AE", "AF", "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "BA",
"BB", "BC", "BD", "BE", "BF", "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "CA", "CB", "CC", "CD", "CE", "CF",
"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "DA", "DB", "DC", "DD", "DE", "DF", "E0", "E1", "E2", "E3", "E4",
"E5", "E6", "E7", "E8", "E9", "EA", "EB", "EC", "ED", "EE", "EF", "F0", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
"FA", "FB", "FC", "FD", "FE", "FF" };

//----------------------------------------------------------------------------
DiskCache::DiskCache():
_topDirectory("/tmp/cache"), _totalDirectories(256), _directoryDepth(1)
{
  pthread_mutex_init(&_mutex, NULL);
}


//----------------------------------------------------------------------------
int32_t
DiskCache::lock(const datum & key, const bool exclusive)
{
  // we combine the notion of locking and opening the file
  // this will make it so we are not having to reopen files for every
  // read and write operation
  // hopefully this is more benificial

  string path;
  _makePath(key, path);
  //cout << path << endl;
  bool needToOpen = false;

  // add the open file to the openFiles map if it not already there
  // lock the map
  if (pthread_mutex_lock(&_mutex) != 0) {
    TSDebug("cache_plugin", "[DiskCache::lock] can't get mutex");
    return -1;
  }
  // lookup
  map<datum, openFile>::iterator it = _openFiles.find(key);
  if (exclusive) {
    if (it != _openFiles.end()) {
      // unlock the map
      if (pthread_mutex_unlock(&_mutex) != 0) {
        abort();
      }
      TSDebug("cache_plugin", "[DiskCache::lock] file already opened with shared access");
      return -1;
    }
  } else {
    if (it != _openFiles.end()) {
      if (it->second.exclusive == true) {
        // unlock the map
        if (pthread_mutex_unlock(&_mutex) != 0) {
          abort();
        }
        TSDebug("cache_plugin", "[DiskCache::lock] file already opend with exclusive access");
        return -1;
      } else {
        it->second.refcount++;
        // unlock the map
        if (pthread_mutex_unlock(&_mutex) != 0) {
          abort();
        }
        return 0;
      }
    }
  }
  // unlock the map
  if (pthread_mutex_unlock(&_mutex) != 0) {
    abort();
  }

  int fd;
  if (exclusive) {
    fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0777);       // TODO should it be read/write?
  } else {
    fd = open(path.c_str(), O_RDONLY);
  }

  if (fd < 0) {
    TSDebug("cache_plugin", "[DiskCache::lock] can't open the file: %s", path.c_str());
    return -1;
  }
  // try to obtain the lock
  if (exclusive) {
    if (flock(fd, LOCK_EX || LOCK_NB) != 0) {
      TSDebug("cache_plugin", "[DiskCache::lock] can't get exclusive flock on file: %s", path.c_str());
      return -1;
    }
  } else {
    if (flock(fd, LOCK_EX || LOCK_NB) != 0) {
      TSDebug("cache_plugin", "[DiskCache::lock] can't get shared flock on file: %s", path.c_str());
      return -1;
    }
  }


  // lock the map
  if (pthread_mutex_lock(&_mutex) != 0) {
    return -1;
  }
  it = _openFiles.find(key);
  if (exclusive) {
    if (it != _openFiles.end()) {
      close(fd);
      // unlock the map
      if (pthread_mutex_unlock(&_mutex) != 0) {
        abort();
      }
      TSDebug("cache_plugin", "[DiskCache::lock] file already opened with shared access");
      return -1;
    } else {
      _openFiles[key] = openFile(fd, true);
    }
  } else {
    if (it != _openFiles.end()) {
      // someone was able to open it before us
      it->second.refcount++;
      close(fd);
    } else {
      _openFiles[key] = openFile(fd, false);
    }
  }
  // unlock the map
  if (pthread_mutex_unlock(&_mutex) != 0) {
    abort();
  }

  return 0;
}


//----------------------------------------------------------------------------
int32_t
DiskCache::unlock(const datum & key)
{
  // close and unlock the file
  // lock the map
  if (pthread_mutex_lock(&_mutex) != 0) {
    return -1;
  }
  map<datum, openFile>::iterator it = _openFiles.find(key);
  if (it == _openFiles.end()) {
    // unlock the map
    if (pthread_mutex_unlock(&_mutex) != 0) {
      abort();
    }
    return -1;
  } else {
    it->second.refcount--;
  }

  if (it->second.refcount == 0) {
    close(it->second.fd);
    if (it->second.truncated == true) {
      string path;
      _makePath(key, path);
      unlink(path.c_str());
    }
    _openFiles.erase(it);
  }
  // unlock the map
  if (pthread_mutex_unlock(&_mutex) != 0) {
    abort();
  }

  return 0;
}


//----------------------------------------------------------------------------
int
DiskCache::_getFileDescriptor(const datum & key, const bool exclusive, const bool truncating)
{
  int fd = -1;

  if (pthread_mutex_lock(&_mutex) != 0) {
    return -1;
  }
  map<datum, openFile>::iterator it = _openFiles.find(key);
  if (it != _openFiles.end()) {

    // check to see if we need to have exclusive access and that we locked
    // the key with exclusive access
    if ((exclusive == true && it->second.exclusive == true) || exclusive == false) {
      fd = it->second.fd;
      if (truncating == true) {
        it->second.truncated = true;
      } else {
        it->second.truncated = false;
      }
    }
  }
  if (pthread_mutex_unlock(&_mutex) != 0) {
    abort();
  }

  return fd;
}


//----------------------------------------------------------------------------
void
DiskCache::aioReadDone(sigval_t x)
{
  //cout << "read done" << endl;
  struct aiocb *aio = (aiocb *) x.sival_ptr;
  _aioRequests.push(aio);
}


//----------------------------------------------------------------------------
void
DiskCache::aioWriteDone(sigval_t x)
{
  //cout << "write done" << endl;
  struct aiocb *aio = (aiocb *) x.sival_ptr;
  _aioRequests.push(aio);
}


//----------------------------------------------------------------------------
int32_t
DiskCache::aioRead(const datum & key, datum & value, const uint64_t size, const uint64_t offset)
{
  // get the file descriptor from the open file map
  int fd = -1;
  if ((fd = _getFileDescriptor(key, false /* non-exclusive */ )) < 0) {
    TSDebug("cache_plugin", "[DiskCache::aioRead] can't find file descriptor");
    return -1;
  }
  // Set up the AIO request
  aiocb *aio = _aioRequests.pop();
  bzero((char *) aio, sizeof(struct aiocb));
  aio->aio_fildes = fd;
  aio->aio_buf = (void *) value.dptr;
  aio->aio_nbytes = size;
  aio->aio_offset = offset;

  // Link the AIO request with a callback
  aio->aio_sigevent.sigev_notify = SIGEV_THREAD;
  aio->aio_sigevent.sigev_notify_function = &aioReadDone;
  aio->aio_sigevent.sigev_value.sival_ptr = aio;

  return aio_read(aio);
}


//----------------------------------------------------------------------------
int32_t
DiskCache::aioWrite(const datum & key, const datum & value)
{
  // we only append to the file, we count on someone calling remove

  // get the file descriptor from the open file map
  int fd = -1;
  if ((fd = _getFileDescriptor(key, true)) < 0) {
    TSDebug("cache_plugin", "[DiskCache::aioWrite] can't find file descriptor");
    return -1;
  }
  // Set up the AIO request
  aiocb *aio = _aioRequests.pop();
  bzero((char *) aio, sizeof(struct aiocb));
  aio->aio_fildes = fd;
  aio->aio_buf = (void *) value.dptr;
  aio->aio_nbytes = value.dsize;

  // Link the AIO request with a callback
  aio->aio_sigevent.sigev_notify = SIGEV_THREAD;
  aio->aio_sigevent.sigev_notify_function = aioWriteDone;
  aio->aio_sigevent.sigev_value.sival_ptr = aio;

  return aio_write(aio);
}


//----------------------------------------------------------------------------
int64_t
DiskCache::getSize(const datum & key)
{
  // get the file descriptor from the open file map
  int fd = -1;
  if ((fd = _getFileDescriptor(key, true)) < 0) {
    return -1;
  }

  struct stat buf;

  fstat(fd, &buf);

  return buf.st_size;
}


//----------------------------------------------------------------------------
int32_t
DiskCache::write(const datum & key, const datum & value)
{
  // we only append to the file, we count on someone calling remove

  // get the file descriptor from the open file map
  int fd = -1;
  if ((fd = _getFileDescriptor(key, true)) < 0) {
    TSDebug("cache_plugin", "[DiskCache::write] can't find file descriptor");
    return -1;
  }
  //cout << "value size: " << value.dsize << endl;
  // try to write all the bytes, if not loop over till they are all written
  ssize_t bytesWritten = -1;
  ssize_t totalWritten = 0;
  while ((bytesWritten =::write(fd, (value.dptr + totalWritten), value.dsize - totalWritten)) > 0) {
    //cout << "bytesWritten: " << bytesWritten << endl;
    totalWritten += bytesWritten;
    //cout << "bytesWritten: " << bytesWritten << endl;
  }
  //cout << "bytesWritten: " << bytesWritten << endl;
  if (bytesWritten < 0) {
    TSDebug("cache_plugin", "[DiskCache::write] 0 bytes written");
    return -1;
  }

  return 0;
}


//----------------------------------------------------------------------------
int32_t
DiskCache::read(const datum & key, datum & value, const uint64_t size, const uint64_t offset)
{
  // get the file descriptor from the open file map
  int fd = -1;
  if ((fd = _getFileDescriptor(key, false /* non-exclusive */ )) < 0) {
    TSDebug("cache_plugin", "[DiskCache::read] can't find file descriptor");
    return -1;
  }

  ssize_t bytesRead = -1;
  ssize_t totalRead = 0;
  value.dsize = 0;

  while ((bytesRead =::pread64(fd, (void *) (value.dptr + totalRead), (size - totalRead), offset + totalRead)) > 0) {
    totalRead += bytesRead;
    //cout << "bytesRead: " << bytesRead << endl;
  }
  //cout << "bytesRead: " << bytesRead << endl;
  if (bytesRead < 0) {
    TSDebug("cache_plugin", "[DiskCache::read] 0 bytes read, offset: %llu, size: %llu", offset, size);
    //perror("read error:");
    return -1;
  }
  value.dsize = totalRead;

  return 0;
}


//----------------------------------------------------------------------------
int32_t
DiskCache::remove(const datum & key)
{
  int fd = -1;

  if ((fd = _getFileDescriptor(key, true, true)) < 0) {
    TSDebug("cache_plugin", "[DiskCache::remove] can't find file descriptor");
    return -1;
  }
  // truncate the file
  int rval = ftruncate(fd, 0);
  if (rval != 0) {
    TSDebug("cache_plugin", "[DiskCache::remove] error truncating file");
  }
  return rval;
}


//----------------------------------------------------------------------------
void
DiskCache::_makePath(const datum & key, string & path) const
{
  unsigned char digest[16];
  uint32_t *pathValue;
  MD5_CTX md5;
  string fileName;

  // get the md5sum of the key
  MD5_Init(&md5);
  MD5_Update(&md5, key.dptr, key.dsize);
  MD5_Final(digest, &md5);

  // we can assume _totalDirectories > _directoryWidth
  unsigned char *digestPos = (unsigned char *) digest;
  pathValue = (uint32_t *) digest;

  // create the filename
  for (int i = 0; i < 16; ++i, ++digestPos) {
    fileName.append(lookupTable[*digestPos]);
  }

  // int to path
  for (uint32_t i = 0; i < _directoryDepth; ++i) {
    uint32_t value = *pathValue;

    if (i) {
      value /= (_directoryWidth * i);   // or we can bit shift
    }
    value = value % _directoryWidth;    // or we can mask/or

    path = "/" + string(lookupTable[value]) + path;
  }

  path = _topDirectory + path + "/" + fileName;
}


//----------------------------------------------------------------------------
int
DiskCache::_makeDirectoryRecursive(const string & path, uint32_t numberDirectories) const
{
  char dirName[4];

  //cout << "directories: " << numberDirectories << endl;
  if (numberDirectories >= _directoryWidth) {
    numberDirectories /= _directoryWidth;
    numberDirectories -= numberDirectories % _directoryWidth;
    //cout << "  directories: " << numberDirectories << endl;

    for (int i = 0; i < _directoryWidth; ++i) {
      snprintf(dirName, 4, "/%02X", i);
      string fullPath = path + dirName;
      if (mkdir(fullPath.c_str(), 0755) == -1) {
        if (errno != EEXIST) {
          TSDebug("cache_plugin", "Couldn't create the cache directory: %s", fullPath.c_str());
          TSError("Couldn't create the  cache directory: %s", fullPath.c_str());

          return 1;
        }
      }
      //cout << fullPath << endl;
      if (numberDirectories > 0) {
        if (_makeDirectoryRecursive(fullPath, numberDirectories) != 0) {
          return 1;
        }
      }
    }
  }
  return 0;
}


//----------------------------------------------------------------------------
void
DiskCache::setNumberDirectories(const uint32_t directories)
{
  uint32_t tmpDirectories = directories;
  int count = 0;


  // set the number of directories to directory with to a power
  // each power is another directory level and there is a maximum
  // of 10 levels

  if (directories < _directoryWidth) {
    count = 1;
  } else {

    while (count < 10) {
      if (tmpDirectories < _directoryWidth) {
        ++count;
        break;
      } else if (tmpDirectories > _directoryWidth) {
        int remainder = tmpDirectories % _directoryWidth;
        ++count;
        tmpDirectories /= _directoryWidth;
        tmpDirectories -= tmpDirectories % _directoryWidth;
        tmpDirectories += remainder;
      } else {
        ++count;
        break;
      }
      //cout << "tmpDirectories: " << tmpDirectories << endl;
    }
  }
  //cout << "count: " << count << endl
  //     << "tmpDirectories: " << tmpDirectories << endl;

  _totalDirectories = (uint32_t) powl(_directoryWidth, count);
  _directoryDepth = count;
}
