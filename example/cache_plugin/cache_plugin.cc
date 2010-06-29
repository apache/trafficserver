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

#include <ts/ts.h>
#include <ts/experimental.h>
#include <iostream>
#include <map>
#include <string>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>


using namespace std;


static map<string, string> cache;
static INKMutex cacheMutex;

//----------------------------------------------------------------------------
void *
eventLoop(void *data)
{
  while (1) {
    INKDebug("cache_plugin", "[eventLoop]");
    sleep(5);

    for (int i = 78; i > 0; --i)
      cout << "-";
    cout << endl;               // print a line
    cout << "entries in cache: " << cache.size() << endl;

    if (INKMutexLock(cacheMutex) == INK_SUCCESS) {
      for (map<string, string>::const_iterator it = cache.begin(); it != cache.end(); ++it) {
        //cout << "key: " << it->first << endl;
        //<< "value:" << it->second << endl;
        cout << "key size: " << it->first.size() << endl << "value size:" << it->second.size() << endl;
      }
      INKMutexUnlock(cacheMutex);
    }
    for (int i = 78; i > 0; --i)
      cout << "-";
    cout << endl;               // print a line
  }

  return NULL;
}


//----------------------------------------------------------------------------
static int
cache_read(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_read]");

  INKHttpTxn txnp = (INKHttpTxn) edata;
  void *key = 0;
  int keySize = 0;
  INKU64 size, offset;

  // get the key for the lookup
  INKCacheKeyGet(txnp, &key, &keySize);
  INKCacheBufferInfoGet(txnp, &size, &offset);

  // 1. get IO buffer from the NewCacehVC
  // 2. get the offset and size to read from cache, size will be less then 32KB
  // 3. read from cache and write to the io buffer using the existing InkAPI
  // Only read 32K at a time from cache

  // lookup in cache and send the date to cache reenable
  const void *cacheData = 0;
  uint64_t cacheSize = 0;

  if (event == INK_EVENT_CACHE_LOOKUP) {
    event = INK_EVENT_CACHE_LOOKUP_COMPLETE;
  } else {
    event = INK_EVENT_CACHE_READ_COMPLETE;
  }

  if (key != 0 && keySize > 0) {
    string keyString((char *) key, keySize);

    if (INKMutexLock(cacheMutex) != INK_SUCCESS) {
      INKDebug("cache_plugin", "[cache_read] failed to acquire cache mutex");
    } else {
      map<string, string>::iterator it = cache.find(keyString);
      if (it != cache.end() && size > 0 && offset < it->second.size()) {
        // found in cache
        cacheSize = size;
        if (size + offset > it->second.size()) {
          cacheSize = it->second.size() - offset;
        }
        cacheData = it->second.substr(offset, cacheSize).c_str();

        if (cacheSize + offset < it->second.size()) {
          if (event == INK_EVENT_CACHE_LOOKUP_COMPLETE) {
            event = INK_EVENT_CACHE_LOOKUP_READY;
          } else {
            event = INK_EVENT_CACHE_READ_READY;
          }
        }
      }
      INKReturnCode rval = INKHttpCacheReenable(txnp, event, cacheData, cacheSize);
      INKMutexUnlock(cacheMutex);
      return rval;
    }
  }

  return INKHttpCacheReenable(txnp, event, cacheData, cacheSize);
}


//----------------------------------------------------------------------------
static int
cache_write(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_write]");

  INKHttpTxn txnp = (INKHttpTxn) edata;

  // get the key for the data
  void *key;
  int keySize;
  INKCacheKeyGet(txnp, &key, &keySize);

  uint64_t cacheSize = 0;

  // 1. get IO buffer from the NewCacheVC
  // 2. figure out if we need to append or create a new entry in cache
  // 3. use the existing InkAPI to read the io buffer and write into cache

  // get the buffer to write into cache and get the start of the buffer
  INKIOBufferReader buffer = INKCacheBufferReaderGet(txnp);
  INKIOBufferBlock block = INKIOBufferReaderStart(buffer);
  int available = INKIOBufferReaderAvail(buffer);

  string keyString((char *) key, keySize);

  // read from cache

  if (INKMutexLock(cacheMutex) != INK_SUCCESS) {
    INKDebug("cache_plugin", "[cache_write] failed to acquire cache mutex");
  } else {
    INKDebug("cache_plugin", "[cache_write] writting to cache");
    map<string, string>::iterator it = cache.find(keyString);
    if (it == cache.end()) {
      cache.insert(make_pair(keyString, string("")));
      it = cache.find(keyString);
    } else if (event == INK_EVENT_CACHE_WRITE_HEADER) {
      //don't append headers
      it->second.erase();
    }

    if (available > 0) {
      int ndone = 0;
      do {
        const char *data = INKIOBufferBlockReadStart(block, buffer, &available);

        // append the buffer block to the string
        if (data != NULL) {
          it->second.append(data, available);
        }
        ndone += available;
      } while ((block = INKIOBufferBlockNext(block)) != NULL);

      INKIOBufferReaderConsume(buffer, ndone);
    }
    cacheSize = it->second.size();
    INKMutexUnlock(cacheMutex);
  }

  return INKHttpCacheReenable(txnp, event, 0, cacheSize);
}


//----------------------------------------------------------------------------
static int
cache_remove(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKDebug("cache_plugin", "[cache_remove]");

  // get the key for the data
  void *key;
  int keySize;
  INKCacheKeyGet(txnp, &key, &keySize);

  if (INKMutexLock(cacheMutex) != INK_SUCCESS) {
    INKDebug("cache_plugin", "[cache_remove] failed to acquire cache mutex");
  } else {
    // find the entry in cache
    string keyString((char *) key, keySize);
    map<string, string>::iterator it = cache.find(keyString);

    // see if we found the entry and remove it
    if (it != cache.end()) {
      cache.erase(it);
    } else {
      INKDebug("cache_plugin", "trying to remove a entry from cache that doesn't exist");
    }
    INKMutexUnlock(cacheMutex);
  }

  return INKHttpCacheReenable(txnp, event, 0, 0);
}


//----------------------------------------------------------------------------
static int
cache_plugin(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
    // read events
  case INK_EVENT_CACHE_LOOKUP:
  case INK_EVENT_CACHE_READ:
    return cache_read(contp, event, edata);
    break;

    // write events
  case INK_EVENT_CACHE_WRITE:
  case INK_EVENT_CACHE_WRITE_HEADER:
    return cache_write(contp, event, edata);
    break;

    // delete events
  case INK_EVENT_CACHE_DELETE:
    return cache_remove(contp, event, edata);
    break;

  case INK_EVENT_CACHE_CLOSE:
    return INKHttpCacheReenable(txnp, event, 0, 0);
    break;

  default:
    INKDebug("cache_plugin", "ERROR: unknown event");
    return 0;
  }
}


//----------------------------------------------------------------------------
void
INKPluginInit(const int argc, const char **argv)
{
  INKPluginRegistrationInfo info;

  INKDebug("cache_plugin", "[INKPluginInit] Starting cache plugin");

  info.plugin_name = (char *) "cache_plugin";
  info.vendor_name = (char *) "ASF";
  info.support_email = (char *) "";

  cacheMutex = INKMutexCreate();

  INKCont continuation_plugin = INKContCreate(cache_plugin, INKMutexCreate());

  INKCacheHookAdd(INK_CACHE_PLUGIN_HOOK, continuation_plugin);

  //cacheThread = INKThreadCreate(eventLoop, 0);
}
