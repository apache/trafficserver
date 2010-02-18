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
#include <iostream>
#include "DiskCache.h"


using namespace std;
static DiskCache cache;


//----------------------------------------------------------------------------
char *
get_info_from_buffer(INKIOBufferReader the_reader)
{
  char *info;
  char *info_start;

  int read_avail, read_done;
  INKIOBufferBlock blk;
  char *buf;

  if (!the_reader)
    return NULL;

  read_avail = INKIOBufferReaderAvail(the_reader);

  info = (char *) INKmalloc(sizeof(char) * read_avail);
  if (info == NULL)
    return NULL;
  info_start = info;

  /* Read the data out of the reader */
  while (read_avail > 0) {
    blk = INKIOBufferReaderStart(the_reader);
    buf = (char *) INKIOBufferBlockReadStart(blk, the_reader, &read_done);
    memcpy(info, buf, read_done);
    if (read_done > 0) {
      INKIOBufferReaderConsume(the_reader, read_done);
      read_avail -= read_done;
      info += read_done;
    }
  }

  return info_start;
}



static int
cache_read(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_read] event id: %d", event);


  INKDebug("cache_plugin", "[cache_read] disk cache plugin ");

  INKHttpTxn txnp = (INKHttpTxn) edata;
  datum key, value;
  int keySize = 0;
  INKU64 size, offset;
  //INKIOBuffer buff;

  // get the key for the lookup
  INKCacheKeyGet(txnp, (void **) &key.dptr, &keySize);
  key.dsize = keySize;
  //INKCacheBufferInfoGet(txnp, &buff,&size, &offset);
  INKCacheBufferInfoGet(txnp, &size, &offset);
  //cout <<"size of the object is" <<size << endl;
  // cout <<"offset of the object is" <<offset << endl;

  // 1. get IO buffer from the NewCacehVC
  // 2. get the offset and size to read from cache, size will be less then 32KB
  // 3. read from cache and write to the io buffer using the existing InkAPI
  // Only read 32K at a time from cache

  // lookup in cache and send the date to cache reenable

  char buffer[32768];
  value.dptr = buffer;
  value.dsize = 0;

  INKDebug("cache_plugin", "[cache_read] lock");
  cache.lock(key, false /* shared lock */ );
  INKDebug("cache_plugin", "[cache_read] read");
  if (cache.read(key, value, size, offset) == -1) {
    INKDebug("cache_plugin", "[cache_read] didn't find in cache");
    value.dptr = 0;
  }
  INKDebug("cache_plugin", "[cache_read] unlock");
  cache.unlock(key);
  // TODO write into IO buffer directly as described in steps above
  /*if(event != INK_EVENT_CACHE_LOOKUP)
     {
     INKIOBufferWrite(buff,value.dptr,value.dsize);
     INKDebug("cache_plugin", "[cache_read] return");
     return INKHttpCacheReenable(txnp, event, 0, value.dsize);
     }
     else
     { */
  return INKHttpCacheReenable(txnp, event, value.dptr, value.dsize);
  //}

}


//----------------------------------------------------------------------------
static int
cache_write(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_write] disk cache plugin");

  INKHttpTxn txnp = (INKHttpTxn) edata;

  // get the key for the data
  datum key, value;
  int keySize = 0;
  INKCacheKeyGet(txnp, (void **) &key.dptr, &keySize);
  key.dsize = keySize;

  // 1. get IO buffer from the NewCacheVC
  // 2. figure out if we need to append or create a new entry in cache
  // 3. use the existing InkAPI to read the io buffer and write into cache

  // get the buffer to write into cache and get the start of the buffer
  INKIOBufferReader buffer = INKCacheBufferReaderGet(txnp);
  INKIOBufferBlock block = INKIOBufferReaderStart(buffer);
  int available = INKIOBufferReaderAvail(buffer);
  char *temp_buf;
  uint64_t totalSize;
  // write to cache
  // if (available > 0) {

  cache.lock(key, true /* exclusive lock */ );
//    do {
//     int valueSize;
//      value.dptr = (char*)INKIOBufferBlockReadStart(block, buffer, &valueSize);
  value.dptr = (char *) get_info_from_buffer(buffer);
  value.dsize = available;
//      INKDebug("cache_plugin", "[cache_write] **** value size %d", valueSize);


  // write the first buffer block to the string
  if (value.dptr != NULL) {
    INKDebug("cache_plugin", "[cache_write] writing to the cache, bytes: %llu", value.dsize);
    if (cache.write(key, value) == -1) {
      INKDebug("cache_plugin", "[cache_write] ERROR: writing to cache");
    }
    //INKfree (value.dptr);
//        INKIOBufferReaderConsume(buffer, valueSize);
  }
//    } while ((block = INKIOBufferBlockNext(block)) != NULL);
  totalSize = cache.getSize(key);
  cache.unlock(key);
//  }

  return INKHttpCacheReenable(txnp, event, 0, totalSize);
}


//----------------------------------------------------------------------------
static int
cache_remove(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_remove] disk cache plugin");

  INKHttpTxn txnp = (INKHttpTxn) edata;

  return INKHttpCacheReenable(txnp, event, 0, 0);
}


//----------------------------------------------------------------------------
static int
cache_main(INKCont contp, INKEvent event, void *edata)
{
  INKDebug("cache_plugin", "[cache_main] event id: %d", event);

  switch (event) {
  case INK_EVENT_CACHE_LOOKUP:
  case INK_EVENT_CACHE_READ:
    return cache_read(contp, event, edata);
    break;

  case INK_EVENT_CACHE_WRITE:
  case INK_EVENT_CACHE_WRITE_HEADER:
    return cache_write(contp, event, edata);
    break;

  case INK_EVENT_CACHE_DELETE:
    return cache_remove(contp, event, edata);
    break;

  case INK_EVENT_CACHE_CLOSE:
    //do nothing
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
  INKCont contp;

  INKDebug("cache_plugin", "Starting plugin");


  info.plugin_name = "cache_plugin";
  info.vendor_name = "ASF";
  info.support_email = "";

  INKCont continuation_main = INKContCreate(cache_main, INKMutexCreate());

  INKCacheHookAdd(INK_CACHE_PLUGIN_HOOK, continuation_main);

  cache.setTopDirectory("/home/trafficserver/share/yts");
  cache.setNumberDirectories(65536);
  if (cache.makeDirectories() != 0) {
    INKDebug("cache_plugin", "Couldn't create the cache directories");
    INKError("cache_plugin", "Couldn't create the cache directories");
    abort();
  }
}
