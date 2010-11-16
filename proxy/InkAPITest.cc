/** @file

  Implements unit test for SDK APIs

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

#include "ink_config.h"
#include <sys/types.h>

#include <errno.h>
//extern int errno;

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "Regression.h"
#include "api/ts/ts.h"
#include "api/ts/experimental.h"
#include "I_RecCore.h"
#include "I_Layout.h"

#include "InkAPITestTool.cc"
#include "http2/HttpSM.h"

#define TC_PASS 1
#define TC_FAIL 0

#define UTDBG_TAG "sdk_ut"

#define LOCAL_IP 0x7f000001     // 127.0.0.1

/******************************************************************************/



/* Use SDK_RPRINT to report failure or success for each test case */
int
SDK_RPRINT(RegressionTest * t, const char *api_name, const char *testcase_name, int status, const char *err_details_format, ...)
{
  int l;
  char buffer[8192];
  char format2[8192];
  snprintf(format2, sizeof(format2), "[%s] %s : [%s] <<%s>> { %s }\n", t->name,
           api_name, testcase_name, status == TC_PASS ? "PASS" : "FAIL", err_details_format);
  va_list ap;
  va_start(ap, err_details_format);
  l = ink_bvsprintf(buffer, format2, ap);
  va_end(ap);
  fputs(buffer, stderr);
  return (l);
}


/*
  REGRESSION_TEST(SDK_<test_name>)(RegressionTest *t, int atype, int *pstatus)

  RegressionTest *test is a pointer on object that will run the test.
   Do not modify.

  int atype is one of:
   REGRESSION_TEST_NONE
   REGRESSION_TEST_QUICK
   REGRESSION_TEST_NIGHTLY
   REGRESSION_TEST_EXTENDED

  int *pstatus should be set to one of:
   REGRESSION_TEST_PASSED
   REGRESSION_TEST_INPROGRESS
   REGRESSION_TEST_FAILED
   REGRESSION_TEST_NOT_RUN
  Note: pstatus is polled and can be used for asynchroneous tests.

*/

/* Misc */
////////////////////////////////////////////////
//       SDK_API_INKTrafficServerVersionGet
//
// Unit Test for API: INKTrafficServerVersionGet
////////////////////////////////////////////////
REGRESSION_TEST(SDK_API_INKTrafficServerVersionGet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  /* Assume the UT runs on TS5.0 and higher */
  const char *ts_version = INKTrafficServerVersionGet();
  if (!ts_version) {
    SDK_RPRINT(test, "INKTrafficServerVersionGet", "TestCase1", TC_FAIL, "can't get traffic server version");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  int major_ts_version = 0;
  int minor_ts_version = 0;
  int patch_ts_version = 0;
  // coverity[secure_coding]
  if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
    SDK_RPRINT(test, "INKTrafficServerVersionGet", "TestCase2", TC_FAIL, "traffic server version format is incorrect");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  if (major_ts_version < 2) {
    SDK_RPRINT(test, "INKTrafficServerVersionGet", "TestCase3", TC_FAIL, "traffic server major version is incorrect");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  SDK_RPRINT(test, "INKTrafficServerVersionGet", "TestCase1", TC_PASS, "ok");
  *pstatus = REGRESSION_TEST_PASSED;
  return;
}


////////////////////////////////////////////////
//       SDK_API_INKPluginDirGet
//
// Unit Test for API: INKPluginDirGet
//                    INKInstallDirGet
////////////////////////////////////////////////
REGRESSION_TEST(SDK_API_INKPluginDirGet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  const char *plugin_dir = INKPluginDirGet();
  const char *install_dir = INKInstallDirGet();

  if (!plugin_dir) {
    SDK_RPRINT(test, "INKPluginDirGet", "TestCase1", TC_FAIL, "can't get plugin dir");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  if (!install_dir) {
    SDK_RPRINT(test, "INKInstallDirGet", "TestCase1", TC_FAIL, "can't get installation dir");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  // XXX: This doesn't have to be true
  //      since the location can be anywhere
  //
  if (strstr(plugin_dir, "libexec/trafficserver") == NULL) {
    SDK_RPRINT(test, "INKPluginDirGet", "TestCase2", TC_FAIL, "plugin dir(%s) is incorrect, expected (%s) in path",plugin_dir,"libexec/trafficserver");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  if (strstr(plugin_dir, install_dir) == NULL) {
    SDK_RPRINT(test, "INKInstallDirGet", "TestCase2", TC_FAIL, "install dir is incorrect");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  SDK_RPRINT(test, "INKPluginDirGet", "TestCase1", TC_PASS, "ok");
  SDK_RPRINT(test, "INKInstallDirGet", "TestCase1", TC_PASS, "ok");
  *pstatus = REGRESSION_TEST_PASSED;
  return;
}


/* INKConfig */
////////////////////////////////////////////////
//       SDK_API_INKConfig
//
// Unit Test for API: INKConfigSet
//                    INKConfigGet
//                    INKConfigRelease
//                    INKConfigDataGet
////////////////////////////////////////////////
static int my_config_id = -1;
typedef struct
{
  const char *a;
  const char *b;
} ConfigData;

static void
config_destroy_func(void *data)
{
  ConfigData *config = (ConfigData *) data;
  INKfree(config);
  return;
}

REGRESSION_TEST(SDK_API_INKConfig) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  ConfigData *config = (ConfigData *) INKmalloc(sizeof(ConfigData));
  config->a = "unit";
  config->b = "test";

  my_config_id = INKConfigSet(0, config, config_destroy_func);

  INKConfig test_config = NULL;
  test_config = INKConfigGet(my_config_id);

  if (!test_config) {
    SDK_RPRINT(test, "INKConfigSet", "TestCase1", TC_FAIL, "can't correctly set global config structure");
    SDK_RPRINT(test, "INKConfigGet", "TestCase1", TC_FAIL, "can't correctly get global config structure");
    INKConfigRelease(my_config_id, config);
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  if (INKConfigDataGet(test_config) != config) {
    SDK_RPRINT(test, "INKConfigDataGet", "TestCase1", TC_FAIL, "failed to get config data");
    INKConfigRelease(my_config_id, config);
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  SDK_RPRINT(test, "INKConfigGet", "TestCase1", TC_PASS, "ok");
  SDK_RPRINT(test, "INKConfigSet", "TestCase1", TC_PASS, "ok");
  SDK_RPRINT(test, "INKConfigDataGet", "TestCase1", TC_PASS, "ok");

  INKConfigRelease(my_config_id, config);
  *pstatus = REGRESSION_TEST_PASSED;
  return;
}

/* INKNetVConn */
//////////////////////////////////////////////
//       SDK_API_INKNetVConn
//
// Unit Test for API: INKNetVConnRemoteIPGet
//                    INKNetVConnRemotePortGet
//                    INKNetAccept
//                    INKNetConnect
//////////////////////////////////////////////
#define IP(a,b,c,d) htonl((a) << 24 | (b) << 16 | (c) << 8 | (d))
const unsigned short server_port = 12345;
RegressionTest *SDK_NetVConn_test;
int *SDK_NetVConn_pstatus;

int
server_handler(INKCont contp, INKEvent event, void *data)
{
  NOWARN_UNUSED(data);
  if (event == INK_EVENT_VCONN_EOS)
    INKContDestroy(contp);

  return 1;
}

int
client_handler(INKCont contp, INKEvent event, void *data)
{
  if (event == INK_EVENT_NET_CONNECT_FAILED) {
    SDK_RPRINT(SDK_NetVConn_test, "INKNetAccept", "TestCase1", TC_FAIL, "can't connect to server");
    SDK_RPRINT(SDK_NetVConn_test, "INKNetConnect", "TestCase1", TC_FAIL, "can't connect to server");

    // no need to continue, return
    INKContDestroy(contp);
    // Fix me: how to deal with server side cont?
    *SDK_NetVConn_pstatus = REGRESSION_TEST_FAILED;

    return 1;
  } else {
    SDK_RPRINT(SDK_NetVConn_test, "INKNetAccept", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(SDK_NetVConn_test, "INKNetConnect", "TestCase1", TC_PASS, "ok");

    unsigned int input_server_ip = 0;
    int input_server_port = 0;
    INKNetVConnRemoteIPGet((INKVConn) data, &input_server_ip);
    INKNetVConnRemotePortGet((INKVConn) data, &input_server_port);

    if (input_server_ip != htonl(LOCAL_IP)) {
      SDK_RPRINT(SDK_NetVConn_test, "INKNetVConnRemoteIPGet", "TestCase1", TC_FAIL, "server ip is incorrect");

      INKContDestroy(contp);
      // Fix me: how to deal with server side cont?
      *SDK_NetVConn_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else
      SDK_RPRINT(SDK_NetVConn_test, "INKNetVConnRemoteIPGet", "TestCase1", TC_PASS, "ok");

    if (input_server_port != server_port) {
      SDK_RPRINT(SDK_NetVConn_test, "INKNetVConnRemotePortGet", "TestCase1", TC_FAIL, "server port is incorrect");

      INKContDestroy(contp);
      // Fix me: how to deal with server side cont?
      *SDK_NetVConn_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else
      SDK_RPRINT(SDK_NetVConn_test, "INKNetVConnRemotePortGet", "TestCase1", TC_PASS, "ok");

    INKVConnClose((INKVConn) data);
  }

  INKContDestroy(contp);

  *SDK_NetVConn_pstatus = REGRESSION_TEST_PASSED;
  return 1;
}

REGRESSION_TEST(SDK_API_INKNetVConn) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  SDK_NetVConn_test = test;
  SDK_NetVConn_pstatus = pstatus;

  INKMutex server_mutex = INKMutexCreate();
  INKMutex client_mutex = INKMutexCreate();

  INKCont server_cont = INKContCreate(server_handler, server_mutex);
  INKCont client_cont = INKContCreate(client_handler, client_mutex);

  INKNetAccept(server_cont, server_port);

  unsigned int server_ip = IP(127, 0, 0, 1);
  INKNetConnect(client_cont, server_ip, server_port);
}

/* INKCache, INKVConn, INKVIO */
//////////////////////////////////////////////
//       SDK_API_INKCache
//
// Unit Test for API: INKCacheReady
//                    INKCacheWrite
//                    INKCacheRead
//                    INKCacheKeyCreate
//                    INKCacheKeyDigestSet
//                    INKVConnCacheObjectSizeGet
//                    INKVConnClose
//                    INKVConnClosedGet
//                    INKVConnRead
//                    INKVConnReadVIOGet
//                    INKVConnWrite
//                    INKVConnWriteVIOGet
//                    INKVIOBufferGet
//                    INKVIOContGet
//                    INKVIOMutexGet
//                    INKVIONBytesGet
//                    INKVIONBytesSet
//                    INKVIONDoneGet
//                    INKVIONDoneSet
//                    INKVIONTodoGet
//                    INKVIOReaderGet
//                    INKVIOReenable
//                    INKVIOVConnGet
//////////////////////////////////////////////

// INKVConnAbort can't be tested
// Fix me: test INKVConnShutdown, INKCacheKeyDataTypeSet,
//         INKCacheKeyHostNameSet, INKCacheKeyPinnedSet

// Logic of the test:
//  - write OBJECT_SIZE bytes in the cache in 3 shots
//    (OBJECT_SIZE/2, then OBJECT_SIZE-100 and finally OBJECT_SIZE)
//  - read object from the cache
//  - remove it from the cache
//  - try to read it (should faild)


#define OBJECT_SIZE 100000      // size of the object we'll write/read/remove in cache


RegressionTest *SDK_Cache_test;
int *SDK_Cache_pstatus;
static char content[OBJECT_SIZE];
static int read_counter = 0;

typedef struct
{
  INKIOBuffer bufp;
  INKIOBuffer out_bufp;
  INKIOBufferReader readerp;
  INKIOBufferReader out_readerp;

  INKVConn write_vconnp;
  INKVConn read_vconnp;
  INKVIO read_vio;
  INKVIO write_vio;

  INKCacheKey key;
} CacheVConnStruct;

int
cache_handler(INKCont contp, INKEvent event, void *data)
{
  Debug("sdk_ut_cache_write", "Event %d data %p", event, data);

  CacheVConnStruct *cache_vconn = (CacheVConnStruct *) INKContDataGet(contp);

  INKIOBufferBlock blockp;
  char *ptr_block;
  int64 ntodo, ndone, nbytes, towrite, avail, content_length;

  switch (event) {
  case INK_EVENT_CACHE_OPEN_WRITE:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_OPEN_WRITE %d %p", event, data);
    SDK_RPRINT(SDK_Cache_test, "INKCacheWrite", "TestCase1", TC_PASS, "ok");

    // data is write_vc
    cache_vconn->write_vconnp = (INKVConn) data;

    // Create buffers/readers to write and read data into the cache
    cache_vconn->bufp = INKIOBufferCreate();
    cache_vconn->readerp = INKIOBufferReaderAlloc(cache_vconn->bufp);
    cache_vconn->out_bufp = INKIOBufferCreate();
    cache_vconn->out_readerp = INKIOBufferReaderAlloc(cache_vconn->out_bufp);

    // Write content into upstream IOBuffer
    ntodo = OBJECT_SIZE;
    ndone = 0;
    while (ntodo > 0) {
      blockp = INKIOBufferStart(cache_vconn->bufp);
      ptr_block = INKIOBufferBlockWriteStart(blockp, &avail);
      towrite = ((ntodo < avail) ? ntodo : avail);
      memcpy(ptr_block, content + ndone, towrite);
      INKIOBufferProduce(cache_vconn->bufp, towrite);
      ntodo -= towrite;
      ndone += towrite;
    }

    // first write half of the data. To test INKVIOReenable
    cache_vconn->write_vio = INKVConnWrite((INKVConn) data, contp, cache_vconn->readerp, OBJECT_SIZE / 2);
    return 1;

  case INK_EVENT_CACHE_OPEN_WRITE_FAILED:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_OPEN_WRITE_FAILED %d %p", event, data);
    SDK_RPRINT(SDK_Cache_test, "INKCacheWrite", "TestCase1", TC_FAIL, "can't open cache vc, edtata = %p", data);
    INKReleaseAssert(!"cache");

    // no need to continue, return
    *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
    return 1;

  case INK_EVENT_CACHE_OPEN_READ:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_OPEN_READ %d %p", event, data);
    if (read_counter == 2) {
      SDK_RPRINT(SDK_Cache_test, "INKCacheRead", "TestCase2", TC_FAIL, "shouldn't open cache vc");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }

    SDK_RPRINT(SDK_Cache_test, "INKCacheRead", "TestCase1", TC_PASS, "ok");

    cache_vconn->read_vconnp = (INKVConn) data;
    INKVConnCacheObjectSizeGet(cache_vconn->read_vconnp, &content_length);
    Debug(UTDBG_TAG "_cache_read", "In cache open read [Content-Length: %d]", content_length);
    if (content_length != OBJECT_SIZE) {
      SDK_RPRINT(SDK_Cache_test, "INKVConnCacheObjectSizeGet", "TestCase1", TC_FAIL, "cached data size is incorrect");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVConnCacheObjectSizeGet", "TestCase1", TC_PASS, "ok");
      cache_vconn->read_vio = INKVConnRead((INKVConn) data, contp, cache_vconn->out_bufp, content_length);
    }
    return 1;

  case INK_EVENT_CACHE_OPEN_READ_FAILED:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_OPEN_READ_FAILED %d %p", event, data);
    if (read_counter == 1) {
      SDK_RPRINT(SDK_Cache_test, "INKCacheRead", "TestCase1", TC_FAIL, "can't open cache vc");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }
    SDK_RPRINT(SDK_Cache_test, "INKCacheRead", "TestCase2", TC_PASS, "ok");

    // ok, all tests passed!
    break;

  case INK_EVENT_CACHE_REMOVE:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_REMOVE %d %p", event, data);
    SDK_RPRINT(SDK_Cache_test, "INKCacheRemove", "TestCase1", TC_PASS, "ok");

    // read the data which has been removed
    read_counter++;
    INKCacheRead(contp, cache_vconn->key);
    return 1;

  case INK_EVENT_CACHE_REMOVE_FAILED:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_CACHE_REMOVE_FAILED %d %p", event, data);
    SDK_RPRINT(SDK_Cache_test, "INKCacheRemove", "TestCase1", TC_FAIL, "can't remove cached item");

    // no need to continue, return
    *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
    return 1;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_VCONN_WRITE_COMPLETE %d %p", event, data);

    // VConn/VIO APIs
    nbytes = INKVIONBytesGet(cache_vconn->write_vio);
    ndone = INKVIONDoneGet(cache_vconn->write_vio);
    ntodo = INKVIONTodoGet(cache_vconn->write_vio);
    Debug(UTDBG_TAG "_cache_write", "Nbytes=%d Ndone=%d Ntodo=%d", nbytes, ndone, ntodo);

    if (ndone == (OBJECT_SIZE / 2)) {
      INKVIONBytesSet(cache_vconn->write_vio, (OBJECT_SIZE - 100));
      INKVIOReenable(cache_vconn->write_vio);
      Debug(UTDBG_TAG "_cache_write", "Increment write_counter in write_complete [a]");
      return 1;
    } else if (ndone == (OBJECT_SIZE - 100)) {
      INKVIONBytesSet(cache_vconn->write_vio, OBJECT_SIZE);
      INKVIOReenable(cache_vconn->write_vio);
      Debug(UTDBG_TAG "_cache_write", "Increment write_counter in write_complete [b]");
      return 1;
    } else if (ndone == OBJECT_SIZE) {
      Debug(UTDBG_TAG "_cache_write", "finishing up [c]");

      SDK_RPRINT(SDK_Cache_test, "INKVIOReenable", "TestCase2", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVIONBytesSet", "TestCase1", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVConnWrite", "TestCase1", TC_PASS, "ok");
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKCacheWrite", "TestCase1", TC_FAIL, "Did not write expected # of bytes");
      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }

    if ((INKVIO) data != cache_vconn->write_vio) {
      SDK_RPRINT(SDK_Cache_test, "INKVConnWrite", "TestCase1", TC_FAIL, "write_vio corrupted");
      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }
    Debug(UTDBG_TAG "_cache_write", "finishing up [d]");


    if (INKVIOBufferGet(cache_vconn->write_vio) != cache_vconn->bufp) {
      SDK_RPRINT(SDK_Cache_test, "INKVIOBufferGet", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIOBufferGet", "TestCase1", TC_PASS, "ok");
    }

    if (INKVIOContGet(cache_vconn->write_vio) != contp) {
      SDK_RPRINT(SDK_Cache_test, "INKVIOContGet", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIOContGet", "TestCase1", TC_PASS, "ok");
    }

    Debug(UTDBG_TAG "_cache_write", "finishing up [f]");

    if (INKVIOMutexGet(cache_vconn->write_vio) != INKContMutexGet(contp)) {
      SDK_RPRINT(SDK_Cache_test, "INKVIOMutexGet", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIOMutexGet", "TestCase1", TC_PASS, "ok");
    }

    if (INKVIOVConnGet(cache_vconn->write_vio) != cache_vconn->write_vconnp) {
      SDK_RPRINT(SDK_Cache_test, "INKVIOVConnGet", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIOVConnGet", "TestCase1", TC_PASS, "ok");
    }

    Debug(UTDBG_TAG "_cache_write", "finishing up [g]");

    if (INKVIOReaderGet(cache_vconn->write_vio) != cache_vconn->readerp) {
      SDK_RPRINT(SDK_Cache_test, "INKVIOReaderGet", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIOReaderGet", "TestCase1", TC_PASS, "ok");
    }

    // tests for write is done, close write_vconnp
    INKVConnClose(cache_vconn->write_vconnp);
    cache_vconn->write_vconnp = NULL;

    Debug(UTDBG_TAG "_cache_write", "finishing up [h]");


    // start to read data out of cache
    read_counter++;
    INKCacheRead(contp, cache_vconn->key);
    Debug(UTDBG_TAG "_cache_read", "starting read [i]");
    return 1;


  case INK_EVENT_VCONN_WRITE_READY:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_VCONN_WRITE_READY %d %p", event, data);
    if ((INKVIO) data != cache_vconn->write_vio) {
      SDK_RPRINT(SDK_Cache_test, "INKVConnWrite", "TestCase1", TC_FAIL, "write_vio corrupted");
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }

    nbytes = INKVIONBytesGet(cache_vconn->write_vio);
    ndone = INKVIONDoneGet(cache_vconn->write_vio);
    ntodo = INKVIONTodoGet(cache_vconn->write_vio);
    Debug(UTDBG_TAG "_cache_write", "Nbytes=%d Ndone=%d Ntodo=%d", nbytes, ndone, ntodo);

    INKVIOReenable(cache_vconn->write_vio);
    return 1;

  case INK_EVENT_VCONN_READ_COMPLETE:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_VCONN_READ_COMPLETE %d %p", event, data);
    if ((INKVIO) data != cache_vconn->read_vio) {
      SDK_RPRINT(SDK_Cache_test, "INKVConnRead", "TestCase1", TC_FAIL, "read_vio corrupted");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }

    nbytes = INKVIONBytesGet(cache_vconn->read_vio);
    ntodo = INKVIONTodoGet(cache_vconn->read_vio);
    ndone = INKVIONDoneGet(cache_vconn->read_vio);
    Debug(UTDBG_TAG "_cache_read", "Nbytes=%d Ndone=%d Ntodo=%d", nbytes, ndone, ntodo);

    if (nbytes != (ndone + ntodo)) {
      SDK_RPRINT(SDK_Cache_test, "INKVIONBytesGet", "TestCase1", TC_FAIL, "read_vio corrupted");
      SDK_RPRINT(SDK_Cache_test, "INKVIONTodoGet", "TestCase1", TC_FAIL, "read_vio corrupted");
      SDK_RPRINT(SDK_Cache_test, "INKVIONDoneGet", "TestCase1", TC_FAIL, "read_vio corrupted");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIONBytesGet", "TestCase1", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVIONTodoGet", "TestCase1", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVIONDoneGet", "TestCase1", TC_PASS, "ok");

      INKVIONDoneSet(cache_vconn->read_vio, 0);
      if (INKVIONDoneGet(cache_vconn->read_vio) != 0) {
        SDK_RPRINT(SDK_Cache_test, "INKVIONDoneSet", "TestCase1", TC_FAIL, "fail to set");

        // no need to continue, return
        *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
        return 1;
      } else
        SDK_RPRINT(SDK_Cache_test, "INKVIONDoneSet", "TestCase1", TC_PASS, "ok");

      Debug(UTDBG_TAG "_cache_write", "finishing up [i]");

      // now waiting for 100ms to make sure the key is
      // written in directory remove the content
      INKContSchedule(contp, 100);
    }

    return 1;

  case INK_EVENT_VCONN_READ_READY:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_VCONN_READ_READY %d %p", event, data);
    if ((INKVIO) data != cache_vconn->read_vio) {
      SDK_RPRINT(SDK_Cache_test, "INKVConnRead", "TestCase1", TC_FAIL, "read_vio corrupted");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    }

    nbytes = INKVIONBytesGet(cache_vconn->read_vio);
    ntodo = INKVIONTodoGet(cache_vconn->read_vio);
    ndone = INKVIONDoneGet(cache_vconn->read_vio);
    Debug(UTDBG_TAG "_cache_read", "Nbytes=%d Ndone=%d Ntodo=%d", nbytes, ndone, ntodo);

    if (nbytes != (ndone + ntodo)) {
      SDK_RPRINT(SDK_Cache_test, "INKVIONBytesGet", "TestCase1", TC_FAIL, "read_vio corrupted");
      SDK_RPRINT(SDK_Cache_test, "INKVIONTodoGet", "TestCase1", TC_FAIL, "read_vio corrupted");
      SDK_RPRINT(SDK_Cache_test, "INKVIONDoneGet", "TestCase1", TC_FAIL, "read_vio corrupted");

      // no need to continue, return
      *SDK_Cache_pstatus = REGRESSION_TEST_FAILED;
      return 1;
    } else {
      SDK_RPRINT(SDK_Cache_test, "INKVIONBytesGet", "TestCase1", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVIONTodoGet", "TestCase1", TC_PASS, "ok");
      SDK_RPRINT(SDK_Cache_test, "INKVIONDoneGet", "TestCase1", TC_PASS, "ok");
    }

    // Fix for bug INKqa12276: Must consume data from iobuffer
    nbytes = INKIOBufferReaderAvail(cache_vconn->out_readerp);
    INKIOBufferReaderConsume(cache_vconn->out_readerp, nbytes);
    INKDebug(UTDBG_TAG "_cache_read", "Consuming %d bytes from cache read VC", nbytes);

    INKVIOReenable(cache_vconn->read_vio);
    Debug(UTDBG_TAG "_cache_read", "finishing up [j]");
    return 1;

  case INK_EVENT_TIMEOUT:
    Debug(UTDBG_TAG "_cache_event", "INK_EVENT_TIMEOUT %d %p", event, data);
    // do remove cached doc
    INKCacheRemove(contp, cache_vconn->key);
    return 1;

  default:
    INKReleaseAssert(!"Test SDK_API_INKCache: unexpected event");
  }

  Debug(UTDBG_TAG "_cache_event", "DONE DONE DONE");

  // destroy the data structure
  Debug(UTDBG_TAG "_cache_write", "all tests passed [z]");
  INKIOBufferDestroy(cache_vconn->bufp);
  INKIOBufferDestroy(cache_vconn->out_bufp);
  INKCacheKeyDestroy(cache_vconn->key);
  INKfree(cache_vconn);
  *SDK_Cache_pstatus = REGRESSION_TEST_PASSED;

  return 1;
}

REGRESSION_TEST(SDK_API_INKCache) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  SDK_Cache_test = test;
  SDK_Cache_pstatus = pstatus;
  int is_ready = 0;

  // Check if Cache is ready
  INKCacheReady(&is_ready);
  if (!is_ready) {
    SDK_RPRINT(test, "INKCacheReady", "TestCase1", TC_FAIL, "cache is not ready");

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKCacheReady", "TestCase1", TC_PASS, "ok");
  }

  // Create CacheKey
  char key_name[] = "key_for_regression_test";
  INKCacheKey key, key_cmp;
  INKCacheKeyCreate(&key);
  INKCacheKeyCreate(&key_cmp);
  if (key == NULL) {
    SDK_RPRINT(test, "INKCacheKeyCreate", "TestCase1", TC_FAIL, "can't malloc memory for key");

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (key_cmp != NULL)
      INKCacheKeyDestroy(key_cmp);
    return;
  } else {
    SDK_RPRINT(test, "INKCacheKeyCreate", "TestCase1", TC_PASS, "ok");
  }
  INKCacheKeyDigestSet(key, (unsigned char *) key_name, strlen(key_name));
  INKCacheKeyDigestSet(key_cmp, (unsigned char *) key_name, strlen(key_name));

  if (memcmp(key, key_cmp, sizeof(INKCacheKey)) != 0) {
    SDK_RPRINT(test, "INKCacheKeySetDigest", "TestCase1", TC_FAIL, "digest is wrong");

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    INKCacheKeyDestroy(key);
    INKCacheKeyDestroy(key_cmp);
    return;
  } else {
    SDK_RPRINT(test, "INKCacheKeySetDigest", "TestCase1", TC_PASS, "ok");
    INKCacheKeyDestroy(key_cmp);
  }

  // prepare caching content
  // string, null-terminated.
  for (int i = 0; i < (OBJECT_SIZE - 1); i++) {
    content[i] = 'a';
  }
  content[OBJECT_SIZE - 1] = '\0';

  //Write data to cache.
  INKCont contp = INKContCreate(cache_handler, INKMutexCreate());
  CacheVConnStruct *cache_vconn = (CacheVConnStruct *) INKmalloc(sizeof(CacheVConnStruct));
  cache_vconn->key = key;
  INKContDataSet(contp, cache_vconn);

  INKCacheWrite(contp, key);
}

/* INKfopen */

//////////////////////////////////////////////
//       SDK_API_INKfopen
//
// Unit Test for API: INKfopen
//                    INKclose
//                    INKfflush
//                    INKfgets
//                    INKfread
//                    INKfwrite
//////////////////////////////////////////////

// Used to create tmp file
//#define TMP_DIR "/var/tmp"
#define	PFX	"plugin.config"

REGRESSION_TEST(SDK_API_INKfopen) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  char write_file_name[PATH_NAME_MAX + 1];

  INKFile source_read_file;     // existing file
  INKFile write_file;           // to be created
  INKFile cmp_read_file;        // read & compare

  char input_buffer[BUFSIZ];
  char cmp_buffer[BUFSIZ];
  struct stat stat_buffer_pre, stat_buffer_post, stat_buffer_input;
  char *ret_val;
  int error_counter = 0, read = 0, wrote = 0;
  int64 read_amount = 0;
  char INPUT_TEXT_FILE[] = "plugin.config";
  char input_file_full_path[BUFSIZ];



  // Set full path to file at run time.
  // TODO: This can never fail since we are
  //       returning the char[]
  //       Better check the dir itself.
  //
  if (INKInstallDirGet() == NULL) {
    error_counter++;
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }
  // Add "etc/trafficserver" to point to config directory
  ink_filepath_make(input_file_full_path, sizeof(input_file_full_path),
                    INKConfigDirGet(), INPUT_TEXT_FILE);

  // open existing file for reading
  if (!(source_read_file = INKfopen(input_file_full_path, "r"))) {
    SDK_RPRINT(test, "INKfopen", "TestCase1", TC_FAIL, "can't open file for reading");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else
    SDK_RPRINT(test, "INKfopen", "TestCase1", TC_PASS, "ok");

  // Create unique tmp _file_name_, do not use any TS file_name
  snprintf(write_file_name, PATH_NAME_MAX, "/tmp/%sXXXXXX", PFX);
  int write_file_fd;            // this file will be reopened below
  if ((write_file_fd = mkstemp(write_file_name)) <= 0) {
    SDK_RPRINT(test, "mkstemp", "std func", TC_FAIL, "can't create file for writing");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    return;
  }
  close(write_file_fd);

  // open file for writing, the file doesn't have to exist.
  if (!(write_file = INKfopen(write_file_name, "w"))) {
    SDK_RPRINT(test, "INKfopen", "TestCase2", TC_FAIL, "can't open file for writing");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    return;
  }
  SDK_RPRINT(test, "INKfopen", "TestCase2", TC_PASS, "ok");

  memset(input_buffer, '\0', BUFSIZ);

  // source_read_file and input_file_full_path are the same file
  if (stat(input_file_full_path, &stat_buffer_input) != 0) {
    SDK_RPRINT(test, "stat", "std func", TC_FAIL, "source file and input file messed up");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  read_amount = (stat_buffer_input.st_size <= (off_t)sizeof(input_buffer)) ?
    (stat_buffer_input.st_size) : (sizeof(input_buffer));

  // INKfgets
  if ((ret_val = INKfgets(source_read_file, input_buffer, read_amount))
      == NULL) {
    SDK_RPRINT(test, "INKfgets", "TestCase1", TC_FAIL, "can't read from file");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  } else {
    if (ret_val != input_buffer) {
      SDK_RPRINT(test, "INKfgets", "TestCase2", TC_FAIL, "reading error");
      error_counter++;

      // no need to continue, return
      *pstatus = REGRESSION_TEST_FAILED;
      if (source_read_file != NULL)
        INKfclose(source_read_file);
      if (write_file != NULL)
        INKfclose(write_file);
      return;
    } else
      SDK_RPRINT(test, "INKfgets", "TestCase1", TC_PASS, "ok");
  }

  // INKfwrite
  wrote = INKfwrite(write_file, input_buffer, read_amount);
  if (wrote != read_amount) {
    SDK_RPRINT(test, "INKfwrite", "TestCase1", TC_FAIL, "writing error");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  SDK_RPRINT(test, "INKfwrite", "TestCase1", TC_PASS, "ok");

  // INKfflush
  if (stat(write_file_name, &stat_buffer_pre) != 0) {
    SDK_RPRINT(test, "stat", "std func", TC_FAIL, "INKfwrite error");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  INKfflush(write_file);        // write_file should point to write_file_name

  if (stat(write_file_name, &stat_buffer_post) != 0) {
    SDK_RPRINT(test, "stat", "std func", TC_FAIL, "INKfflush error");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  if ((stat_buffer_pre.st_size == 0) && (stat_buffer_post.st_size == read_amount)) {
    SDK_RPRINT(test, "INKfflush", "TestCase1", TC_PASS, "ok");
  } else {
    SDK_RPRINT(test, "INKfflush", "TestCase1", TC_FAIL, "INKfflush error");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  // INKfread
  // open again for reading
  cmp_read_file = INKfopen(write_file_name, "r");
  if (cmp_read_file == NULL) {
    SDK_RPRINT(test, "INKfopen", "TestCase3", TC_FAIL, "can't open file for reading");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    return;
  }

  read_amount = (stat_buffer_input.st_size <= (off_t)sizeof(cmp_buffer)) ? (stat_buffer_input.st_size) : (sizeof(cmp_buffer));

  // INKfread on read file
  read = INKfread(cmp_read_file, cmp_buffer, read_amount);
  if (read != read_amount) {
    SDK_RPRINT(test, "INKfread", "TestCase1", TC_FAIL, "can't reading");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    if (cmp_read_file != NULL)
      INKfclose(cmp_read_file);
    return;
  } else
    SDK_RPRINT(test, "INKfread", "TestCase1", TC_PASS, "ok");

  // compare input_buffer and cmp_buffer buffers
  if (memcmp(input_buffer, cmp_buffer, read_amount) != 0) {
    SDK_RPRINT(test, "INKfread", "TestCase2", TC_FAIL, "reading error");
    error_counter++;

    // no need to continue, return
    *pstatus = REGRESSION_TEST_FAILED;
    if (source_read_file != NULL)
      INKfclose(source_read_file);
    if (write_file != NULL)
      INKfclose(write_file);
    if (cmp_read_file != NULL)
      INKfclose(cmp_read_file);
    return;
  } else
    SDK_RPRINT(test, "INKfread", "TestCase2", TC_PASS, "ok");

  // remove the tmp file
  if (unlink(write_file_name) != 0) {
    SDK_RPRINT(test, "unlink", "std func", TC_FAIL, "can't remove temp file");
  }
  // INKfclose on read file
  INKfclose(source_read_file);
  SDK_RPRINT(test, "INKfclose", "TestCase1", TC_PASS, "ok");

  // INKfclose on write file
  INKfclose(write_file);
  SDK_RPRINT(test, "INKfclose", "TestCase2", TC_PASS, "ok");

  if (error_counter == 0) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }
  if (cmp_read_file != NULL)
    INKfclose(cmp_read_file);
}

/* INKThread */

//////////////////////////////////////////////
//       SDK_API_INKThread
//
// Unit Test for API: INKThread
//                    INKThreadCreate
//                    INKThreadSelf
//////////////////////////////////////////////
static int thread_err_count = 0;
static RegressionTest *SDK_Thread_test;
static int *SDK_Thread_pstatus;
static void *thread_create_handler(void *arg);

static void *
thread_create_handler(void *arg)
{
  NOWARN_UNUSED(arg);
  INKThread INKthread;
  //Fix me: do more useful work
  sleep(10);

  INKthread = INKThreadSelf();
  if (INKthread == 0) {
    thread_err_count++;
    SDK_RPRINT(SDK_Thread_test, "INKThreadCreate", "TestCase2", TC_FAIL, "can't get thread");
  } else {
    SDK_RPRINT(SDK_Thread_test, "INKThreadCreate", "TestCase2", TC_PASS, "ok");
  }

  if (thread_err_count > 0)
    *SDK_Thread_pstatus = REGRESSION_TEST_FAILED;
  else
    *SDK_Thread_pstatus = REGRESSION_TEST_PASSED;

  return NULL;
}

// Fix me: Solaris threads/Win2K threads tests

// Argument data passed to thread init functions
//  cannot be allocated on the stack.

REGRESSION_TEST(SDK_API_INKThread) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  SDK_Thread_test = test;
  SDK_Thread_pstatus = pstatus;

  INKThread curr_thread = 0;
//    INKThread created_thread = 0;
  pthread_t curr_tid;

  curr_tid = pthread_self();

  // INKThreadSelf
  curr_thread = INKThreadSelf();
  if (curr_thread == 0) {
    SDK_RPRINT(test, "INKThreadSelf", "TestCase1", TC_FAIL, "can't get the current thread");
    thread_err_count++;
  } else {
    SDK_RPRINT(test, "INKThreadSelf", "TestCase1", TC_PASS, "ok");
  }

  // INKThreadCreate
  INKThread created_thread = INKThreadCreate(thread_create_handler, (void *) curr_tid);
  if (created_thread == NULL) {
    thread_err_count++;
    SDK_RPRINT(test, "INKThreadCreate", "TestCase1", TC_FAIL, "can't create thread");
  } else {
    SDK_RPRINT(test, "INKThreadCreate", "TestCase1", TC_PASS, "ok");
  }
}


//////////////////////////////////////////////
//       SDK_API_INKThread
//
// Unit Test for API: INKThreadInit
//                    INKThreadDestroy
//////////////////////////////////////////////
static int thread_init_err_count = 0;
static RegressionTest *SDK_ThreadInit_test;
static int *SDK_ThreadInit_pstatus;
static void *pthread_start_func(void *arg);

static void *
pthread_start_func(void *arg)
{
  NOWARN_UNUSED(arg);
  INKThread temp_thread = 0;

  // INKThreadInit
  temp_thread = INKThreadInit();

  if (!temp_thread) {
    SDK_RPRINT(SDK_ThreadInit_test, "INKThreadInit", "TestCase2", TC_FAIL, "can't init thread");
    thread_init_err_count++;
  } else
    SDK_RPRINT(SDK_ThreadInit_test, "INKThreadInit", "TestCase2", TC_PASS, "ok");

  // Clean up this thread
  if (temp_thread)
    INKThreadDestroy(temp_thread);

  if (thread_init_err_count > 0)
    *SDK_ThreadInit_pstatus = REGRESSION_TEST_FAILED;
  else
    *SDK_ThreadInit_pstatus = REGRESSION_TEST_PASSED;

  return NULL;
}

REGRESSION_TEST(SDK_API_INKThreadInit) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  SDK_ThreadInit_test = test;
  SDK_ThreadInit_pstatus = pstatus;

  pthread_t curr_tid, new_tid;

  curr_tid = pthread_self();

  int ret;
  errno = 0;
  ret = pthread_create(&new_tid, NULL, pthread_start_func, (void *) curr_tid);
  if (ret != 0) {
    thread_init_err_count++;
    SDK_RPRINT(test, "INKThreadInit", "TestCase1", TC_FAIL, "can't create pthread");
  } else
    SDK_RPRINT(test, "INKThreadInit", "TestCase1", TC_PASS, "ok");

}

/* Action */

//////////////////////////////////////////////
//       SDK_API_INKAction
//
// Unit Test for API: INKActionCancel
//////////////////////////////////////////////

static RegressionTest *SDK_ActionCancel_test;
static int *SDK_ActionCancel_pstatus;

int
action_cancel_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(edata);
  if (event == INK_EVENT_IMMEDIATE)     // called from schedule_imm OK
  {
    SDK_RPRINT(SDK_ActionCancel_test, "INKActionCancel", "TestCase1", TC_PASS, "ok");
    *SDK_ActionCancel_pstatus = REGRESSION_TEST_PASSED;
  } else if (event == INK_EVENT_TIMEOUT)        //called from schedule_in Not OK.
  {
    SDK_RPRINT(SDK_ActionCancel_test, "INKActionCancel", "TestCase1", TC_FAIL, "bad action");
    *SDK_ActionCancel_pstatus = REGRESSION_TEST_FAILED;
  } else                        // there is sth wrong
  {
    SDK_RPRINT(SDK_ActionCancel_test, "INKActionCancel", "TestCase1", TC_FAIL, "bad event");
    *SDK_ActionCancel_pstatus = REGRESSION_TEST_FAILED;
  }

  INKContDestroy(contp);
  return 0;
}

REGRESSION_TEST(SDK_API_INKActionCancel) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  SDK_ActionCancel_test = test;
  SDK_ActionCancel_pstatus = pstatus;

  INKMutex cont_mutex = INKMutexCreate();
  INKCont contp = INKContCreate(action_cancel_handler, cont_mutex);
  INKAction actionp = INKContSchedule(contp, 10000);

  INKMutexLock(cont_mutex);
  if (INKActionDone(actionp)) {
    *pstatus = REGRESSION_TEST_FAILED;
    INKMutexUnlock(cont_mutex);
    return;
  } else {
    INKActionCancel(actionp);
  }
  INKMutexUnlock(cont_mutex);

  INKContSchedule(contp, 0);
}

//////////////////////////////////////////////
//       SDK_API_INKAction
//
// Unit Test for API: INKActionDone
//////////////////////////////////////////////
/* Currently, don't know how to test it because INKAction
   is at "done" status only "shortly" after finish
   executing action_done_handler. Another possibility is
   to use reentrant call. But in both cases it's not
   guaranteed to get ActionDone.
   */

/* Continuations */

//////////////////////////////////////////////
//       SDK_API_INKCont
//
// Unit Test for API: INKContCreate
//                    INKContCall
//////////////////////////////////////////////

// this is needed for asynchronous APIs
static RegressionTest *SDK_ContCreate_test;
static int *SDK_ContCreate_pstatus;

int
cont_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(contp);
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(edata);
  SDK_RPRINT(SDK_ContCreate_test, "INKContCreate", "TestCase1", TC_PASS, "ok");
  SDK_RPRINT(SDK_ContCreate_test, "INKContCall", "TestCase1", TC_PASS, "ok");

  *SDK_ContCreate_pstatus = REGRESSION_TEST_PASSED;

  return 0;
}


REGRESSION_TEST(SDK_API_INKContCreate) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  // For asynchronous APIs, use static vars to store test and pstatus
  SDK_ContCreate_test = test;
  SDK_ContCreate_pstatus = pstatus;

  INKMutex mutexp = INKMutexCreate();
  INKCont contp = INKContCreate(cont_handler, mutexp);
  int lock = 0;

  INKMutexLockTry(mutexp, &lock);
  if (lock)     //mutex is grabbed
  {
    INKContCall(contp, (INKEvent) 0, NULL);
    INKMutexUnlock(mutexp);
  } else                        //mutex has problems
  {

    SDK_RPRINT(SDK_ContCreate_test, "INKContCreate", "TestCase1", TC_FAIL, "continuation creation has problems");
    SDK_RPRINT(SDK_ContCreate_test, "INKContCall", "TestCase1", TC_FAIL, "continuation has problems");

    *pstatus = REGRESSION_TEST_FAILED;
  }

  INKContDestroy(contp);
}


//////////////////////////////////////////////
//       SDK_API_INKCont
//
// Unit Test for API: INKContDataGet
//                    INKContDataSet
//////////////////////////////////////////////

// this is needed for asynchronous APIs
static RegressionTest *SDK_ContData_test;
static int *SDK_ContData_pstatus;

// this is specific for this test
typedef struct
{
  int data1;
  int data2;
} MyData;

int
cont_data_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(edata);
  MyData *my_data = (MyData *) INKContDataGet(contp);

  if (my_data->data1 == 1 && my_data->data2 == 2) {
    SDK_RPRINT(SDK_ContData_test, "INKContDataSet", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(SDK_ContData_test, "INKContDataGet", "TestCase1", TC_PASS, "ok");

    *SDK_ContData_pstatus = REGRESSION_TEST_PASSED;
  } else {
    // If we get bad data, it's a failure
    SDK_RPRINT(SDK_ContData_test, "INKContDataSet", "TestCase1", TC_FAIL, "bad data");
    SDK_RPRINT(SDK_ContData_test, "INKContDataGet", "TestCase1", TC_FAIL, "bad data");

    *SDK_ContData_pstatus = REGRESSION_TEST_FAILED;
  }

  INKfree(my_data);
  INKContDestroy(contp);
  return 0;
}


REGRESSION_TEST(SDK_API_INKContDataGet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  // For asynchronous APIs, use static vars to store test and pstatus
  SDK_ContData_test = test;
  SDK_ContData_pstatus = pstatus;

  INKCont contp = INKContCreate(cont_data_handler, INKMutexCreate());

  MyData *my_data = (MyData *) INKmalloc(sizeof(MyData));
  my_data->data1 = 1;
  my_data->data2 = 2;

  INKContDataSet(contp, (void *) my_data);

  INKContSchedule(contp, 0);
}



//////////////////////////////////////////////
//       SDK_API_INKCont
//
// Unit Test for API: INKContMutexGet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKContMutexGet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKMutex mutexp_input;
  INKMutex mutexp_output;
  INKCont contp;

  mutexp_input = INKMutexCreate();
  contp = INKContCreate(cont_handler, mutexp_input);

  mutexp_output = INKContMutexGet(contp);

  if (mutexp_input == mutexp_output) {
    SDK_RPRINT(test, "INKContMutexGet", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else
    SDK_RPRINT(test, "INKContMutexGet", "TestCase1", TC_FAIL, "Continutation's mutex corrupted");

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);

  INKContDestroy(contp);

}

//////////////////////////////////////////////
//       SDK_API_INKCont
//
// Unit Test for API: INKContSchedule
//////////////////////////////////////////////

// this is needed for asynchronous APIs
static RegressionTest *SDK_ContSchedule_test;
static int *SDK_ContSchedule_pstatus;

// this is specific for this test
static int tc1_count = 0;
static int tc2_count = 0;

int
cont_schedule_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(edata);
  if (event == INK_EVENT_IMMEDIATE) {
    // Test Case 1
    SDK_RPRINT(SDK_ContSchedule_test, "INKContSchedule", "TestCase1", TC_PASS, "ok");
    tc1_count++;
  } else if (event == INK_EVENT_TIMEOUT) {
    // Test Case 2
    SDK_RPRINT(SDK_ContSchedule_test, "INKContSchedule", "TestCase2", TC_PASS, "ok");
    tc2_count++;
  } else {
    // If we receive a bad event, it's a failure
    SDK_RPRINT(SDK_ContSchedule_test, "INKContSchedule", "TestCase1|2",
               TC_FAIL, "received unexpected event number %d", event);
    *SDK_ContSchedule_pstatus = REGRESSION_TEST_FAILED;
    return 0;
  }

  // We expect to be called once for TC1 and once for TC2
  if ((tc1_count == 1) && (tc2_count == 1)) {
    *SDK_ContSchedule_pstatus = REGRESSION_TEST_PASSED;
  }
  // If TC1 or TC2 executed more than once, something is fishy..
  else if (tc1_count + tc2_count >= 2) {
    *SDK_ContSchedule_pstatus = REGRESSION_TEST_FAILED;
  }

  INKContDestroy(contp);
  return 0;
}

/* Mutex */

/*
   Fix me: test for grabbing the mutex from two
   different threads.
   */

//////////////////////////////////////////////
//       SDK_API_INKMutex
//
// Unit Test for API: INKMutexCreate
//                    INKMutexLock
//                    INKMutexUnLock
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKMutexCreate) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKMutex mutexp = INKMutexCreate();

  INKMutexLock(mutexp);

  /* This is normal because all locking is from the same thread */
  int lock = 0;

  INKMutexLockTry(mutexp, &lock);
  INKMutexLockTry(mutexp, &lock);

  if (lock) {
    SDK_RPRINT(test, "INKMutexCreate", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKMutexLock", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKMutexLockTry", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKMutexCreate", "TestCase1", TC_FAIL, "mutex can't be grabbed twice from the same thread");
    SDK_RPRINT(test, "INKMutexLock", "TestCase1", TC_FAIL, "mutex can't be grabbed twice from the same thread");
    SDK_RPRINT(test, "INKMutexLockTry", "TestCase1", TC_FAIL, "mutex can't be grabbed twice from the same thread");

  }

  INKMutexUnlock(mutexp);
  SDK_RPRINT(test, "INKMutexUnLock", "TestCase1", TC_PASS, "ok");

  if (test_passed) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }

}

/* IOBuffer */

//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferCreate
//                    INKIOBufferWaterMarkGet
//                    INKIOBufferWaterMarkSet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferCreate) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  int64 watermark = 1000;

  INKIOBuffer bufp = INKIOBufferCreate();

  INKIOBufferWaterMarkSet(bufp, watermark);

  watermark = 0;
  INKIOBufferWaterMarkGet(bufp, &watermark);

  if (watermark == 1000) {
    SDK_RPRINT(test, "INKIOBufferCreate", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferWaterMarkGet", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferWaterMarkSet", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferCreate", "TestCase1", TC_FAIL, "watermark failed");
    SDK_RPRINT(test, "INKIOBufferWaterMarkGet", "TestCase1", TC_FAIL, "watermark failed");
    SDK_RPRINT(test, "INKIOBufferWaterMarkSet", "TestCase1", TC_FAIL, "watermark failed");
  }

  INKIOBufferDestroy(bufp);

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;

}


//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferSizedCreate
//                    INKIOBufferProduce
//                    INKIOBufferReaderAlloc
//                    INKIOBufferReaderAvail
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferProduce) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKIOBuffer bufp = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);        //size is 4096

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);

  INKIOBufferProduce(bufp, 10);

  int reader_avail = INKIOBufferReaderAvail(readerp);
  if (reader_avail == 10) {
    SDK_RPRINT(test, "INKIOBufferProduce", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferReaderAlloc", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferReaderAvail", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferProduce", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferReaderAlloc", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferReaderAvail", "TestCase1", TC_FAIL, "failed");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;
}


//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferReaderConsume
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferReaderConsume) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKIOBuffer bufp = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);

  INKIOBufferProduce(bufp, 10);
  INKIOBufferReaderConsume(readerp, 10);

  int reader_avail = INKIOBufferReaderAvail(readerp);
  if (reader_avail == 0) {
    SDK_RPRINT(test, "INKIOBufferReaderConsume", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferReaderConsume", "TestCase1", TC_FAIL, "failed");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;
}

//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferReaderClone
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferReaderClone) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKIOBuffer bufp = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);

  INKIOBufferProduce(bufp, 10);
  INKIOBufferReaderConsume(readerp, 5);

  INKIOBufferReader readerp2 = INKIOBufferReaderClone(readerp);

  int reader_avail = INKIOBufferReaderAvail(readerp2);
  if (reader_avail == 5) {
    SDK_RPRINT(test, "INKIOBufferReaderClone", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferReaderClone", "TestCase1", TC_FAIL, "failed");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;
}

//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferStart
//                    INKIOBufferReaderStart
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferStart) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKIOBuffer bufp = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);

  if (INKIOBufferStart(bufp) == INKIOBufferReaderStart(readerp)) {
    SDK_RPRINT(test, "INKIOBufferStart", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferReaderStart", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferStart", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferReaderStart", "TestCase1", TC_FAIL, "failed");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;
}


//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferCopy
//                    INKIOBufferWrite
//                    INKIOBufferReaderCopy
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferCopy) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  char input_buf[] = "This is the test for INKIOBufferCopy, INKIOBufferWrite, INKIOBufferReaderCopy";
  char output_buf[1024];
  INKIOBuffer bufp = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);
  INKIOBuffer bufp2 = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_4K);

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);
  INKIOBufferWrite(bufp, input_buf, (strlen(input_buf) + 1));
  INKIOBufferCopy(bufp2, readerp, (strlen(input_buf) + 1), 0);
  INKIOBufferReaderCopy(readerp, output_buf, (strlen(input_buf) + 1));

  if (strcmp(input_buf, output_buf) == 0) {
    SDK_RPRINT(test, "INKIOBufferWrite", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferCopy", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferReaderCopy", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferWrite", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferCopy", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferReaderCopy", "TestCase1", TC_FAIL, "failed");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);
  return;
}

//////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBuffer
//                    INKIOBufferWrite
//                    INKIOBufferReaderCopy
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferBlockReadAvail) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed_1 = false;
  bool test_passed_2 = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  int i = 10000;
  INKIOBuffer bufp = INKIOBufferCreate();
  INKIOBufferWrite(bufp, (char*)&i, sizeof(int));
  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);

  int64 avail_write, avail_read;

  // TODO: This is probably not correct any more.
  INKIOBufferBlock blockp = INKIOBufferStart(bufp);

  if ((INKIOBufferBlockWriteStart(blockp, &avail_write) - INKIOBufferBlockReadStart(blockp, readerp, &avail_read)) ==
      sizeof(int)) {
    SDK_RPRINT(test, "INKIOBufferBlockReadStart", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferBlockWriteStart", "TestCase1", TC_PASS, "ok");
    test_passed_1 = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferBlockReadStart", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferBlockWriteStart", "TestCase1", TC_FAIL, "failed");
  }

  if ((INKIOBufferBlockReadAvail(blockp, readerp) + INKIOBufferBlockWriteAvail(blockp)) == 4096) {
    SDK_RPRINT(test, "INKIOBufferBlockReadAvail", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKIOBufferBlockWriteAvail", "TestCase1", TC_PASS, "ok");
    test_passed_2 = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferBlockReadAvail", "TestCase1", TC_FAIL, "failed");
    SDK_RPRINT(test, "INKIOBufferBlockWriteAvail", "TestCase1", TC_FAIL, "failed");
  }

  if (test_passed_1 && test_passed_2) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }

  return;
}

//////////////////////////////////////////////////
//       SDK_API_INKIOBuffer
//
// Unit Test for API: INKIOBufferBlockNext
//////////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKIOBufferBlockNext) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  int i = 10000;
  INKIOBuffer bufp = INKIOBufferCreate();
  INKIOBufferWrite(bufp, (char*)&i, sizeof(int));

  INKIOBufferReader readerp = INKIOBufferReaderAlloc(bufp);
  INKIOBufferBlock blockp = INKIOBufferReaderStart(readerp);

  // TODO: This is probaby not the best of regression tests right now ...
  // Note that this assumes block size is > sizeof(int) bytes.
  if (INKIOBufferBlockNext(blockp) == NULL) {
    SDK_RPRINT(test, "INKIOBufferBlockNext", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKIOBufferBlockNext", "TestCase1", TC_FAIL, "fail");
  }

  if (test_passed) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }

  return;
}



/* Stats */

//////////////////////////////////////////////
//       SDK_API_INKStat
//
// Unit Test for API: INKStatCreate
//                    INKStatIntSet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatIntSet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKStat stat = INKStatCreate("stat_is", INKSTAT_TYPE_INT64);

  INKStatIntSet(stat, 100);
  int64 stat_val;

  INKStatIntGet(stat, &stat_val);

  if (stat_val == 100) {
    SDK_RPRINT(test, "INKStatIntSet", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKStatIntSet", "TestCase1", TC_FAIL, "can't set to correct integer value");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);

}

//////////////////////////////////////////////
//       SDK_API_INKStat
//
// Unit Test for API: INKStatIntAddTo
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatIntAddTo) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKStat stat = INKStatCreate("stat_ia", INKSTAT_TYPE_INT64);

  INKStatIntAddTo(stat, 100);
  int64 stat_val;

  INKStatIntGet(stat, &stat_val);

  if (stat_val == 100) {
    SDK_RPRINT(test, "INKStatIntAddTo", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKStatIntAddTo", "TestCase1", TC_FAIL, "can't add to correct integer value");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);

}

//////////////////////////////////////////////
//       SDK_API_INKStat
//
// Unit Test for API: INKStatFloatAddTo
//                    INKStatFloatGet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatFloatAddTo) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKStat stat = INKStatCreate("stat_fa", INKSTAT_TYPE_FLOAT);

  INKStatFloatAddTo(stat, 100.0);
  float stat_val;
  INKStatFloatGet(stat, &stat_val);

  if (stat_val == 100.0) {
    SDK_RPRINT(test, "INKStatFloatAddTo", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKStatFloatAddTo", "TestCase1", TC_FAIL, "can't add to correct float value");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);

}

//////////////////////////////////////////////
//       SDK_API_INKStat
//
// Unit Test for API: INKStatFloatSet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatFloatSet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKStat stat = INKStatCreate("stat_fs", INKSTAT_TYPE_FLOAT);

  INKStatFloatSet(stat, 100.0);
  float stat_val;
  INKStatFloatGet(stat, &stat_val);

  if (stat_val == 100.0) {
    SDK_RPRINT(test, "INKStatFloatSet", "TestCase1", TC_PASS, "ok");
    test_passed = true;
  } else {
    SDK_RPRINT(test, "INKStatFloatSet", "TestCase1", TC_FAIL, "can't set to correct float value");
  }

  // Status of the whole test
  *pstatus = ((test_passed == true) ? REGRESSION_TEST_PASSED : REGRESSION_TEST_FAILED);

}

//////////////////////////////////////////////
//       SDK_API_INKStat
//
// Unit Test for API: INKStatIncrement
//                    INKStatDecrement
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatIncrement) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  bool test_passed_int_increase = false;
  bool test_passed_int_decrease = false;
  bool test_passed_float_increase = false;
  bool test_passed_float_decrease = false;
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKStat stat_1 = INKStatCreate("stat_1", INKSTAT_TYPE_INT64);
  INKStat stat_2 = INKStatCreate("stat_2", INKSTAT_TYPE_FLOAT);

  INKStatIncrement(stat_1);
  int64 stat1_val;
  INKStatIntGet(stat_1, &stat1_val);

  if (stat1_val == 1) {
    SDK_RPRINT(test, "INKStatIncrement", "TestCase1", TC_PASS, "ok for int stat");
    test_passed_int_increase = true;
  } else {
    SDK_RPRINT(test, "INKStatIncrement", "TestCase1", TC_FAIL, "can't increase to correct integer value");
  }

  INKStatDecrement(stat_1);
  INKStatIntGet(stat_1, &stat1_val);

  if (stat1_val == 0) {
    SDK_RPRINT(test, "INKStatDecrement", "TestCase1", TC_PASS, "ok for int stat");
    test_passed_int_decrease = true;
  } else {
    SDK_RPRINT(test, "INKStatDecrement", "TestCase1", TC_FAIL, "can't decrease to correct integer value");
  }

  INKStatIncrement(stat_2);
  float stat2_val;
  INKStatFloatGet(stat_2, &stat2_val);

  if (stat2_val == 1.0) {
    SDK_RPRINT(test, "INKStatIncrement", "TestCase2", TC_PASS, "ok for float stat");
    test_passed_float_increase = true;
  } else {
    char message[80];
    snprintf(message, sizeof(message), "can't increase to correct float value (1.0 != %.3f)", stat2_val);
    SDK_RPRINT(test, "INKStatIncrement", "TestCase2", TC_FAIL, &message[0]);
  }

  INKStatDecrement(stat_2);
  INKStatFloatGet(stat_2, &stat2_val);

  if (stat2_val == 0.0) {
    SDK_RPRINT(test, "INKStatDecrement", "TestCase2", TC_PASS, "ok for float stat");
    test_passed_float_decrease = true;
  } else {
    SDK_RPRINT(test, "INKStatDecrement", "TestCase2", TC_FAIL, "can't decrease to correct float value");
  }

  // Status of the whole test
  if (test_passed_int_increase && test_passed_int_decrease && test_passed_float_increase && test_passed_float_decrease)
    *pstatus = REGRESSION_TEST_PASSED;
  else
    *pstatus = REGRESSION_TEST_FAILED;

}

////////////////////////////////////////////////////
//       SDK_API_INKCoupledStat
//
// Unit Test for API: INKStatCoupledGlobalCategoryCreate
//                    INKStatCoupledLoacalCopyCreate
//                    INKStatCoupledLoacalCopyDestroy
//                    INKStatCoupledGlobalAdd
//                    INKStatCoupledLocalAdd
//                    INKStatsCoupledUpdate
////////////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKStatCoupled) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  /* Create global category and its stats */
  INKCoupledStat stat_global_category = INKStatCoupledGlobalCategoryCreate("global.category");

  INKStat global_stat_sum = INKStatCoupledGlobalAdd(stat_global_category,
                                                    "global.statsum",
                                                    INKSTAT_TYPE_FLOAT);

  INKStat global_stat_1 = INKStatCoupledGlobalAdd(stat_global_category,
                                                  "global.stat1",
                                                  INKSTAT_TYPE_INT64);

  INKStat global_stat_2 = INKStatCoupledGlobalAdd(stat_global_category,
                                                  "global.stat2",
                                                  INKSTAT_TYPE_INT64);

  /* Create local category and its stats */
  INKCoupledStat stat_local_copy = INKStatCoupledLocalCopyCreate("local.copy",
                                                                 stat_global_category);

  INKStat local_stat_sum = INKStatCoupledLocalAdd(stat_local_copy,
                                                  "local.statsum",
                                                  INKSTAT_TYPE_FLOAT);

  INKStat local_stat_1 = INKStatCoupledLocalAdd(stat_local_copy,
                                                "local.stat1",
                                                INKSTAT_TYPE_INT64);

  INKStat local_stat_2 = INKStatCoupledLocalAdd(stat_local_copy,
                                                "local.stat2",
                                                INKSTAT_TYPE_INT64);

  /* stat operation */
  INKStatIntSet(local_stat_1, 100);
  INKStatIntSet(local_stat_2, 100);
  float local_val_1;
  INKStatFloatGet(local_stat_1, &local_val_1);
  float local_val_2;
  INKStatFloatGet(local_stat_2, &local_val_2);

  INKStatFloatAddTo(local_stat_sum, local_val_1);
  INKStatFloatAddTo(local_stat_sum, local_val_2);
  float local_val_sum;
  INKStatFloatGet(local_stat_sum, &local_val_sum);

  INKStatsCoupledUpdate(stat_local_copy);
  INKStatCoupledLocalCopyDestroy(stat_local_copy);

  float global_val_sum;
  INKStatFloatGet(global_stat_sum, &global_val_sum);
  int64 global_val_1;
  INKStatIntGet(global_stat_1, &global_val_1);
  int64 global_val_2;
  INKStatIntGet(global_stat_2, &global_val_2);

  if (local_val_1 == global_val_1 && local_val_2 == global_val_2 && local_val_sum == global_val_sum) {
    SDK_RPRINT(test, "INKStatCoupledGlobalCategoryCreate", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKStatCoupledGlobalAdd", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKStatCoupledLocalCopyCreate", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKStatCoupledLocalAdd", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKStatCoupledLocalCopyDestroy", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(test, "INKStatCoupledUpdate", "TestCase1", TC_PASS, "ok");
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    SDK_RPRINT(test, "INKStatCoupledGlobalCategoryCreate", "TestCase1", TC_FAIL,
               "global stats' value is not equal to local one");
    SDK_RPRINT(test, "INKStatCoupledGlobalAdd", "TestCase1", TC_FAIL, "global stats' value is not equal to local one");
    SDK_RPRINT(test, "INKStatCoupledLocalCopyCreate", "TestCase1", TC_FAIL,
               "global stats' value is not equal to local one");
    SDK_RPRINT(test, "INKStatCoupledLocalAdd", "TestCase1", TC_FAIL, "global stats' value is not equal to local one");
    SDK_RPRINT(test, "INKStatCoupledLocalCopyDestroy", "TestCase1", TC_FAIL,
               "global stats' value is not equal to local one");
    SDK_RPRINT(test, "INKStatCoupledUpdate", "TestCase1", TC_FAIL, "global stats' value is not equal to local one");

    *pstatus = REGRESSION_TEST_FAILED;
  }
}


REGRESSION_TEST(SDK_API_INKContSchedule) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  // For asynchronous APIs, use static vars to store test and pstatus
  SDK_ContSchedule_test = test;
  SDK_ContSchedule_pstatus = pstatus;

  INKCont contp = INKContCreate(cont_schedule_handler, INKMutexCreate());
  INKCont contp2 = INKContCreate(cont_schedule_handler, INKMutexCreate());

  // Test Case 1: schedule immediate
  INKContSchedule(contp, 0);

  // Test Case 2: schedule in 10ms
  INKContSchedule(contp2, 10);
}

//////////////////////////////////////////////////////////////////////////////
//     SDK_API_HttpHookAdd
//
// Unit Test for API: INKHttpHookAdd
//                    INKHttpTxnReenable
//                    INKHttpTxnClientIPGet
//                    INKHttpTxnServerIPGet
//                    INKHttpTxnClientIncomingPortGet
//                    INKHttpTxnClientRemotePortGet
//                    INKHttpTxnClientReqGet
//                    INKHttpTxnClientRespGet
//                    INKHttpTxnServerReqGet
//                    INKHttpTxnServerRespGet
//////////////////////////////////////////////////////////////////////////////

#define HTTP_HOOK_TEST_REQUEST_ID  1

typedef struct
{
  RegressionTest *regtest;
  int *pstatus;
  SocketServer *os;
  ClientTxn *browser;
  int hook_mask;
  int reenable_mask;
  bool test_client_ip_get;
  bool test_client_incoming_port_get;
  bool test_client_remote_port_get;
  bool test_client_req_get;
  bool test_client_resp_get;
  bool test_server_ip_get;
  bool test_server_req_get;
  bool test_server_resp_get;
  bool test_next_hop_ip_get;

  unsigned int magic;
} SocketTest;


//This func is called by us from mytest_handler to test INKHttpTxnClientIPGet
static int
checkHttpTxnClientIPGet(SocketTest * test, void *data)
{

  int ip;
  INKHttpTxn txnp = (INKHttpTxn) data;
  int actual_ip = LOCAL_IP;     /* 127.0.0.1 is expected because the client is on the same machine */

  ip = INKHttpTxnClientIPGet(txnp);
  if (ip == 0) {
    test->test_client_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIPGet", "TestCase1", TC_FAIL, "INKHttpTxnClientIPGet returns 0");
    return INK_EVENT_CONTINUE;
  }

  if (ntohl(ip) == (uint32_t) actual_ip) {
    test->test_client_ip_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIPGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_client_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIPGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }
  return INK_EVENT_CONTINUE;

}

//This func is called by us from mytest_handler to check for INKHttpTxnNextHopIPGet
static int
checkHttpTxnNextHopIPGet(SocketTest * test, void *data)
{
  INKHttpTxn txnp = (INKHttpTxn) data;
  int actual_ip = LOCAL_IP;     /* 127.0.0.1 is expected because the client is on the same machine */
  int nexthopip;

  nexthopip = INKHttpTxnNextHopIPGet(txnp);
  if (nexthopip == 0) {
    test->test_next_hop_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnNextHopIPGet", "TestCase1", TC_FAIL, "INKHttpTxnNextHopIPGet returns 0");
    return INK_EVENT_CONTINUE;
  }

  if (ntohl(nexthopip) == (uint32_t) actual_ip) {
    test->test_next_hop_ip_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnNextHopIPGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_next_hop_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnNextHopIPGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}



//This func is called by us from mytest_handler to test INKHttpTxnServerIPGet
static int
checkHttpTxnServerIPGet(SocketTest * test, void *data)
{

  int ip;
  INKHttpTxn txnp = (INKHttpTxn) data;
  int actual_ip = ntohl(LOCAL_IP);      /* 127.0.0.1 is expected because the client is on the same machine */

  ip = INKHttpTxnServerIPGet(txnp);
  if (ip == 0) {
    test->test_server_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerIPGet", "TestCase1", TC_FAIL, "INKHttpTxnServerIPGet returns 0");
    return INK_EVENT_CONTINUE;
  }

  if (ip == actual_ip) {
    test->test_server_ip_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerIPGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_server_ip_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerIPGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }


  return INK_EVENT_CONTINUE;

}

//This func is called by us from mytest_handler to test INKHttpTxnClientIncomingPortGet
static int
checkHttpTxnClientIncomingPortGet(SocketTest * test, void *data)
{

  int port = -1;
  INKMgmtInt port_from_config_file = -1;
  INKHttpTxn txnp = (INKHttpTxn) data;

  if ((port = INKHttpTxnClientIncomingPortGet(txnp)) < 0) {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIncomingPortGet", "TestCase1", TC_FAIL,
               "INKHttpTxnClientIncomingPortGet returns INK_ERROR");
    test->test_client_incoming_port_get = false;
    return INK_EVENT_CONTINUE;
  }

  if (INKMgmtIntGet("proxy.config.http.server_port", &port_from_config_file) == 0) {
    port_from_config_file = 8080;
  }

  INKDebug(UTDBG_TAG, "TS HTTP port = %x, Txn incoming client port %x", (int) port_from_config_file, port);

  if (port == (int) port_from_config_file) {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIncomingPortGet", "TestCase1", TC_PASS, "ok");
    test->test_client_incoming_port_get = true;
  } else {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientIncomingPortGet", "TestCase1", TC_FAIL,
               "Value's Mismatch. From Funtion: %d  Expected value: %d", port, port_from_config_file);
    test->test_client_incoming_port_get = false;
  }
  return INK_EVENT_CONTINUE;
}

//This func is called by us from mytest_handler to test INKHttpTxnClientRemotePortGet
static int
checkHttpTxnClientRemotePortGet(SocketTest * test, void *data)
{

  int port = -1;
  int browser_port = -1;
  INKHttpTxn txnp = (INKHttpTxn) data;

  browser_port = test->browser->local_port;

  if (INKHttpTxnClientRemotePortGet(txnp, &port) != INK_SUCCESS) {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRemotePortGet", "TestCase1", TC_FAIL,
               "INKHttpTxnClientRemotePortGet doesn't return INK_SUCCESS");
    test->test_client_remote_port_get = false;
    return INK_EVENT_CONTINUE;
  }

  INKDebug(UTDBG_TAG, "Browser port = %x, Txn remote port = %x", browser_port, port);

  if ((int)ntohs(port) == browser_port) {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRemotePortGet", "TestCase1", TC_PASS, "ok");
    test->test_client_remote_port_get = true;
  } else {
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRemotePortGet", "TestCase1", TC_FAIL,
               "Value's Mismatch. From Function: %d Expected Value: %d", ntohs(port), browser_port);
    test->test_client_remote_port_get = false;
  }
  return INK_EVENT_CONTINUE;
}

// This func is called by us from mytest_handler to test INKHttpTxnClientReqGet
static int
checkHttpTxnClientReqGet(SocketTest * test, void *data)
{

  INKMBuffer bufp;
  INKMLoc mloc;
  INKHttpTxn txnp = (INKHttpTxn) data;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &mloc)) {
    test->test_client_req_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientReqGet", "TestCase1", TC_FAIL, "Unable to get handle to client request");
    return INK_EVENT_CONTINUE;
  }


  if ((bufp == (&((HttpSM *) txnp)->t_state.hdr_info.client_request)) &&
      (mloc == (((HttpSM *) txnp)->t_state.hdr_info.client_request.m_http))
    ) {
    test->test_client_req_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientReqGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_client_req_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientReqGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}

// This func is called by us from mytest_handler to test INKHttpTxnClientRespGet
static int
checkHttpTxnClientRespGet(SocketTest * test, void *data)
{

  INKMBuffer bufp;
  INKMLoc mloc;
  INKHttpTxn txnp = (INKHttpTxn) data;

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &mloc)) {
    test->test_client_resp_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRespGet", "TestCase1", TC_FAIL,
               "Unable to get handle to client response");
    return INK_EVENT_CONTINUE;
  }

  if ((bufp == (&((HttpSM *) txnp)->t_state.hdr_info.client_response)) &&
      (mloc == (((HttpSM *) txnp)->t_state.hdr_info.client_response.m_http))
    ) {
    test->test_client_resp_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRespGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_client_resp_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnClientRespGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}

// This func is called by us from mytest_handler to test INKHttpTxnServerReqGet
static int
checkHttpTxnServerReqGet(SocketTest * test, void *data)
{

  INKMBuffer bufp;
  INKMLoc mloc;
  INKHttpTxn txnp = (INKHttpTxn) data;

  if (!INKHttpTxnServerReqGet(txnp, &bufp, &mloc)) {
    test->test_server_req_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerReqGet", "TestCase1", TC_FAIL, "Unable to get handle to server request");
    return INK_EVENT_CONTINUE;
  }

  if ((bufp == (&((HttpSM *) txnp)->t_state.hdr_info.server_request)) &&
      (mloc == (((HttpSM *) txnp)->t_state.hdr_info.server_request.m_http))
    ) {
    test->test_server_req_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerReqGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_server_req_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerReqGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}

// This func is called by us from mytest_handler to test INKHttpTxnServerRespGet
static int
checkHttpTxnServerRespGet(SocketTest * test, void *data)
{

  INKMBuffer bufp;
  INKMLoc mloc;
  INKHttpTxn txnp = (INKHttpTxn) data;

  if (!INKHttpTxnServerRespGet(txnp, &bufp, &mloc)) {
    test->test_server_resp_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerRespGet", "TestCase1", TC_FAIL,
               "Unable to get handle to server response");
    return INK_EVENT_CONTINUE;
  }

  if ((bufp == (&((HttpSM *) txnp)->t_state.hdr_info.server_response)) &&
      (mloc == (((HttpSM *) txnp)->t_state.hdr_info.server_response.m_http))
    ) {
    test->test_server_resp_get = true;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerRespGet", "TestCase1", TC_PASS, "ok");
  } else {
    test->test_server_resp_get = false;
    SDK_RPRINT(test->regtest, "INKHttpTxnServerRespGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}


// This func is called both by us when scheduling EVENT_IMMEDIATE
// And by HTTP SM for registered hooks
static int
mytest_handler(INKCont contp, INKEvent event, void *data)
{
  SocketTest *test = (SocketTest *) INKContDataGet(contp);
  if (test == NULL) {
    if ((event == INK_EVENT_IMMEDIATE) || (event == INK_EVENT_TIMEOUT)) {
      return 0;
    }
    INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE);
    return 0;
  }
  INKAssert(test->magic == MAGIC_ALIVE);
  INKAssert(test->browser->magic == MAGIC_ALIVE);

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    if (test->hook_mask == 0) {
      test->hook_mask |= 1;
    }

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 1;
    }
    break;

  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    if (test->hook_mask == 1) {
      test->hook_mask |= 2;
    }

    checkHttpTxnClientReqGet(test, data);

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 2;
    }

    break;

  case INK_EVENT_HTTP_OS_DNS:
    if (test->hook_mask == 3) {
      test->hook_mask |= 4;
    }

    checkHttpTxnClientIncomingPortGet(test, data);
    checkHttpTxnClientRemotePortGet(test, data);

    checkHttpTxnClientIPGet(test, data);
    checkHttpTxnServerIPGet(test, data);

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 4;
    }
    break;

  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (test->hook_mask == 7) {
      test->hook_mask |= 8;
    }
    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 8;
    }
    break;

  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    if (test->hook_mask == 15) {
      test->hook_mask |= 16;
    }

    checkHttpTxnServerReqGet(test, data);
    checkHttpTxnNextHopIPGet(test, data);

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 16;
    }


    break;

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (test->hook_mask == 31) {
      test->hook_mask |= 32;
    }
    checkHttpTxnServerRespGet(test, data);

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 32;
    }

    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    if (test->hook_mask == 63) {
      test->hook_mask |= 64;
    }

    checkHttpTxnClientRespGet(test, data);

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 64;
    }

    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    if (test->hook_mask == 127) {
      test->hook_mask |= 128;
    }

    if (INKHttpTxnReenable((INKHttpTxn) data, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL,
                 "INKHttpTxnReenable doesn't return INK_SUCCESS");
    } else {
      test->reenable_mask |= 128;
    }
    break;

  case INK_EVENT_IMMEDIATE:
  case INK_EVENT_TIMEOUT:
    /* Browser still waiting the response ? */
    if (test->browser->status == REQUEST_INPROGRESS) {
      INKContSchedule(contp, 25);
    }
    /* Browser got the response. test is over. clean up */
    else {
      /* Note: response is available using test->browser->response pointer */
      if ((test->browser->status == REQUEST_SUCCESS) && (test->hook_mask == 255)) {
        *(test->pstatus) = REGRESSION_TEST_PASSED;
        SDK_RPRINT(test->regtest, "INKHttpHookAdd", "TestCase1", TC_PASS, "ok");

      } else {
        *(test->pstatus) = REGRESSION_TEST_FAILED;
        SDK_RPRINT(test->regtest, "INKHttpHookAdd", "TestCase1", TC_FAIL,
                   "Hooks not called or request failure. Hook mask = %d", test->hook_mask);
      }

      if (test->reenable_mask == 255) {
        SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_PASS, "ok");

      } else {
        *(test->pstatus) = REGRESSION_TEST_FAILED;
        SDK_RPRINT(test->regtest, "INKHttpTxnReenable", "TestCase1", TC_FAIL, "Txn not reenabled properly");

      }

      if ((test->test_client_ip_get != true) ||
          (test->test_client_incoming_port_get != true) ||
          (test->test_client_remote_port_get != true) ||
          (test->test_client_req_get != true) ||
          (test->test_client_resp_get != true) ||
          (test->test_server_ip_get != true) ||
          (test->test_server_req_get != true) ||
          (test->test_server_resp_get != true) || (test->test_next_hop_ip_get != true)
        ) {
        *(test->pstatus) = REGRESSION_TEST_FAILED;
      }
      // transaction is over. clean up.
      synclient_txn_delete(test->browser);
      synserver_delete(test->os);

      test->magic = MAGIC_DEAD;
      INKfree(test);
      INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(test->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(test->regtest, "INKHttpHookAdd", "TestCase1", TC_FAIL, "Unexpected event %d", event);
    break;
  }

  return INK_EVENT_IMMEDIATE;
}



EXCLUSIVE_REGRESSION_TEST(SDK_API_HttpHookAdd) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKCont cont = INKContCreate(mytest_handler, INKMutexCreate());
  SocketTest *socktest = (SocketTest *) INKmalloc(sizeof(SocketTest));

  socktest->regtest = test;
  socktest->pstatus = pstatus;
  socktest->hook_mask = 0;
  socktest->reenable_mask = 0;
  socktest->test_client_ip_get = false;
  socktest->test_client_incoming_port_get = false;
  socktest->test_client_req_get = false;
  socktest->test_client_resp_get = false;
  socktest->test_server_ip_get = false;
  socktest->test_server_req_get = false;
  socktest->test_server_resp_get = false;
  socktest->test_next_hop_ip_get = false;
  socktest->magic = MAGIC_ALIVE;
  INKContDataSet(cont, socktest);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_SEND_REQUEST_HDR_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_TXN_CLOSE_HOOK, cont);

  /* Create a new synthetic server */
  socktest->os = synserver_create(SYNSERVER_LISTEN_PORT);
  synserver_start(socktest->os);

  /* Create a client transaction */
  socktest->browser = synclient_txn_create();
  char *request = generate_request(HTTP_HOOK_TEST_REQUEST_ID);  // this request has a no-cache that prevents caching
  synclient_txn_send_request(socktest->browser, request);
  INKfree(request);

  /* Wait until transaction is done */
  if (socktest->browser->status == REQUEST_INPROGRESS) {
    INKContSchedule(cont, 25);
  }

  return;
}


//////////////////////////////////////////////
//       SDK_API_INKUrl
//
// Unit Test for API: INKUrlCreate
//                    INKUrlDestroy
//                    INKUrlSchemeGet
//                    INKUrlSchemeSet
//                    INKUrlUserGet
//                    INKUrlUserSet
//                    INKUrlPasswordGet
//                    INKUrlPasswordSet
//                    INKUrlHostGet
//                    INKUrlHostSet
//                    INKUrlPortGet
//                    INKUrlPortSet
//                    INKUrlPathGet
//                    INKUrlPathSet
//                    INKUrlHttpParamsGet
//                    INKUrlHttpParamsSet
//                    INKUrlHttpQueryGet
//                    INKUrlHttpQuerySet
//                    INKUrlHttpFragmentGet
//                    INKUrlHttpFragmentSet
//                    INKUrlCopy
//                    INKUrlClone
//                    INKUrlStringGet
//                    INKUrlPrint
//                    INKUrlLengthGet
//                    INKUrlFtpTypeGet
//                    INKUrlFtpTypeSet
//////////////////////////////////////////////

char *
test_url_print(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int64 block_avail;

  char *output_string;
  int output_len;

  output_buffer = INKIOBufferCreate();

  if (!output_buffer) {
    INKError("couldn't allocate IOBuffer\n");
  }

  reader = INKIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  if (INKUrlPrint(bufp, hdr_loc, output_buffer) != INK_SUCCESS) {
    return NULL;
  }

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);
  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    INKIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  INKIOBufferReaderFree(reader);
  INKIOBufferDestroy(output_buffer);

  return output_string;
}

REGRESSION_TEST(SDK_API_INKUrl) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  INKMBuffer bufp1 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp2 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp3 = (INKMBuffer) INK_ERROR_PTR;
  INKMLoc url_loc1;
  INKMLoc url_loc2;
  INKMLoc url_loc3;
  const char *scheme = INK_URL_SCHEME_HTTP;
  const char *scheme_get;
  const char *user = "yyy";
  const char *user_get;
  const char *password = "xxx";
  const char *password_get;
  const char *host = "www.example.com";
  const char *host_get;
  int port = 2021;
  char port_char[10];
  int port_get = 80;
  const char *path = "about/overview.html";
  const char *path_get;
  const char *params = "abcdef";
  const char *params_get;
  const char *query = "name=xxx";
  const char *query_get;
  const char *fragment = "yyy";
  const char *fragment_get;
  char *url_expected_string;
  char *url_string_from_1 = (char *) INK_ERROR_PTR;
  char *url_string_from_2 = (char *) INK_ERROR_PTR;
  char *url_string_from_3 = (char *) INK_ERROR_PTR;
  char *url_string_from_print = (char *) INK_ERROR_PTR;
  int url_expected_length;
  int url_length_from_1;
  int url_length_from_2;
  int type = 'a';
  int type_get;

  bool test_passed_create = false;
  bool test_passed_destroy = false;
  bool test_passed_scheme = false;
  bool test_passed_user = false;
  bool test_passed_password = false;
  bool test_passed_host = false;
  bool test_passed_port = false;
  bool test_passed_path = false;
  bool test_passed_params = false;
  bool test_passed_query = false;
  bool test_passed_fragment = false;
  bool test_passed_copy = false;
  bool test_passed_clone = false;
  bool test_passed_string1 = false;
  bool test_passed_string2 = false;
  bool test_passed_print = false;
  bool test_passed_length1 = false;
  bool test_passed_length2 = false;
  bool test_passed_type = false;

  int length;

  *pstatus = REGRESSION_TEST_INPROGRESS;

  //Initialization
  memset(port_char, 0, 10);
  snprintf(port_char, sizeof(port_char), "%d", port);

  // HTTP URL

  url_expected_length = strlen(scheme) + strlen("://") +
    ((user == NULL) ? 0 : strlen(user)) +
    ((password == NULL) ? ((user == NULL) ? 0 : strlen("@")) : strlen(":") + strlen(password) + strlen("@")) +
    strlen(host) +
    ((port == 80) ? 0 : strlen(port_char) + strlen(":")) +
    strlen("/") + strlen(path) +
    ((params == NULL) ? 0 : strlen(";") + strlen(params)) +
    ((query == NULL) ? 0 : strlen("?") + strlen(query)) + ((fragment == NULL) ? 0 : strlen("#") + strlen(fragment));

  size_t len = url_expected_length + 1;
  url_expected_string = (char *) INKmalloc(len * sizeof(char));
  memset(url_expected_string, 0, url_expected_length + 1);
  snprintf(url_expected_string, len, "%s://%s%s%s%s%s%s%s/%s%s%s%s%s%s%s",
           scheme,
           ((user == NULL) ? "" : user),
           ((password == NULL) ? "" : ":"),
           ((password == NULL) ? "" : password),
           (((user == NULL) && (password == NULL)) ? "" : "@"),
           host,
           ((port == 80) ? "" : ":"),
           ((port == 80) ? "" : port_char),
           ((path == NULL) ? "" : path),
           ((params == NULL) ? "" : ";"),
           ((params == NULL) ? "" : params),
           ((query == NULL) ? "" : "?"),
           ((query == NULL) ? "" : query), ((fragment == NULL) ? "" : "#"), ((fragment == NULL) ? "" : fragment)
    );


  // Set Functions

  if ((bufp1 = INKMBufferCreate()) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKMBufferCreate", "TestCase1", TC_FAIL, "unable to allocate MBuffer.");
    goto print_results;
  };
  if ((url_loc1 = INKUrlCreate(bufp1)) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKUrlCreate", "TestCase1", TC_FAIL, "unable to create URL within buffer.");
    goto print_results;
  }
  //Scheme
  if (INKUrlSchemeSet(bufp1, url_loc1, scheme, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlSchemeSet", "TestCase1", TC_FAIL, "INKUrlSchemeSet Returned INK_ERROR");
  } else {
    if ((scheme_get = INKUrlSchemeGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlSchemeSet|Get", "TestCase1", TC_FAIL, "INKUrlSchemeGet Returned INK_ERROR_PTR");
    } else {
      if (strcmp(scheme_get, scheme) == 0) {
        SDK_RPRINT(test, "INKUrlSchemeSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_scheme = true;
      } else {
        SDK_RPRINT(test, "INKUrlSchemeSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, scheme_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired using INKUrlSchemeGet");
      }
    }
  }

  //User
  if (INKUrlUserSet(bufp1, url_loc1, user, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlUserSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((user_get = INKUrlUserGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlUserSet|Get", "TestCase1", TC_FAIL, "INKUrlUserGet Returned INK_ERROR_PTR");
    } else {
      if (((user_get == NULL) && (user == NULL)) || (strcmp(user_get, user) == 0)) {
        SDK_RPRINT(test, "INKUrlUserSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_user = true;
      } else {
        SDK_RPRINT(test, "INKUrlUserSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, user_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlUserGet");
      }
    }
  }

  // Password
  if (INKUrlPasswordSet(bufp1, url_loc1, password, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlPasswordSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((password_get = INKUrlPasswordGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlPasswordSet|Get", "TestCase1", TC_FAIL, "INKUrlPasswordGet Returned INK_ERROR_PTR");
    } else {
      if (((password_get == NULL) && (password == NULL)) || (strcmp(password_get, password) == 0)) {
        SDK_RPRINT(test, "INKUrlPasswordSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_password = true;
      } else {
        SDK_RPRINT(test, "INKUrlPasswordSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, password_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to release handle to string acquired by INKUrlPasswordGet");
      }
    }
  }

  //Host
  if (INKUrlHostSet(bufp1, url_loc1, host, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlHostSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((host_get = INKUrlHostGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlHostSet|Get", "TestCase1", TC_FAIL, "INKUrlHostGet Returned INK_ERROR_PTR");
    } else {
      if (strcmp(host_get, host) == 0) {
        SDK_RPRINT(test, "INKUrlHostSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_host = true;
      } else {
        SDK_RPRINT(test, "INKUrlHostSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, host_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlHostGet");
      }
    }
  }

  //Port
  if (INKUrlPortSet(bufp1, url_loc1, port) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlPortSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    port_get = 80;
    if ((port_get = INKUrlPortGet(bufp1, url_loc1)) == INK_ERROR) {
      SDK_RPRINT(test, "INKUrlPortSet|Get", "TestCase1", TC_FAIL, "INKUrlPortGet Returned INK_ERROR");
    } else {
      if (port_get == port) {
        SDK_RPRINT(test, "INKUrlPortSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_port = true;
      } else {
        SDK_RPRINT(test, "INKUrlPortSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
    }
  }

  //Path
  if (INKUrlPathSet(bufp1, url_loc1, path, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlPathSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((path_get = INKUrlPathGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlPathSet|Get", "TestCase1", TC_FAIL, "INKUrlPathGet Returned INK_ERROR_PTR");
    } else {
      if (((path == NULL) && (path_get == NULL)) || (strcmp(path, path_get) == 0)) {
        SDK_RPRINT(test, "INKUrlPathSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_path = true;
      } else {
        SDK_RPRINT(test, "INKUrlPathSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, path_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlPathGet");
      }
    }
  }

  //Params
  if (INKUrlHttpParamsSet(bufp1, url_loc1, params, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlHttpParamsSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((params_get = INKUrlHttpParamsGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlHttpParamsSet|Get", "TestCase1", TC_FAIL, "INKUrlHttpParamsGet Returned INK_ERROR_PTR");
    } else {
      if (((params == NULL) && (params_get == NULL)) || (strcmp(params, params_get) == 0)) {
        SDK_RPRINT(test, "INKUrlHttpParamsSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_params = true;
      } else {
        SDK_RPRINT(test, "INKUrlHttpParamsSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, params_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlParamsGet");
      }
    }
  }

  //Query
  if (INKUrlHttpQuerySet(bufp1, url_loc1, query, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlHttpQuerySet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((query_get = INKUrlHttpQueryGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlHttpQuerySet|Get", "TestCase1", TC_FAIL, "INKUrlHttpQueryGet Returned INK_ERROR_PTR");
    } else {
      if (((query == NULL) && (query_get == NULL)) || (strcmp(query, query_get) == 0)) {
        SDK_RPRINT(test, "INKUrlHttpQuerySet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_query = true;
      } else {
        SDK_RPRINT(test, "INKUrlHttpQuerySet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, query_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlQueryGet");
      }
    }
  }

  //Fragments
  if (INKUrlHttpFragmentSet(bufp1, url_loc1, fragment, -1) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlHttpFragmentSet", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    if ((fragment_get = INKUrlHttpFragmentGet(bufp1, url_loc1, &length)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlHttpFragmentSet|Get", "TestCase1", TC_FAIL,
                 "INKUrlHttpFragmentGet Returned INK_ERROR_PTR");
    } else {
      if (((fragment == NULL) && (fragment_get == NULL)) || (strcmp(fragment, fragment_get) == 0)) {
        SDK_RPRINT(test, "INKUrlHttpFragmentSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_fragment = true;
      } else {
        SDK_RPRINT(test, "INKUrlHttpFragmentSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
      if (INKHandleStringRelease(bufp1, url_loc1, fragment_get) != INK_SUCCESS) {
        SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                   "Unable to relase handle to string acquired by INKUrlFragmentGet");
      }
    }
  }

  //Length
  if ((url_length_from_1 = INKUrlLengthGet(bufp1, url_loc1)) == INK_ERROR) {
    SDK_RPRINT(test, "INKUrlLengthGet", "TestCase1", TC_FAIL, "Returns INK_ERROR");
  } else {
    if (url_length_from_1 == url_expected_length) {
      SDK_RPRINT(test, "INKUrlLengthGet", "TestCase1", TC_PASS, "ok");
      test_passed_length1 = true;
    } else {
      SDK_RPRINT(test, "INKUrlLengthGet", "TestCase1", TC_FAIL, "Values don't match");
    }
  }


  //String
  if ((url_string_from_1 = INKUrlStringGet(bufp1, url_loc1, NULL)) == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKUrlStringGet", "TestCase1", TC_FAIL, "Returns INK_ERROR_PTR");
  } else {
    if (strcmp(url_string_from_1, url_expected_string) == 0) {
      SDK_RPRINT(test, "INKUrlStringGet", "TestCase1", TC_PASS, "ok");
      test_passed_string1 = true;
    } else {
      SDK_RPRINT(test, "INKUrlStringGet", "TestCase1", TC_FAIL, "Values don't match");
    }
  }


  //Copy
  if ((bufp2 = INKMBufferCreate()) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKMBufferCreate", "TestCase2", TC_FAIL, "unable to allocate MBuffer for INKUrlCopy.");
    goto print_results;
  };
  if ((url_loc2 = INKUrlCreate(bufp2)) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKUrlCreate", "TestCase2", TC_FAIL, "unable to create URL within buffer for INKUrlCopy.");
    goto print_results;
  }
  if (INKUrlCopy(bufp2, url_loc2, bufp1, url_loc1) == INK_ERROR) {
    SDK_RPRINT(test, "INKUrlCopy", "TestCase1", TC_FAIL, "Returned INK_ERROR");
  } else {
    //Length Test Case 2
    if ((url_length_from_2 = INKUrlLengthGet(bufp2, url_loc2)) == INK_ERROR) {
      SDK_RPRINT(test, "INKUrlLengthGet", "TestCase2", TC_FAIL, "Returns INK_ERROR");
    } else {
      if (url_length_from_2 == url_expected_length) {
        SDK_RPRINT(test, "INKUrlLengthGet", "TestCase2", TC_PASS, "ok");
        test_passed_length2 = true;
      } else {
        SDK_RPRINT(test, "INKUrlCopy", "TestCase1", TC_FAIL, "Values don't match");
      }
    }


    //String Test Case 2
    if ((url_string_from_2 = INKUrlStringGet(bufp2, url_loc2, NULL)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlStringGet", "TestCase2", TC_FAIL, "Returns INK_ERROR_PTR");
    } else {
      if (strcmp(url_string_from_2, url_expected_string) == 0) {
        SDK_RPRINT(test, "INKUrlStringGet", "TestCase2", TC_PASS, "ok");
        test_passed_string2 = true;
      } else {
        SDK_RPRINT(test, "INKUrlStringGet", "TestCase2", TC_FAIL, "Values don't match");
      }
    }

    // Copy Test Case
    if (strcmp(url_string_from_1, url_string_from_2) == 0) {
      SDK_RPRINT(test, "INKUrlCopy", "TestCase1", TC_PASS, "ok");
      test_passed_copy = true;
    } else {
      SDK_RPRINT(test, "INKUrlCopy", "TestCase1", TC_FAIL, "Values Don't Match");
    }
  }

  //Clone
  if ((bufp3 = INKMBufferCreate()) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKMBufferCreate", "TestCase2", TC_FAIL, "unable to allocate MBuffer for INKUrlClone.");
    goto print_results;
  };
  if ((url_loc3 = INKUrlClone(bufp3, bufp1, url_loc1)) == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKUrlClone", "TestCase1", TC_FAIL, "Returned INK_ERROR_PTR");
  } else {
    //String Test Case 2
    if ((url_string_from_3 = INKUrlStringGet(bufp3, url_loc3, NULL)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlClone", "TestCase2", TC_FAIL, "INKUrlStringGet Returns INK_ERROR_PTR");
    } else {
      // Copy Test Case
      if (strcmp(url_string_from_1, url_string_from_3) == 0) {
        SDK_RPRINT(test, "INKUrlClone", "TestCase1", TC_PASS, "ok");
        test_passed_clone = true;
      } else {
        SDK_RPRINT(test, "INKUrlClone", "TestCase1", TC_FAIL, "Values Don't Match");
      }
    }
  }

  //UrlPrint
  url_string_from_print = test_url_print(bufp1, url_loc1);
  if (url_string_from_print == NULL) {
    SDK_RPRINT(test, "INKUrlPrint", "TestCase1", TC_FAIL, "INKUrlPrint doesn't return INK_SUCCESS");
  } else {
    if (strcmp(url_string_from_print, url_expected_string) == 0) {
      SDK_RPRINT(test, "INKUrlPrint", "TestCase1", TC_PASS, "ok");
      test_passed_print = true;
    } else {
      SDK_RPRINT(test, "INKUrlPrint", "TestCase1", TC_FAIL, "INKUrlPrint doesn't return INK_SUCCESS");
    }
    INKfree(url_string_from_print);
  }

  if (INKUrlFtpTypeSet(bufp1, url_loc1, type) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKUrlFtpTypeSet", "TestCase1", TC_FAIL, "INKUrlFtpTypeSet Returned INK_ERROR");
  } else {
    if ((type_get = INKUrlFtpTypeGet(bufp1, url_loc1)) == INK_ERROR) {
      SDK_RPRINT(test, "INKUrlFtpTypeSet|Get", "TestCase1", TC_FAIL, "INKUrlFtpTypeGet Returned INK_ERROR");
    } else {
      if (type_get == type) {
        SDK_RPRINT(test, "INKUrlFtpTypeSet&Get", "TestCase1", TC_PASS, "ok");
        test_passed_type = true;
      } else {
        SDK_RPRINT(test, "INKUrlFtpTypeSet&Get", "TestCase1", TC_FAIL, "Values don't match");
      }
    }
  }


  if ((INKUrlDestroy(bufp1, url_loc1) == INK_ERROR) &&
      (INKUrlDestroy(bufp2, url_loc2) == INK_ERROR) && (INKUrlDestroy(bufp3, url_loc3) == INK_ERROR)
    ) {
    SDK_RPRINT(test, "INKUrlCreate", "TestCase1&2", TC_PASS, "ok");
    SDK_RPRINT(test, "INKUrlDestroy", "TestCase1|2|3", TC_FAIL, "Returns INK_ERROR");
  } else {
    SDK_RPRINT(test, "INKUrlCreate", "TestCase1&2", TC_PASS, "ok");
    SDK_RPRINT(test, "INKUrlDestroy", "TestCase1&2&3", TC_PASS, "ok");
    INKHandleMLocRelease(bufp1, INK_NULL_MLOC, url_loc1);
    INKHandleMLocRelease(bufp2, INK_NULL_MLOC, url_loc2);
    INKHandleMLocRelease(bufp3, INK_NULL_MLOC, url_loc3);
    test_passed_create = true;
    test_passed_destroy = true;
  }

print_results:
  INKfree(url_expected_string);
  if (url_string_from_1 != INK_ERROR_PTR) {
    INKfree(url_string_from_1);
  }
  if (url_string_from_2 != INK_ERROR_PTR) {
    INKfree(url_string_from_2);
  }
  if (url_string_from_3 != INK_ERROR_PTR) {
    INKfree(url_string_from_3);
  }
  if (bufp1 != INK_ERROR_PTR) {
    INKMBufferDestroy(bufp1);
  }
  if (bufp2 != INK_ERROR_PTR) {
    INKMBufferDestroy(bufp2);
  }
  if (bufp3 != INK_ERROR_PTR) {
    INKMBufferDestroy(bufp3);
  }
  if ((test_passed_create == false) ||
      (test_passed_destroy == false) ||
      (test_passed_scheme == false) ||
      (test_passed_user == false) ||
      (test_passed_password == false) ||
      (test_passed_host == false) ||
      (test_passed_port == false) ||
      (test_passed_path == false) ||
      (test_passed_params == false) ||
      (test_passed_query == false) ||
      (test_passed_fragment == false) ||
      (test_passed_copy == false) ||
      (test_passed_clone == false) ||
      (test_passed_string1 == false) ||
      (test_passed_string2 == false) ||
      (test_passed_print == false) ||
      (test_passed_length1 == false) || (test_passed_length2 == false) || (test_passed_type == false)
    ) {
        /*** Debugging the test itself....
	(test_passed_create == false)?printf("test_passed_create is false\n"):printf("");
	(test_passed_destroy == false)?printf("test_passed_destroy is false\n"):printf("");
	(test_passed_scheme == false)?printf("test_passed_scheme is false\n"):printf("");
	(test_passed_user == false)?printf("test_passed_user is false\n"):printf("");
	(test_passed_password == false)?printf("test_passed_password is false\n"):printf("");
	(test_passed_host == false)?printf("test_passed_host is false\n"):printf("");
	(test_passed_port == false)?printf("test_passed_port is false\n"):printf("");
	(test_passed_path == false)?printf("test_passed_path is false\n"):printf("");
	(test_passed_params == false)?printf("test_passed_params is false\n"):printf("");
	(test_passed_query == false)?printf("test_passed_query is false\n"):printf("");
	(test_passed_fragment == false)?printf("test_passed_fragment is false\n"):printf("");
	(test_passed_copy == false)?printf("test_passed_copy is false\n"):printf("");
	(test_passed_string1 == false)?printf("test_passed_string1 is false\n"):printf("");
	(test_passed_string2 == false)?printf("test_passed_string2 is false\n"):printf("");
	(test_passed_length1 == false)?printf("test_passed_length1 is false\n"):printf("");
	(test_passed_length2 == false)?printf("test_passed_length2 is false\n"):printf("");
	(test_passed_type == false)?printf("test_passed_type is false\n"):printf("");
	.....***********/
    *pstatus = REGRESSION_TEST_FAILED;
  } else {
    *pstatus = REGRESSION_TEST_PASSED;
  }
}

//////////////////////////////////////////////
//       SDK_API_INKHttpHdr
//
// Unit Test for API: INKHttpHdrCreate
//                    INKHttpHdrCopy
//                    INKHttpHdrClone
//                    INKHttpHdrDestroy
//                    INKHttpHdrLengthGet
//                    INKHttpHdrMethodGet
//                    INKHttpHdrMethodSet
//                    INKHttpHdrPrint
//                    INKHttpHdrReasonGet
//                    INKHttpHdrReasonLookup
//                    INKHttpHdrReasonSet
//                    INKHttpHdrStatusGet
//                    INKHttpHdrStatusSet
//                    INKHttpHdrTypeGet
//                    INKHttpHdrUrlGet
//                    INKHttpHdrUrlSet
//////////////////////////////////////////////

/**
 * If you change value of any constant in this function then reflect that change in variable expected_iobuf.
 */
REGRESSION_TEST(SDK_API_INKHttpHdr) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  INKMBuffer bufp1 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp2 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp3 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp4 = (INKMBuffer) INK_ERROR_PTR;

  INKMLoc hdr_loc1 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc hdr_loc2 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc hdr_loc3 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc hdr_loc4 = (INKMLoc) INK_ERROR_PTR;

  INKHttpType hdr1type;
  INKHttpType hdr2type;

  const char *methodGet;

  INKMLoc url_loc;
  INKMLoc url_loc_Get;
  const char *url_host = "www.example.com";
  int url_port = 2345;
  const char *url_path = "abcd/efg/hij.htm";

  const char *response_reason = "aefa";
  const char *response_reason_get;

  INKHttpStatus status_get;

  int version_major = 2;
  int version_minor = 1;
  int version_get;

  /* INKHttpType type1; unused: lv */
  /* INKHttpType type2; unused: lv */
  const char *method1;
  const char *method2;
  int length1;
  int length2;
  INKMLoc url_loc1;
  INKMLoc url_loc2;
  /* int version1; unused: lv */
  /* int version2; unused: lv */

  int length;
  const char *expected_iobuf = "GET http://www.example.com:2345/abcd/efg/hij.htm HTTP/2.1\r\n\r\n";
  int actual_length;
  int expected_length;
  bool test_passed_Http_Hdr_Create = false;
  bool test_passed_Http_Hdr_Type = false;
  bool test_passed_Http_Hdr_Method = false;
  bool test_passed_Http_Hdr_Url = false;
  bool test_passed_Http_Hdr_Status = false;
  bool test_passed_Http_Hdr_Reason = false;
  bool test_passed_Http_Hdr_Reason_Lookup = false;
  bool test_passed_Http_Hdr_Version = false;
  bool test_passed_Http_Hdr_Copy = false;
  bool test_passed_Http_Hdr_Clone = false;
  bool test_passed_Http_Hdr_Length = false;
  bool test_passed_Http_Hdr_Print = false;
  bool test_passed_Http_Hdr_Destroy = false;
  bool try_print_function = true;
  bool test_buffer_created = true;


  *pstatus = REGRESSION_TEST_INPROGRESS;

  if (((bufp1 = INKMBufferCreate()) == INK_ERROR_PTR) ||
      ((bufp2 = INKMBufferCreate()) == INK_ERROR_PTR) ||
      ((bufp3 = INKMBufferCreate()) == INK_ERROR_PTR) || ((bufp4 = INKMBufferCreate()) == INK_ERROR_PTR)
    ) {
    SDK_RPRINT(test, "INKHttpHdr", "All Test Cases", TC_FAIL,
               "INKMBufferCreate returns INK_ERROR_PTR. Cannot test the functions");
    test_buffer_created = true;
  }
  // Create
  if (test_buffer_created == true) {
    if (((hdr_loc1 = INKHttpHdrCreate(bufp1)) == INK_ERROR_PTR) ||
        ((hdr_loc2 = INKHttpHdrCreate(bufp2)) == INK_ERROR_PTR) ||
        ((hdr_loc3 = INKHttpHdrCreate(bufp3)) == INK_ERROR_PTR)
      ) {
      SDK_RPRINT(test, "INKHttpHdrCreate", "TestCase1|2|3", TC_FAIL, "INKHttpHdrCreate returns INK_ERROR_PTR.");
    } else {
      SDK_RPRINT(test, "INKHttpHdrCreate", "TestCase1&2&3", TC_PASS, "ok");
      test_passed_Http_Hdr_Create = true;
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrCreate", "All Test Cases", TC_FAIL, "Cannot run test as unable to allocate MBuffers");
  }


  // Type
  if (test_passed_Http_Hdr_Create == true) {
    if ((INKHttpHdrTypeSet(bufp1, hdr_loc1, INK_HTTP_TYPE_REQUEST) == INK_ERROR) ||
        (INKHttpHdrTypeSet(bufp2, hdr_loc2, INK_HTTP_TYPE_RESPONSE) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKHttpHdrTypeSet", "TestCase1|2", TC_FAIL, "INKHttpHdrTypeSet returns INK_ERROR");
    } else {
      if (((hdr1type = INKHttpHdrTypeGet(bufp1, hdr_loc1)) == (INKHttpType) INK_ERROR) ||
          ((hdr2type = INKHttpHdrTypeGet(bufp2, hdr_loc2)) == (INKHttpType) INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKHttpHdrTypeSet&Get", "TestCase1|2", TC_FAIL, "INKHttpHdrTypeGet returns INK_ERROR");
      } else {
        if ((hdr1type == INK_HTTP_TYPE_REQUEST) && (hdr2type == INK_HTTP_TYPE_RESPONSE)) {
          SDK_RPRINT(test, "INKHttpHdrTypeSet&Get", "TestCase1&2", TC_PASS, "ok");
          test_passed_Http_Hdr_Type = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrTypeSet&Get", "TestCase1&2", TC_FAIL, "Values mismatch");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrTypeSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header Creation Test failed");
  }

  // Method
  if (test_passed_Http_Hdr_Type == true) {
    if (INKHttpHdrMethodSet(bufp1, hdr_loc1, INK_HTTP_METHOD_GET, -1) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrMethodSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrMethodSet returns INK_ERROR");
    } else {
      if ((methodGet = INKHttpHdrMethodGet(bufp1, hdr_loc1, &length)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKHttpHdrMethodSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrMethodGet retuns INK_ERROR_PTR");
      } else {
        if ((strncmp(methodGet, INK_HTTP_METHOD_GET, length) == 0) && (length == (int) strlen(INK_HTTP_METHOD_GET))) {
          SDK_RPRINT(test, "INKHttpHdrMethodSet&Get", "TestCase1", TC_PASS, "ok");
          test_passed_Http_Hdr_Method = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrMethodSet&Get", "TestCase1", TC_FAIL, "Value's mismatch");
        }
        if (INKHandleStringRelease(bufp1, hdr_loc1, methodGet) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                     "Unable to release handle acquired by INKHttpHdrMethodGet");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrMethodSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header's Type cannot be set");
  }

  // Url
  if (test_passed_Http_Hdr_Type == true) {
    if ((url_loc = INKUrlCreate(bufp1)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "TestCase1", TC_FAIL,
                 "Cannot run test as INKUrlCreate returns INK_ERROR_PTR");
    } else {
      if (INKHttpHdrUrlSet(bufp1, hdr_loc1, url_loc) == INK_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrUrlSet returns INK_ERROR");
      } else {
        if ((url_loc_Get = INKHttpHdrUrlGet(bufp1, hdr_loc1)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrUrlGet retuns INK_ERROR_PTR");
        } else {
          if (url_loc == url_loc_Get) {
            SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "TestCase1", TC_PASS, "ok");
            test_passed_Http_Hdr_Url = true;
          } else {
            SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "TestCase1", TC_FAIL, "Value's mismatch");
          }
          if (INKHandleMLocRelease(bufp1, hdr_loc1, url_loc_Get) == INK_ERROR) {
            SDK_RPRINT(test, "INKHandleMLocRelease", "", TC_FAIL, "Unable to release handle to URL");
          }
        }
      }

      // Fill up the URL for Copy Test Case.
      if (INKUrlSchemeSet(bufp1, url_loc, INK_URL_SCHEME_HTTP, -1) == INK_ERROR) {
        SDK_RPRINT(test, "INKUrlSchemeSet", "", TC_FAIL, "Unable to set scheme in URL in the HTTP Header");
        try_print_function = false;
      }
      if (INKUrlHostSet(bufp1, url_loc, url_host, -1) == INK_ERROR) {
        SDK_RPRINT(test, "INKUrlHostSet", "", TC_FAIL, "Unable to set host in URL in the HTTP Header");
        try_print_function = false;
      }
      if (INKUrlPortSet(bufp1, url_loc, url_port) == INK_ERROR) {
        SDK_RPRINT(test, "INKUrlPortSet", "", TC_FAIL, "Unable to set port in URL in the HTTP Header");
        try_print_function = false;
      }
      if (INKUrlPathSet(bufp1, url_loc, url_path, -1) == INK_ERROR) {
        SDK_RPRINT(test, "INKUrlPathSet", "", TC_FAIL, "Unable to set path in URL in the HTTP Header");
        try_print_function = false;
      }
      if (INKHandleMLocRelease(bufp1, hdr_loc1, url_loc) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "", TC_FAIL, "Unable to release handle to URL");
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrUrlSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header's Type cannot be set");
  }

  // Reason
  if (test_passed_Http_Hdr_Type == true) {
    if (INKHttpHdrReasonSet(bufp2, hdr_loc2, response_reason, -1) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrReasonSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrReasonSet returns INK_ERROR");
    } else {
      if ((response_reason_get = INKHttpHdrReasonGet(bufp2, hdr_loc2, &length)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKHttpHdrReasonSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrReasonGet returns INK_ERROR_PTR");
      } else {
        if ((strncmp(response_reason_get, response_reason, length) == 0) && (length == (int) strlen(response_reason))) {
          SDK_RPRINT(test, "INKHttpHdrReasonSet&Get", "TestCase1", TC_PASS, "ok");
          test_passed_Http_Hdr_Reason = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrReasonSet&Get", "TestCase1", TC_FAIL, "Value's mismatch");
        }
        if (INKHandleStringRelease(bufp2, hdr_loc2, response_reason_get) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                     "Unable to release handle to string acquired by INKHttpHdrReasonGet");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrReasonSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header's Type cannot be set");
  }

  // Status
  if (test_passed_Http_Hdr_Type == true) {
    if (INKHttpHdrStatusSet(bufp2, hdr_loc2, INK_HTTP_STATUS_OK) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrStatusSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrStatusSet returns INK_ERROR");
    } else {
      if ((status_get = INKHttpHdrStatusGet(bufp2, hdr_loc2)) == (INKHttpStatus) INK_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrStatusSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrStatusGet returns INK_ERROR");
      } else {
        if (status_get == INK_HTTP_STATUS_OK) {
          SDK_RPRINT(test, "INKHttpHdrStatusSet&Get", "TestCase1", TC_PASS, "ok");
          test_passed_Http_Hdr_Status = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrStatusSet&Get", "TestCase1", TC_FAIL, "Value's mismatch");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrStatusSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header's Type cannot be set");
  }

  //Version
  if (test_passed_Http_Hdr_Type == true) {
    if (INKHttpHdrVersionSet(bufp1, hdr_loc1, INK_HTTP_VERSION(version_major, version_minor)) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrVersionSet returns INK_ERROR");
    } else {
      if ((version_get = INKHttpHdrVersionGet(bufp1, hdr_loc1)) == INK_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase1", TC_FAIL, "INKHttpHdrVersionGet returns INK_ERROR");
      } else {
        if ((version_major == INK_HTTP_MAJOR(version_get)) && (version_minor == INK_HTTP_MINOR(version_get))) {
          SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase1", TC_PASS, "ok");
          test_passed_Http_Hdr_Version = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase1", TC_FAIL, "Value's mismatch");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "All Test Case", TC_FAIL,
               "Cannot run test as Header's Type cannot be set");
  }

  if (test_passed_Http_Hdr_Version == true) {
    if (INKHttpHdrVersionSet(bufp2, hdr_loc2, INK_HTTP_VERSION(version_major, version_minor)) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase2", TC_FAIL, "INKHttpHdrVersionSet returns INK_ERROR");
      test_passed_Http_Hdr_Version = false;
    } else {
      if ((version_get = INKHttpHdrVersionGet(bufp2, hdr_loc2)) == INK_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase2", TC_FAIL, "INKHttpHdrVersionGet returns INK_ERROR");
        test_passed_Http_Hdr_Version = false;
      } else {
        if ((version_major == INK_HTTP_MAJOR(version_get)) && (version_minor == INK_HTTP_MINOR(version_get))) {
          SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase2", TC_PASS, "ok");
        } else {
          SDK_RPRINT(test, "INKHttpHdrVersionSet&Get", "TestCase2", TC_FAIL, "Value's mismatch");
          test_passed_Http_Hdr_Version = false;
        }
      }
    }
  }
  //Reason Lookup
  if (strcmp("None", INKHttpHdrReasonLookup(INK_HTTP_STATUS_NONE)) != 0) {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase1", TC_FAIL,
               "INKHttpHdrReasonLookup returns INK_ERROR_PTR or Value's mismatch");
  } else {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase1", TC_PASS, "ok");
    test_passed_Http_Hdr_Reason_Lookup = true;
  }

  if (strcmp("Ok", INKHttpHdrReasonLookup(INK_HTTP_STATUS_OK)) != 0) {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase2", TC_FAIL,
               "INKHttpHdrReasonLookup returns INK_ERROR_PTR or Value's mismatch");
    if (test_passed_Http_Hdr_Reason_Lookup == true) {
      test_passed_Http_Hdr_Reason_Lookup = false;
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase2", TC_PASS, "ok");
  }

  if (strcmp("Continue", INKHttpHdrReasonLookup(INK_HTTP_STATUS_CONTINUE)) != 0) {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase3", TC_FAIL,
               "INKHttpHdrReasonLookup returns INK_ERROR_PTR or Value's mismatch");
    if (test_passed_Http_Hdr_Reason_Lookup == true) {
      test_passed_Http_Hdr_Reason_Lookup = false;
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase3", TC_PASS, "ok");
  }

  if (strcmp("Not Modified", INKHttpHdrReasonLookup(INK_HTTP_STATUS_NOT_MODIFIED)) != 0) {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase2", TC_FAIL,
               "INKHttpHdrReasonLookup returns INK_ERROR_PTR or Value's mismatch");
    if (test_passed_Http_Hdr_Reason_Lookup == true) {
      test_passed_Http_Hdr_Reason_Lookup = false;
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrReasonLookup", "TestCase4", TC_PASS, "ok");
  }

  // Copy
  if (test_passed_Http_Hdr_Create == true) {
    if (INKHttpHdrCopy(bufp3, hdr_loc3, bufp1, hdr_loc1) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKHttpHdrCopy returns INK_ERROR");
    } else {

      bool flag = true;
      // Check the type
      if (flag == true) {
        INKHttpType type1;
        INKHttpType type2;

        if (((type1 = INKHttpHdrTypeGet(bufp1, hdr_loc1)) == (INKHttpType) INK_ERROR) ||
            ((type2 = INKHttpHdrTypeGet(bufp3, hdr_loc3)) == (INKHttpType) INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKHttpTypeGet returns INK_ERROR.");
          flag = false;
        } else {
          if (type1 != type2) {
            SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "Type mismatch in both headers");
            flag = false;
          }
        }
      }
      // Check the Version
      if (flag == true) {
        int version1;
        int version2;

        if (((version1 = INKHttpHdrVersionGet(bufp1, hdr_loc1)) == INK_ERROR) ||
            ((version2 = INKHttpHdrVersionGet(bufp3, hdr_loc3)) == INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          if (version1 != version2) {
            SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "Version mismatch in both headers");
            flag = false;
          }
        }
      }
      // Check the Method
      if (flag == true) {

        if (((method1 = INKHttpHdrMethodGet(bufp1, hdr_loc1, &length1)) == INK_ERROR_PTR) ||
            ((method2 = INKHttpHdrMethodGet(bufp3, hdr_loc3, &length2)) == INK_ERROR_PTR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          if ((length1 != length2) || (strncmp(method1, method2, length1) != 0)) {
            SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "Method mismatch in both headers");
            flag = false;
          }
          if (INKHandleStringRelease(bufp1, hdr_loc1, method1) == INK_ERROR) {
            SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                       "Error in releasing handle acquired using INKHttpHdrMethodGet");
          }
          if (INKHandleStringRelease(bufp3, hdr_loc3, method2) == INK_ERROR) {
            SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                       "Error in releasing handle acquired using INKHttpHdrMethodGet");
          }
        }
      }
      // Check the URL
      if (flag == true) {

        if (((url_loc1 = INKHttpHdrUrlGet(bufp1, hdr_loc1)) == INK_ERROR_PTR) ||
            ((url_loc2 = INKHttpHdrUrlGet(bufp3, hdr_loc3)) == INK_ERROR_PTR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          const char *scheme1;
          const char *scheme2;

          const char *host1;
          const char *host2;

          int port1;
          int port2;

          const char *path1;
          const char *path2;

          // URL Scheme
          if (((scheme1 = INKUrlSchemeGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
              ((scheme2 = INKUrlSchemeGet(bufp3, url_loc2, &length2)) == INK_ERROR_PTR)
            ) {
            SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKUrlSchemeGet returns INK_ERROR_PTR");
            flag = false;
          } else {
            if ((length1 != length2) || (strncmp(scheme1, scheme2, length1) != 0)) {
              SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                         "Url Scheme has different values in both headers");
              flag = false;
            }
            if ((INKHandleStringRelease(bufp1, url_loc1, scheme1) == INK_ERROR) ||
                (INKHandleStringRelease(bufp3, url_loc2, scheme2) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                         "Error in releasing handle acquired using INKUrlSchemeGet.");
            }
          }



          // URL Host
          if (flag == true) {
            if (((host1 = INKUrlHostGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
                ((host2 = INKUrlHostGet(bufp3, url_loc2, &length2)) == INK_ERROR_PTR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKUrlHostGet returns INK_ERROR_PTR");
              flag = false;
            } else {
              if ((length1 != length2) || (strncmp(host1, host2, length1) != 0)) {
                SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                           "Url Host has different values in both headers");
                flag = false;
              }
              if ((INKHandleStringRelease(bufp1, url_loc1, host1) == INK_ERROR) ||
                  (INKHandleStringRelease(bufp3, url_loc2, host2) == INK_ERROR)
                ) {
                SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                           "Error in releasing handle acquired using INKUrlHostGet");
              }
            }
          }
          // URL Port
          if (flag == true) {
            if (((port1 = INKUrlPortGet(bufp1, url_loc1)) == INK_ERROR) ||
                ((port2 = INKUrlPortGet(bufp3, url_loc2)) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKUrlPortGet returns INK_ERROR");
              flag = false;
            } else {
              if (port1 != port2) {
                SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                           "Url Port has different values in both headers");
                flag = false;
              }
            }
          }
          // URL Path
          if (flag == true) {
            if (((path1 = INKUrlPathGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
                ((path2 = INKUrlPathGet(bufp3, url_loc2, &length2)) == INK_ERROR_PTR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL, "INKUrlPathGet returns INK_ERROR_PTR");
              flag = false;
            } else {
              if ((path1 != NULL) && (path2 != NULL)) {
                if ((length1 != length2) || (strncmp(path1, path2, length1) != 0)) {
                  SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                             "Url Path has different values in both headers");
                  flag = false;
                }
                if ((INKHandleStringRelease(bufp1, url_loc1, path1) == INK_ERROR) ||
                    (INKHandleStringRelease(bufp3, url_loc2, path2) == INK_ERROR)
                  ) {
                  SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                             "Error in releasing handle acquired using INKUrlPathGet");
                }
              } else {
                if (path1 != path2) {
                  SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                             "Url Host has different values in both headers");
                  flag = false;
                }
              }
            }
            if ((INKHandleMLocRelease(bufp1, hdr_loc1, url_loc1) == INK_ERROR) ||
                (INKHandleMLocRelease(bufp3, hdr_loc3, url_loc2) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHandleMLocRelease", "", TC_FAIL,
                         "Unable to release Handle acquired by INKHttpHdrUrlGet");
            }
          }

          if (flag == true) {
            SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_PASS, "ok");
            test_passed_Http_Hdr_Copy = true;
          }
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrCopy", "All Test Cases", TC_PASS, "Cannot run test as INKHttpHdrCreate has failed");
  }

  // Clone
  if (test_passed_Http_Hdr_Create == true) {
    if ((hdr_loc4 = INKHttpHdrClone(bufp4, bufp1, hdr_loc1)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKHttpHdrClone returns INK_ERROR_PTR");
    } else {

      bool flag = true;
      // Check the type
      if (flag == true) {
        INKHttpType type1;
        INKHttpType type2;

        if (((type1 = INKHttpHdrTypeGet(bufp1, hdr_loc1)) == (INKHttpType) INK_ERROR) ||
            ((type2 = INKHttpHdrTypeGet(bufp4, hdr_loc4)) == (INKHttpType) INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKHttpTypeGet returns INK_ERROR.");
          flag = false;
        } else {
          if (type1 != type2) {
            SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "Type mismatch in both headers");
            flag = false;
          }
        }
      }
      // Check the Version
      if (flag == true) {
        int version1;
        int version2;

        if (((version1 = INKHttpHdrVersionGet(bufp1, hdr_loc1)) == INK_ERROR) ||
            ((version2 = INKHttpHdrVersionGet(bufp4, hdr_loc4)) == INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          if (version1 != version2) {
            SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "Version mismatch in both headers");
            flag = false;
          }
        }
      }
      // Check the Method
      if (flag == true) {

        if (((method1 = INKHttpHdrMethodGet(bufp1, hdr_loc1, &length1)) == INK_ERROR_PTR) ||
            ((method2 = INKHttpHdrMethodGet(bufp4, hdr_loc4, &length2)) == INK_ERROR_PTR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          if ((length1 != length2) || (strncmp(method1, method2, length1) != 0)) {
            SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "Method mismatch in both headers");
            flag = false;
          }
          if (INKHandleStringRelease(bufp1, hdr_loc1, method1) == INK_ERROR) {
            SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                       "Error in releasing handle acquired using INKHttpHdrMethodGet");
          }
          if (INKHandleStringRelease(bufp4, hdr_loc4, method2) == INK_ERROR) {
            SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                       "Error in releasing handle acquired using INKHttpHdrMethodGet");
          }
        }
      }
      // Check the URL
      if (flag == true) {

        if (((url_loc1 = INKHttpHdrUrlGet(bufp1, hdr_loc1)) == INK_ERROR_PTR) ||
            ((url_loc2 = INKHttpHdrUrlGet(bufp4, hdr_loc4)) == INK_ERROR_PTR)
          ) {
          SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKHttpVersionGet returns INK_ERROR");
          flag = false;
        } else {
          const char *scheme1;
          const char *scheme2;

          const char *host1;
          const char *host2;

          int port1;
          int port2;

          const char *path1;
          const char *path2;

          // URL Scheme
          if (((scheme1 = INKUrlSchemeGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
              ((scheme2 = INKUrlSchemeGet(bufp4, url_loc2, &length2)) == INK_ERROR_PTR)
            ) {
            SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKUrlSchemeGet returns INK_ERROR_PTR");
            flag = false;
          } else {
            if ((length1 != length2) || (strncmp(scheme1, scheme2, length1) != 0)) {
              SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL,
                         "Url Scheme has different values in both headers");
              flag = false;
            }
            if ((INKHandleStringRelease(bufp1, url_loc1, scheme1) == INK_ERROR) ||
                (INKHandleStringRelease(bufp4, url_loc2, scheme2) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                         "Error in releasing handle acquired using INKUrlSchemeGet.");
            }
          }



          // URL Host
          if (flag == true) {
            if (((host1 = INKUrlHostGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
                ((host2 = INKUrlHostGet(bufp4, url_loc2, &length2)) == INK_ERROR_PTR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKUrlHostGet returns INK_ERROR_PTR");
              flag = false;
            } else {
              if ((length1 != length2) || (strncmp(host1, host2, length1) != 0)) {
                SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL,
                           "Url Host has different values in both headers");
                flag = false;
              }
              if ((INKHandleStringRelease(bufp1, url_loc1, host1) == INK_ERROR) ||
                  (INKHandleStringRelease(bufp4, url_loc2, host2) == INK_ERROR)
                ) {
                SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                           "Error in releasing handle acquired using INKUrlHostGet");
              }
            }
          }
          // URL Port
          if (flag == true) {
            if (((port1 = INKUrlPortGet(bufp1, url_loc1)) == INK_ERROR) ||
                ((port2 = INKUrlPortGet(bufp4, url_loc2)) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKUrlPortGet returns INK_ERROR");
              flag = false;
            } else {
              if (port1 != port2) {
                SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL,
                           "Url Port has different values in both headers");
                flag = false;
              }
            }
          }
          // URL Path
          if (flag == true) {
            if (((path1 = INKUrlPathGet(bufp1, url_loc1, &length1)) == INK_ERROR_PTR) ||
                ((path2 = INKUrlPathGet(bufp4, url_loc2, &length2)) == INK_ERROR_PTR)
              ) {
              SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_FAIL, "INKUrlPathGet returns INK_ERROR_PTR");
              flag = false;
            } else {
              if ((path1 != NULL) && (path2 != NULL)) {
                if ((length1 != length2) || (strncmp(path1, path2, length1) != 0)) {
                  SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                             "Url Path has different values in both headers");
                  flag = false;
                }
                if ((INKHandleStringRelease(bufp1, url_loc1, path1) == INK_ERROR) ||
                    (INKHandleStringRelease(bufp4, url_loc2, path2) == INK_ERROR)
                  ) {
                  SDK_RPRINT(test, "INKHandleStringRelease", "", TC_FAIL,
                             "Error in releasing handle acquired using INKUrlPathGet");
                }
              } else {
                if (path1 != path2) {
                  SDK_RPRINT(test, "INKHttpHdrCopy", "TestCase1", TC_FAIL,
                             "Url Host has different values in both headers");
                  flag = false;
                }
              }
            }
            if ((INKHandleMLocRelease(bufp1, hdr_loc1, url_loc1) == INK_ERROR) ||
                (INKHandleMLocRelease(bufp4, hdr_loc4, url_loc2) == INK_ERROR)
              ) {
              SDK_RPRINT(test, "INKHandleMLocRelease", "", TC_FAIL,
                         "Unable to release Handle acquired by INKHttpHdrUrlGet");
            }
          }

          if (flag == true) {
            SDK_RPRINT(test, "INKHttpHdrClone", "TestCase1", TC_PASS, "ok");
            test_passed_Http_Hdr_Clone = true;
          }
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrClone", "All Test Cases", TC_PASS, "Cannot run test as INKHttpHdrCreate has failed");
  }


  //LengthGet
  if (test_passed_Http_Hdr_Create == true) {
    if ((actual_length = INKHttpHdrLengthGet(bufp1, hdr_loc1)) == INK_ERROR) {
      SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL, "INKHttpHdrLengthGet returns INK_ERROR");
    } else {
      INKIOBuffer iobuf;

      if ((iobuf = INKIOBufferCreate()) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL,
                   "Cannot create iobuffer. Cannot continue with test");
      } else {
        if (INKHttpHdrPrint(bufp1, hdr_loc1, iobuf) == INK_ERROR) {
          SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL, "INKHttpHdrPrint returned INK_ERROR");
        } else {
          INKIOBufferReader iobufreader;
          if ((iobufreader = INKIOBufferReaderAlloc(iobuf)) == INK_ERROR_PTR) {
            SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL, "Cannot allocate a reader to io buffer");
          } else {
            if ((expected_length = INKIOBufferReaderAvail(iobufreader)) == INK_ERROR) {
              SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL,
                         "Cannot calculate the length to be expected.");
            } else {
              if (actual_length == expected_length) {
                SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_PASS, "ok");
                test_passed_Http_Hdr_Length = true;
              } else {
                SDK_RPRINT(test, "INKHttpHdrLengthGet", "TestCase1", TC_FAIL, "Incorrect value returned.");
              }
            }
          }

          // Print.
          if ((test_passed_Http_Hdr_Method == true) && (test_passed_Http_Hdr_Url == true) &&
              (test_passed_Http_Hdr_Version == true) && (test_passed_Http_Hdr_Length == true) &&
              (try_print_function == true)
            ) {
            char *actual_iobuf = NULL;

            actual_iobuf = (char *) INKmalloc((actual_length + 1) * sizeof(char));

            if (actual_iobuf == NULL) {
              SDK_RPRINT(test, "INKHttpHdrPrint", "TestCase1", TC_FAIL, "Unable to allocate memory");
            } else {

              INKIOBufferBlock iobufblock;
              int bytes_read;

              memset(actual_iobuf, 0, (actual_length + 1) * sizeof(char));
              bytes_read = 0;

              iobufblock = INKIOBufferReaderStart(iobufreader);

              while ((iobufblock != NULL) && (iobufblock != INK_ERROR_PTR)) {
                const char *block_start;
                int64 block_size;
                block_start = INKIOBufferBlockReadStart(iobufblock, iobufreader, &block_size);

                if ((block_start == INK_ERROR_PTR) || (block_size == 0) || (block_size == INK_ERROR)) {
                  break;
                }

                memcpy(actual_iobuf + bytes_read, block_start, block_size);
                bytes_read += block_size;

                /*
                   if (INKIOBufferReaderConsume(iobufreader,block_size)==INK_ERROR) {
                   break;
                   }
                 */
                INKIOBufferReaderConsume(iobufreader, block_size);
                iobufblock = INKIOBufferReaderStart(iobufreader);
              }
              if (strcmp(actual_iobuf, expected_iobuf) == 0) {
                SDK_RPRINT(test, "INKHttpHdrPrint", "TestCase1", TC_PASS, "ok");
                test_passed_Http_Hdr_Print = true;
              } else {
                SDK_RPRINT(test, "INKHttpHdrPrint", "TestCase1", TC_FAIL, "Value's mismatch");
              }

              INKfree(actual_iobuf);
              /*
                 if ((INKIOBufferReaderFree(iobufreader)==INK_ERROR) ||
                 (INKIOBufferDestroy(iobuf)==INK_ERROR)
                 ) {
                 SDK_RPRINT(test,"INKIOBuffer","",TC_FAIL,"Unable to free memory");
                 }
               */
              INKIOBufferReaderFree(iobufreader);
              INKIOBufferDestroy(iobuf);
            }
          } else {
            SDK_RPRINT(test, "INKHttpHdrPrint", "TestCase1", TC_FAIL, "Unable to run test for INKHttpHdrPrint");
          }

        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrLengthGet", "All Test Cases", TC_PASS,
               "Cannot run test as INKHttpHdrCreate has failed");
  }

  // Destroy
  if (test_passed_Http_Hdr_Create == true) {
    if ((INKHttpHdrDestroy(bufp1, hdr_loc1) == INK_ERROR) ||
        (INKHttpHdrDestroy(bufp2, hdr_loc2) == INK_ERROR) ||
        (INKHttpHdrDestroy(bufp3, hdr_loc3) == INK_ERROR) || (INKHttpHdrDestroy(bufp4, hdr_loc4) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKHttpHdrDestroy", "TestCase1|2|3|4", TC_FAIL, "INKHttpHdrDestroy returns INK_ERROR.");
    } else {
      if ((INKHandleMLocRelease(bufp1, INK_NULL_MLOC, hdr_loc1) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp2, INK_NULL_MLOC, hdr_loc2) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp3, INK_NULL_MLOC, hdr_loc3) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp4, INK_NULL_MLOC, hdr_loc4) == INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase1|2|3|4", TC_FAIL, "Unable to release the handle to headers");
      }
      SDK_RPRINT(test, "INKHttpHdrDestroy", "TestCase1&2&3&4", TC_PASS, "ok");
      test_passed_Http_Hdr_Destroy = true;
    }
  } else {
    SDK_RPRINT(test, "INKHttpHdrDestroy", "All Test Cases", TC_FAIL, "Cannot run test as header was not created");
  }

  if (bufp1 != INK_ERROR_PTR) {
    if (INKMBufferDestroy(bufp1) == INK_ERROR) {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase1", TC_FAIL, "Unable to destroy MBuffer");
    }
  }

  if (bufp2 != INK_ERROR_PTR) {
    if (INKMBufferDestroy(bufp2) == INK_ERROR) {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase2", TC_FAIL, "Unable to destroy MBuffer");
    }
  }

  if (bufp3 != INK_ERROR_PTR) {
    if (INKMBufferDestroy(bufp3) == INK_ERROR) {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase3", TC_FAIL, "Unable to destroy MBuffer");
    }
  }

  if (bufp4 != INK_ERROR_PTR) {
    if (INKMBufferDestroy(bufp4) == INK_ERROR) {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase4", TC_FAIL, "Unable to destroy MBuffer");
    }
  }

  if ((test_passed_Http_Hdr_Create == true) &&
      (test_passed_Http_Hdr_Type == true) &&
      (test_passed_Http_Hdr_Method == true) &&
      (test_passed_Http_Hdr_Url == true) &&
      (test_passed_Http_Hdr_Status == true) &&
      (test_passed_Http_Hdr_Reason == true) &&
      (test_passed_Http_Hdr_Reason_Lookup == true) &&
      (test_passed_Http_Hdr_Version == true) &&
      (test_passed_Http_Hdr_Copy == true) &&
      (test_passed_Http_Hdr_Clone == true) &&
      (test_passed_Http_Hdr_Length == true) &&
      (test_passed_Http_Hdr_Print == true) && (test_passed_Http_Hdr_Destroy == true)
    ) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }

  return;

}


//////////////////////////////////////////////
//       SDK_API_INKMimeHdrField
//
// Unit Test for API: INKMBufferCreate
//                    INKMBufferDestroy
//                    INKMimeHdrCreate
//                    INKMimeHdrDestroy
//                    INKMimeHdrFieldCreate
//                    INKMimeHdrFieldDestroy
//                    INKMimeHdrFieldFind
//                    INKMimeHdrFieldGet
//                    INKMimeHdrFieldAppend
//                    INKMimeHdrFieldNameGet
//                    INKMimeHdrFieldNameSet
//                    INKMimeHdrFieldNext
//                    INKMimeHdrFieldsClear
//                    INKMimeHdrFieldsCount
//                    INKMimeHdrFieldValueAppend
//                    INKMimeHdrFieldValueDelete
//                    INKMimeHdrFieldValueStringGet
//                    INKMimeHdrFieldValueDateGet
//                    INKMimeHdrFieldValueIntGet
//                    INKMimeHdrFieldValueUintGet
//                    INKMimeHdrFieldValueStringInsert
//                    INKMimeHdrFieldValueDateInsert
//                    INKMimeHdrFieldValueIntInsert
//                    INKMimeHdrFieldValueUintInsert
//                    INKMimeHdrFieldValuesClear
//                    INKMimeHdrFieldValuesCount
//                    INKMimeHdrFieldValueStringSet
//                    INKMimeHdrFieldValueDateSet
//                    INKMimeHdrFieldValueIntSet
//                    INKMimeHdrFieldValueUintSet
//                    INKMimeHdrLengthGet
//                    INKMimeHdrPrint
//////////////////////////////////////////////

INKReturnCode
compare_field_names(RegressionTest * test, INKMBuffer bufp1, INKMLoc mime_loc1, INKMLoc field_loc1, INKMBuffer bufp2,
                    INKMLoc mime_loc2, INKMLoc field_loc2)
{
  const char *name1;
  const char *name2;
  int length1;
  int length2;

  if ((name1 = INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc1, &length1)) == INK_ERROR_PTR) {
    return INK_ERROR;
  }

  if ((name2 = INKMimeHdrFieldNameGet(bufp2, mime_loc2, field_loc2, &length2)) == INK_ERROR_PTR) {
    if (INKHandleStringRelease(bufp1, field_loc1, name1) == INK_ERROR) {
      SDK_RPRINT(test, "", "TestCase1", TC_FAIL, "compare_field_names: Unable to release string handle.");
    }
    return INK_ERROR;
  }

  if ((length1 == length2) && (strncmp(name1, name2, length1) == 0)
    ) {
    if ((INKHandleStringRelease(bufp1, field_loc1, name1) == INK_ERROR) ||
        (INKHandleStringRelease(bufp2, field_loc2, name2) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "", "TestCase2", TC_FAIL, "compare_field_names: Unable to release string handle.");
    }
    return INK_SUCCESS;
  } else {
    if ((INKHandleStringRelease(bufp1, field_loc1, name1) == INK_ERROR) ||
        (INKHandleStringRelease(bufp2, field_loc2, name2) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "", "TestCase3", TC_FAIL, "compare_field_names: Unable to release string handle.");
    }
    return INK_ERROR;
  }
}

REGRESSION_TEST(SDK_API_INKMimeHdrField) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  INKMBuffer bufp1 = (INKMBuffer) INK_ERROR_PTR;

  INKMLoc mime_loc1 = (INKMLoc) INK_ERROR_PTR;

  INKMLoc field_loc11 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc field_loc12 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc field_loc13 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc field_loc14 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc field_loc15 = (INKMLoc) INK_ERROR_PTR;


  const char *field1Name = "field1";
  const char *field2Name = "field2";
  const char *field3Name = "field3";
  const char *field4Name = "field4";
  const char *field5Name = "field5";

  const char *field1NameGet;
  const char *field2NameGet;
  const char *field3NameGet;
  const char *field4NameGet;
  const char *field5NameGet;

  int field1NameGetLength;
  int field2NameGetLength;
  int field3NameGetLength;
  int field4NameGetLength;
  int field5NameGetLength;

  int field1_length;
  int field2_length;
  int field3_length;
  int field4_length;
  /* int field5_length; unused: lv */

  INKMLoc test_field_loc11 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc test_field_loc12 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc test_field_loc13 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc test_field_loc14 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc test_field_loc15 = (INKMLoc) INK_ERROR_PTR;

  int actualNumberOfFields;
  int numberOfFields;

  const char *field1Value1 = "field1Value1";
  const char *field1Value2 = "field1Value2";
  const char *field1Value3 = "field1Value3";
  const char *field1Value4 = "field1Value4";
  const char *field1Value5 = "field1Value5";
  const char *field1ValueNew = "newfieldValue";

  const char *field1Value1Get;
  const char *field1Value2Get;
  const char *field1Value3Get;
  const char *field1Value4Get;
  const char *field1Value5Get;
  const char *field1ValueNewGet;

  int lengthField1Value1;
  int lengthField1Value2;
  int lengthField1Value3;
  int lengthField1Value4;
  int lengthField1Value5;
  int lengthField1ValueNew;

  time_t field2Value1 = time(NULL);
  time_t field2Value1Get;
  time_t field2ValueNew;
  time_t field2ValueNewGet;

  int field3Value1 = 31;
  int field3Value2 = 32;
  int field3Value3 = 33;
  int field3Value4 = 34;
  int field3Value5 = 35;
  int field3ValueNew = 30;

  int field3Value1Get;
  int field3Value2Get;
  int field3Value3Get;
  int field3Value4Get;
  int field3Value5Get;
  int field3ValueNewGet;

  unsigned int field4Value1 = 41;
  unsigned int field4Value2 = 42;
  unsigned int field4Value3 = 43;
  unsigned int field4Value4 = 44;
  unsigned int field4Value5 = 45;
  unsigned int field4ValueNew = 40;

  unsigned int field4Value1Get;
  unsigned int field4Value2Get;
  unsigned int field4Value3Get;
  unsigned int field4Value4Get;
  unsigned int field4Value5Get;
  unsigned int field4ValueNewGet;

  const char *field5Value1 = "field5Value1";
  const char *field5Value1Append = "AppendedValue";
  const char *fieldValueAppendGet;
  int lengthFieldValueAppended;
  int field5Value2 = 52;
  const char *field5Value3 = "DeleteValue";
  const char *fieldValueDeleteGet;
  int lengthFieldValueDeleteGet;
  unsigned int field5Value4 = 54;
  int numberOfValueInField;

  INKMLoc field_loc;

  bool test_passed_MBuffer_Create = false;
  bool test_passed_Mime_Hdr_Create = false;
  bool test_passed_Mime_Hdr_Field_Create = false;
  bool test_passed_Mime_Hdr_Field_Name = false;
  bool test_passed_Mime_Hdr_Field_Append = false;
  bool test_passed_Mime_Hdr_Field_Get = false;
  bool test_passed_Mime_Hdr_Field_Next = false;
  bool test_passed_Mime_Hdr_Fields_Count = false;
  bool test_passed_Mime_Hdr_Field_Value_String_Insert = false;
  bool test_passed_Mime_Hdr_Field_Value_String_Get = false;
  bool test_passed_Mime_Hdr_Field_Value_String_Set = false;
  bool test_passed_Mime_Hdr_Field_Value_Date_Insert = false;
  bool test_passed_Mime_Hdr_Field_Value_Date_Get = false;
  bool test_passed_Mime_Hdr_Field_Value_Date_Set = false;
  bool test_passed_Mime_Hdr_Field_Value_Int_Insert = false;
  bool test_passed_Mime_Hdr_Field_Value_Int_Get = false;
  bool test_passed_Mime_Hdr_Field_Value_Int_Set = false;
  bool test_passed_Mime_Hdr_Field_Value_Uint_Insert = false;
  bool test_passed_Mime_Hdr_Field_Value_Uint_Get = false;
  bool test_passed_Mime_Hdr_Field_Value_Uint_Set = false;
  bool test_passed_Mime_Hdr_Field_Value_Append = false;
  bool test_passed_Mime_Hdr_Field_Value_Delete = false;
  bool test_passed_Mime_Hdr_Field_Values_Clear = false;
  bool test_passed_Mime_Hdr_Field_Values_Count = false;
  bool test_passed_Mime_Hdr_Field_Destroy = false;
  bool test_passed_Mime_Hdr_Fields_Clear = false;
  bool test_passed_Mime_Hdr_Destroy = false;
  bool test_passed_MBuffer_Destroy = false;
  bool test_passed_Mime_Hdr_Field_Length_Get = false;

  *pstatus = REGRESSION_TEST_INPROGRESS;

  // INKMBufferCreate
  if ((bufp1 = INKMBufferCreate()) == INK_ERROR_PTR) {
    // Cannot proceed with tests.
    SDK_RPRINT(test, "INKMBufferCreate", "TestCase1", TC_FAIL, "INKMBufferCreate Returns INK_ERROR_PTR");
  } else {
    SDK_RPRINT(test, "INKMBufferCreate", "TestCase1", TC_PASS, "ok");
    test_passed_MBuffer_Create = true;
  }

  // INKMimeHdrCreate
  if (test_passed_MBuffer_Create == true) {
    if ((mime_loc1 = INKMimeHdrCreate(bufp1)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrCreate", "TestCase1", TC_FAIL, "INKMimeHdrCreate Returns INK_ERROR_PTR");
    } else {
      SDK_RPRINT(test, "INKMimeHdrCreate", "TestCase1", TC_PASS, "ok");
      test_passed_Mime_Hdr_Create = true;
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrCreate", "TestCase1", TC_FAIL, "Cannot run test as Test for INKMBufferCreate Failed");
  }

  // INKMimeHdrFieldCreate
  if (test_passed_Mime_Hdr_Create == true) {
    if (((field_loc11 = INKMimeHdrFieldCreate(bufp1, mime_loc1)) == INK_ERROR_PTR) ||
        ((field_loc12 = INKMimeHdrFieldCreate(bufp1, mime_loc1)) == INK_ERROR_PTR) ||
        ((field_loc13 = INKMimeHdrFieldCreate(bufp1, mime_loc1)) == INK_ERROR_PTR) ||
        ((field_loc14 = INKMimeHdrFieldCreate(bufp1, mime_loc1)) == INK_ERROR_PTR) ||
        ((field_loc15 = INKMimeHdrFieldCreate(bufp1, mime_loc1)) == INK_ERROR_PTR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldCreate", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldCreate Returns INK_ERROR_PTR");
    } else {
      SDK_RPRINT(test, "INKMimeHdrFieldCreate", "TestCase1|2|3|4|5", TC_PASS, "ok");
      test_passed_Mime_Hdr_Field_Create = true;
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldCreate", "All Test Case", TC_FAIL,
               "Cannot run test as Test for INKMimeHdrCreate Failed");
  }


  //INKMimeHdrFieldNameGet&Set
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((INKMimeHdrFieldNameSet(bufp1, mime_loc1, field_loc11, field1Name, -1) == INK_ERROR) ||
        (INKMimeHdrFieldNameSet(bufp1, mime_loc1, field_loc12, field2Name, -1) == INK_ERROR) ||
        (INKMimeHdrFieldNameSet(bufp1, mime_loc1, field_loc13, field3Name, -1) == INK_ERROR) ||
        (INKMimeHdrFieldNameSet(bufp1, mime_loc1, field_loc14, field4Name, -1) == INK_ERROR) ||
        (INKMimeHdrFieldNameSet(bufp1, mime_loc1, field_loc15, field5Name, -1) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldNameSet", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldNameSet Returns INK_ERROR_PTR");
    } else {
      if (((field1NameGet =
            INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc11, &field1NameGetLength)) == INK_ERROR_PTR) ||
          ((field2NameGet =
            INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc12, &field2NameGetLength)) == INK_ERROR_PTR) ||
          ((field3NameGet =
            INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc13, &field3NameGetLength)) == INK_ERROR_PTR) ||
          ((field4NameGet =
            INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc14, &field4NameGetLength)) == INK_ERROR_PTR) ||
          ((field5NameGet =
            INKMimeHdrFieldNameGet(bufp1, mime_loc1, field_loc15, &field5NameGetLength)) == INK_ERROR_PTR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldNameGet", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldNameGet Returns INK_ERROR_PTR");
        SDK_RPRINT(test, "INKMimeHdrFieldNameGet|Set", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldNameGet Returns INK_ERROR_PTR");
      } else {
        if (((strncmp(field1NameGet, field1Name, field1NameGetLength) == 0) &&
             (field1NameGetLength == (int) strlen(field1Name))) &&
            ((strncmp(field2NameGet, field2Name, field2NameGetLength) == 0) &&
             (field2NameGetLength == (int) strlen(field2Name))) &&
            ((strncmp(field3NameGet, field3Name, field3NameGetLength) == 0) &&
             (field3NameGetLength == (int) strlen(field3Name))) &&
            ((strncmp(field4NameGet, field4Name, field4NameGetLength) == 0) &&
             (field4NameGetLength == (int) strlen(field4Name))) &&
            ((strncmp(field5NameGet, field5Name, field5NameGetLength) == 0) &&
             field5NameGetLength == (int) strlen(field5Name))
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldNameGet&Set", "TestCase1&2&3&4&5", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Name = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldNameGet&Set", "TestCase1|2|3|4|5", TC_FAIL, "Values Don't Match");
        }
        if ((INKHandleStringRelease(bufp1, field_loc11, field1NameGet) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc12, field2NameGet) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc13, field3NameGet) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc14, field4NameGet) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc15, field5NameGet) == INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldNameGet&Set", "", TC_FAIL, "Unable to release handle to string");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldNameGet&Set", "All Test Case", TC_FAIL,
               "Cannot run test as Test for INKMBufferFieldCreate Failed");
  }




  // INKMimeHdrFieldAppend, INKMimeHdrFieldGet, INKMimeHdrFieldNext
  if (test_passed_Mime_Hdr_Field_Name == true) {
    if ((INKMimeHdrFieldAppend(bufp1, mime_loc1, field_loc11) == INK_ERROR) ||
        (INKMimeHdrFieldAppend(bufp1, mime_loc1, field_loc12) == INK_ERROR) ||
        (INKMimeHdrFieldAppend(bufp1, mime_loc1, field_loc13) == INK_ERROR) ||
        (INKMimeHdrFieldAppend(bufp1, mime_loc1, field_loc14) == INK_ERROR) ||
        (INKMimeHdrFieldAppend(bufp1, mime_loc1, field_loc15) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldAppend Returns INK_ERROR");
    } else {
      if ((test_field_loc11 = INKMimeHdrFieldGet(bufp1, mime_loc1, 0)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldGet Returns INK_ERROR_PTR");
        SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase1", TC_FAIL,
                   "Cannot Test INKMimeHdrFieldNext as INKMimeHdrFieldGet Returns INK_ERROR_PTR");
        SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase1", TC_FAIL, "INKMimeHdrFieldGet Returns INK_ERROR_PTR");
      } else {
        if (compare_field_names(test, bufp1, mime_loc1, field_loc11, bufp1, mime_loc1, test_field_loc11) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase1", TC_FAIL, "Values Don't match");
          SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase1", TC_FAIL,
                     "Cannot Test INKMimeHdrFieldNext as Values don't match");
          SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase1", TC_FAIL, "Values Don't match");
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase1", TC_PASS, "ok");
          SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Append = true;
          test_passed_Mime_Hdr_Field_Get = true;
        }
      }

      if (test_passed_Mime_Hdr_Field_Append == true) {
        if ((test_field_loc12 = INKMimeHdrFieldNext(bufp1, mime_loc1, test_field_loc11)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase2", TC_FAIL,
                     "INKMimeHdrFieldAppend failed as INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase2", TC_FAIL, "INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase2", TC_FAIL,
                     "Cannot be sure that INKMimeHdrFieldGet passed as INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          test_passed_Mime_Hdr_Field_Append = false;
          test_passed_Mime_Hdr_Field_Next = false;
          test_passed_Mime_Hdr_Field_Get = false;
        } else {
          if (compare_field_names(test, bufp1, mime_loc1, field_loc12, bufp1, mime_loc1, test_field_loc12) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase2", TC_PASS, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase2", TC_PASS, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase2", TC_PASS, "Values Don't match");
            test_passed_Mime_Hdr_Field_Append = false;
            test_passed_Mime_Hdr_Field_Next = false;
            test_passed_Mime_Hdr_Field_Get = false;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase2", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase2", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase2", TC_PASS, "ok");
            test_passed_Mime_Hdr_Field_Next = true;
          }
        }
      }

      if (test_passed_Mime_Hdr_Field_Append == true) {
        if ((test_field_loc13 = INKMimeHdrFieldNext(bufp1, mime_loc1, test_field_loc12)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase3", TC_FAIL,
                     "INKMimeHdrFieldNext Returns INK_ERROR. Cannot check.");
          SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase3", TC_FAIL, "INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase3", TC_FAIL,
                     "Cannot be sure that INKMimeHdrFieldGet passed as INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          test_passed_Mime_Hdr_Field_Append = false;
          test_passed_Mime_Hdr_Field_Next = false;
          test_passed_Mime_Hdr_Field_Get = false;
        } else {
          if (compare_field_names(test, bufp1, mime_loc1, field_loc13, bufp1, mime_loc1, test_field_loc13) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase3", TC_FAIL, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase3", TC_FAIL, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase3", TC_FAIL, "Values Don't match");
            test_passed_Mime_Hdr_Field_Append = false;
            test_passed_Mime_Hdr_Field_Next = false;
            test_passed_Mime_Hdr_Field_Get = false;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase3", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase3", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase3", TC_PASS, "ok");
          }
        }
      }

      if (test_passed_Mime_Hdr_Field_Append == true) {
        if ((test_field_loc14 = INKMimeHdrFieldNext(bufp1, mime_loc1, test_field_loc13)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase4", TC_FAIL,
                     "INKMimeHdrFieldNext Returns INK_ERROR. Cannot check.");
          SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase4", TC_FAIL, "INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase4", TC_FAIL,
                     "Cannot be sure that INKMimeHdrFieldGet passed as INKMimeHdrFieldNext Returns INK_ERROR.");
          test_passed_Mime_Hdr_Field_Append = false;
          test_passed_Mime_Hdr_Field_Next = false;
          test_passed_Mime_Hdr_Field_Get = false;
        } else {
          if (compare_field_names(test, bufp1, mime_loc1, field_loc14, bufp1, mime_loc1, test_field_loc14) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase4", TC_FAIL, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase4", TC_FAIL, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase4", TC_FAIL, "Values Don't match");
            test_passed_Mime_Hdr_Field_Append = false;
            test_passed_Mime_Hdr_Field_Next = false;
            test_passed_Mime_Hdr_Field_Get = false;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase4", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase4", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldGet", "TestCase4", TC_PASS, "ok");
          }
        }
      }

      if (test_passed_Mime_Hdr_Field_Append == true) {
        if ((test_field_loc15 = INKMimeHdrFieldNext(bufp1, mime_loc1, test_field_loc14)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase5", TC_FAIL, "INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase5", TC_FAIL,
                     "INKMimeHdrFieldNext Returns INK_ERROR. Cannot check.");
          test_passed_Mime_Hdr_Field_Append = false;
          test_passed_Mime_Hdr_Field_Next = false;
          test_passed_Mime_Hdr_Field_Get = false;
        } else {
          if (compare_field_names(test, bufp1, mime_loc1, field_loc15, bufp1, mime_loc1, test_field_loc15) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase5", TC_FAIL, "Values Don't match");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase5", TC_FAIL, "Values Don't match");
            test_passed_Mime_Hdr_Field_Append = false;
            test_passed_Mime_Hdr_Field_Next = false;
            test_passed_Mime_Hdr_Field_Get = false;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldAppend", "TestCase5", TC_PASS, "ok");
            SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase5", TC_PASS, "ok");
          }
        }
      }

      if ((INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc11) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc12) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc13) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc14) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc15) == INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldAppend/Next/Get", "", TC_FAIL,
                   "Unable to release handle using INKHandleMLocRelease. Can be bad handle.");
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldAppend & INKMimeHdrFieldNext", "All Test Case", TC_FAIL,
               "Cannot run test as Test for INKMimeHdrFieldNameGet&Set Failed");
  }


  //INKMimeHdrFieldsCount
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((numberOfFields = INKMimeHdrFieldsCount(bufp1, mime_loc1)) == INK_ERROR) {
      SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL, "INKMimeHdrFieldsCount Returns INK_ERROR");
    } else {
      actualNumberOfFields = 0;
      if ((field_loc = INKMimeHdrFieldGet(bufp1, mime_loc1, actualNumberOfFields)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL, "INKMimeHdrFieldGet Returns INK_ERROR_PTR");
      } else {
        while (field_loc != NULL) {
          INKMLoc next_field_loc;

          actualNumberOfFields++;
          if ((next_field_loc = INKMimeHdrFieldNext(bufp1, mime_loc1, field_loc)) == INK_ERROR_PTR) {
            SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL,
                       "INKMimeHdrFieldNext Returns INK_ERROR_PTR");
          }
          if (INKHandleMLocRelease(bufp1, mime_loc1, field_loc) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL,
                       "Unable to release handle using INKHandleMLocRelease");
          }
          field_loc = next_field_loc;
          next_field_loc = NULL;
        }
        if (actualNumberOfFields == numberOfFields) {
          SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Fields_Count = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL, "Value's Dont match");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldsCount", "TestCase1", TC_FAIL, "Cannot run Test as INKMimeHdrFieldCreate failed");
  }

  // INKMimeHdrFieldValueStringInsert, INKMimeHdrFieldValueStringGet, INKMimeHdrFieldValueStringSet
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc11, -1, field1Value2, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc11, 0, field1Value1, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc11, -1, field1Value5, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc11, 2, field1Value4, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc11, 2, field1Value3, -1) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldValueStringInsert Returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueStringGet", "TestCase1&2&3&4&5", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueStringInsert returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueStringInsert returns INK_ERROR");
    } else {
      if ((INKMimeHdrFieldValueStringGet(bufp1, mime_loc1, field_loc11, 0, &field1Value1Get, &lengthField1Value1) ==
           INK_ERROR) ||
          (INKMimeHdrFieldValueStringGet(bufp1, mime_loc1, field_loc11, 1, &field1Value2Get, &lengthField1Value2) ==
           INK_ERROR) ||
          (INKMimeHdrFieldValueStringGet(bufp1, mime_loc1, field_loc11, 2, &field1Value3Get, &lengthField1Value3) ==
           INK_ERROR) ||
          (INKMimeHdrFieldValueStringGet(bufp1, mime_loc1, field_loc11, 3, &field1Value4Get, &lengthField1Value4) ==
           INK_ERROR) ||
          (INKMimeHdrFieldValueStringGet(bufp1, mime_loc1, field_loc11, 4, &field1Value5Get, &lengthField1Value5) ==
           INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert|Get", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldValueStringGet Returns INK_ERROR");
        SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueStringSet cannot be tested as INKMimeHdrFieldValueStringInsert|Get failed");
      } else {
        if (((strncmp(field1Value1Get, field1Value1, lengthField1Value1) == 0) &&
             lengthField1Value1 == (int) strlen(field1Value1)) &&
            ((strncmp(field1Value2Get, field1Value2, lengthField1Value2) == 0) &&
             lengthField1Value2 == (int) strlen(field1Value2)) &&
            ((strncmp(field1Value3Get, field1Value3, lengthField1Value3) == 0) &&
             lengthField1Value3 == (int) strlen(field1Value3)) &&
            ((strncmp(field1Value4Get, field1Value4, lengthField1Value4) == 0) &&
             lengthField1Value4 == (int) strlen(field1Value4)) &&
            ((strncmp(field1Value5Get, field1Value5, lengthField1Value5) == 0) &&
             lengthField1Value5 == (int) strlen(field1Value5))
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert", "TestCase1&2&3&4&5", TC_PASS, "ok");
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringGet", "TestCase1&2&3&4&5", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Value_String_Insert = true;
          test_passed_Mime_Hdr_Field_Value_String_Get = true;

          if ((INKMimeHdrFieldValueStringSet(bufp1, mime_loc1, field_loc11, 3, field1ValueNew, -1)) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL,
                       "INKMimeHdrFieldValueStringSet returns INK_ERROR");
          } else {
            if (INKMimeHdrFieldValueStringGet
                (bufp1, mime_loc1, field_loc11, 3, &field1ValueNewGet, &lengthField1ValueNew) == INK_ERROR) {
              SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL,
                         "INKMimeHdrFieldValueStringGet returns INK_ERROR");
            } else {
              if ((strncmp(field1ValueNewGet, field1ValueNew, lengthField1ValueNew) == 0) &&
                  (lengthField1ValueNew == (int) strlen(field1ValueNew))) {
                SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_PASS, "ok");
                test_passed_Mime_Hdr_Field_Value_String_Set = true;
              } else {
                SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL, "Value's Don't match");
              }
              if (INKHandleStringRelease(bufp1, field_loc11, field1ValueNewGet) == INK_ERROR) {
                SDK_RPRINT(test, "INKMimeHdrFieldValueStringGet", "", TC_FAIL, "Unable to release handle to string");
              }
            }
          }
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringGet", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringSet", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueStringSet cannot be tested as INKMimeHdrFieldValueStringInsert|Get failed");
        }
        if ((INKHandleStringRelease(bufp1, field_loc11, field1Value1Get) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc12, field1Value2Get) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc13, field1Value3Get) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc14, field1Value4Get) == INK_ERROR) ||
            (INKHandleStringRelease(bufp1, field_loc15, field1Value5Get) == INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert&Get", "", TC_FAIL, "Unable to release handle to string");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldValueStringInsert&Set&Get", "All", TC_FAIL,
               "Cannot run Test as INKMimeHdrFieldCreate failed");
  }


  // INKMimeHdrFieldValueDateInsert, INKMimeHdrFieldValueDateGet, INKMimeHdrFieldValueDateSet
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if (INKMimeHdrFieldValueDateInsert(bufp1, mime_loc1, field_loc12, field2Value1) == INK_ERROR) {
      SDK_RPRINT(test, "INKMimeHdrFieldValueDateInsert", "TestCase1", TC_FAIL,
                 "INKMimeHdrFieldValueDateInsert Returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueDateGet", "TestCase1", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueDateInsert returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueDateInsert returns INK_ERROR");
    } else {
      if (INKMimeHdrFieldValueDateGet(bufp1, mime_loc1, field_loc12, &field2Value1Get) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueDateInsert|Get", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueDateGet Returns INK_ERROR");
        SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueDateSet cannot be tested as INKMimeHdrFieldValueDateInsert|Get failed");
      } else {
        if (field2Value1Get == field2Value1) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueDateInsert", "TestCase1", TC_PASS, "ok");
          SDK_RPRINT(test, "INKMimeHdrFieldValueDateGet", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Value_Date_Insert = true;
          test_passed_Mime_Hdr_Field_Value_Date_Get = true;
          field2ValueNew = time(NULL);
          if ((INKMimeHdrFieldValueDateSet(bufp1, mime_loc1, field_loc12, field2ValueNew)) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL,
                       "INKMimeHdrFieldValueDateSet returns INK_ERROR");
          } else {
            if (INKMimeHdrFieldValueDateGet(bufp1, mime_loc1, field_loc12, &field2ValueNewGet) == INK_ERROR) {
              SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL,
                         "INKMimeHdrFieldValueDateGet returns INK_ERROR");
            } else {
              if (field2ValueNewGet == field2ValueNew) {
                SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_PASS, "ok");
                test_passed_Mime_Hdr_Field_Value_Date_Set = true;
              } else {
                SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL, "Value's Don't match");
              }
            }
          }
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldValueDateInsert", "TestCase1", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueDateGet", "TestCase1", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueDateSet", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueDateSet cannot be tested as INKMimeHdrFieldValueDateInsert|Get failed");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldValueDateInsert&Set&Get", "TestCase1", TC_FAIL,
               "Cannot run Test as INKMimeHdrFieldCreate failed");
  }


  // INKMimeHdrFieldValueIntInsert, INKMimeHdrFieldValueIntGet, INKMimeHdrFieldValueIntSet
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc13, -1, field3Value2) == INK_ERROR) ||
        (INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc13, 0, field3Value1) == INK_ERROR) ||
        (INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc13, -1, field3Value5) == INK_ERROR) ||
        (INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc13, 2, field3Value4) == INK_ERROR) ||
        (INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc13, 2, field3Value3) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldValueIntInsert", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldValueIntInsert Returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueIntGet", "TestCase1&2&3&4&5", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueIntInsert returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueIntInsert returns INK_ERROR");
    } else {
      if ((INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 0, &field3Value1Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 1, &field3Value2Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 2, &field3Value3Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 3, &field3Value4Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 4, &field3Value5Get) == INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueIntInsert|Get", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldValueIntGet Returns INK_ERROR");
        SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueIntSet cannot be tested as INKMimeHdrFieldValueIntInsert|Get failed");
      } else {
        if ((field3Value1Get == field3Value1) &&
            (field3Value2Get == field3Value2) &&
            (field3Value3Get == field3Value3) && (field3Value4Get == field3Value4) && (field3Value5Get == field3Value5)
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueIntInsert", "TestCase1&2&3&4&5", TC_PASS, "ok");
          SDK_RPRINT(test, "INKMimeHdrFieldValueIntGet", "TestCase1&2&3&4&5", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Value_Int_Insert = true;
          test_passed_Mime_Hdr_Field_Value_Int_Get = true;
          if ((INKMimeHdrFieldValueIntSet(bufp1, mime_loc1, field_loc13, 3, field3ValueNew)) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL,
                       "INKMimeHdrFieldValueIntSet returns INK_ERROR");
          } else {
            if (INKMimeHdrFieldValueIntGet(bufp1, mime_loc1, field_loc13, 3, &field3ValueNewGet) == INK_ERROR) {
              SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL,
                         "INKMimeHdrFieldValueIntGet returns INK_ERROR");
            } else {
              if (field3ValueNewGet == field3ValueNew) {
                SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_PASS, "ok");
                test_passed_Mime_Hdr_Field_Value_Int_Set = true;
              } else {
                SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL, "Value's Don't match");
              }
            }
          }
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldValueIntInsert", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueIntGet", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueIntSet", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueIntSet cannot be tested as INKMimeHdrFieldValueIntInsert|Get failed");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldValueIntInsert&Set&Get", "All", TC_FAIL,
               "Cannot run Test as INKMimeHdrFieldCreate failed");
  }

  // INKMimeHdrFieldValueUintInsert, INKMimeHdrFieldValueUintGet, INKMimeHdrFieldValueUintSet
  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc14, -1, field4Value2) == INK_ERROR) ||
        (INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc14, 0, field4Value1) == INK_ERROR) ||
        (INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc14, -1, field4Value5) == INK_ERROR) ||
        (INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc14, 2, field4Value4) == INK_ERROR) ||
        (INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc14, 2, field4Value3) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldValueUintInsert", "TestCase1|2|3|4|5", TC_FAIL,
                 "INKMimeHdrFieldValueUintInsert Returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueUintGet", "TestCase1&2&3&4&5", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueUintInsert returns INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL,
                 "Cannot run Test as INKMimeHdrFieldValueUintInsert returns INK_ERROR");
    } else {
      if ((INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 0, &field4Value1Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 1, &field4Value2Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 2, &field4Value3Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 3, &field4Value4Get) == INK_ERROR) ||
          (INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 4, &field4Value5Get) == INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueUintInsert|Get", "TestCase1|2|3|4|5", TC_FAIL,
                   "INKMimeHdrFieldValueUintGet Returns INK_ERROR");
        SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueUintSet cannot be tested as INKMimeHdrFieldValueUintInsert|Get failed");
      } else {
        if ((field4Value1Get == field4Value1) &&
            (field4Value2Get == field4Value2) &&
            (field4Value3Get == field4Value3) && (field4Value4Get == field4Value4) && (field4Value5Get == field4Value5)
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueUintInsert", "TestCase1&2&3&4&5", TC_PASS, "ok");
          SDK_RPRINT(test, "INKMimeHdrFieldValueUintGet", "TestCase1&2&3&4&5", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Value_Uint_Insert = true;
          test_passed_Mime_Hdr_Field_Value_Uint_Get = true;
          if ((INKMimeHdrFieldValueUintSet(bufp1, mime_loc1, field_loc14, 3, field4ValueNew)) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL,
                       "INKMimeHdrFieldValueUintSet returns INK_ERROR");
          } else {
            if (INKMimeHdrFieldValueUintGet(bufp1, mime_loc1, field_loc14, 3, &field4ValueNewGet) == INK_ERROR) {
              SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL,
                         "INKMimeHdrFieldValueUintGet returns INK_ERROR");
            } else {
              if (field4ValueNewGet == field4ValueNew) {
                SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_PASS, "ok");
                test_passed_Mime_Hdr_Field_Value_Uint_Set = true;
              } else {
                SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL, "Value's Don't match");
              }
            }
          }
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldValueUintInsert", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueUintGet", "TestCase1|2|3|4|5", TC_PASS, "Value's Don't Match");
          SDK_RPRINT(test, "INKMimeHdrFieldValueUintSet", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueUintSet cannot be tested as INKMimeHdrFieldValueUintInsert|Get failed");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldValueUintInsert&Set&Get", "All", TC_FAIL,
               "Cannot run Test as INKMimeHdrFieldCreate failed");
  }

  // INKMimeHdrFieldLengthGet
  field1_length = INKMimeHdrFieldLengthGet(bufp1, mime_loc1, field_loc11);
  field2_length = INKMimeHdrFieldLengthGet(bufp1, mime_loc1, field_loc12);
  field3_length = INKMimeHdrFieldLengthGet(bufp1, mime_loc1, field_loc13);
  field4_length = INKMimeHdrFieldLengthGet(bufp1, mime_loc1, field_loc14);
  if ((field1_length == INK_ERROR || field1_length == 0) ||
      (field2_length == INK_ERROR || field2_length == 0) ||
      (field3_length == INK_ERROR || field3_length == 0) || (field4_length == INK_ERROR || field4_length == 0)) {
    SDK_RPRINT(test, "INKMimeHdrFieldLengthGet", "TestCase1", TC_FAIL, "Returned bad length");
    test_passed_Mime_Hdr_Field_Length_Get = false;
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldLengthGet", "TestCase1", TC_PASS, "ok");
    test_passed_Mime_Hdr_Field_Length_Get = true;
  }



  // INKMimeHdrFieldValueAppend, INKMimeHdrFieldValueDelete, INKMimeHdrFieldValuesCount, INKMimeHdrFieldValuesClear

  if (test_passed_Mime_Hdr_Field_Create == true) {
    if ((INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc15, -1, field5Value1, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueIntInsert(bufp1, mime_loc1, field_loc15, -1, field5Value2) == INK_ERROR) ||
        (INKMimeHdrFieldValueStringInsert(bufp1, mime_loc1, field_loc15, -1, field5Value3, -1) == INK_ERROR) ||
        (INKMimeHdrFieldValueUintInsert(bufp1, mime_loc1, field_loc15, -1, field5Value4) == INK_ERROR)
      ) {
      SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_FAIL,
                 "INKMimeHdrFieldValueString|Int|UintInsert returns INK_ERROR. Cannot create field for testing.");
      SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_FAIL,
                 "INKMimeHdrFieldValueString|Int|UintInsert returns INK_ERROR. Cannot create field for testing.");
      SDK_RPRINT(test, "INKMimeHdrFieldValuesCount", "TestCase1", TC_FAIL,
                 "INKMimeHdrFieldValueString|Int|UintInsert returns INK_ERROR. Cannot create field for testing.");
      SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_FAIL,
                 "INKMimeHdrFieldValueString|Int|UintInsert returns INK_ERROR. Cannot create field for testing.");
    } else {
      if (INKMimeHdrFieldValueAppend(bufp1, mime_loc1, field_loc15, 0, field5Value1Append, -1) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueAppend returns INK_ERROR");
      } else {
        if ((INKMimeHdrFieldValueStringGet
             (bufp1, mime_loc1, field_loc15, 0, &fieldValueAppendGet, &lengthFieldValueAppended)) != INK_SUCCESS) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueStringGet doesn't return INK_SUCCESS");
        } else {
          char *expected_value;
          size_t len = strlen(field5Value1) + strlen(field5Value1Append) + 1;
          expected_value = (char *) INKmalloc(len);
          memset(expected_value, 0, strlen(field5Value1) + strlen(field5Value1Append) + 1);
          ink_strncpy(expected_value, field5Value1, len);
          strncat(expected_value, field5Value1Append, len - strlen(expected_value) - 1);
          if ((strncmp(fieldValueAppendGet, expected_value, lengthFieldValueAppended) == 0) &&
              (lengthFieldValueAppended = strlen(expected_value))
            ) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_PASS, "ok");
            test_passed_Mime_Hdr_Field_Value_Append = true;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_FAIL, "Values mismatch");
          }
          if (INKHandleStringRelease(bufp1, field_loc15, fieldValueAppendGet) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "", TC_FAIL, "Unable to release handle to string");
          }
          INKfree(expected_value);
        }
      }

      if ((numberOfValueInField = INKMimeHdrFieldValuesCount(bufp1, mime_loc1, field_loc15)) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldValuesCount", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValuesCount returns INK_ERROR");
      } else {
        if (numberOfValueInField == 4) {
          SDK_RPRINT(test, "INKMimeHdrFieldValuesCount", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Values_Count = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldValuesCount", "TestCase1", TC_FAIL, "Values don't match");
        }
      }


      if (INKMimeHdrFieldValueDelete(bufp1, mime_loc1, field_loc15, 2) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValueDelete Returns INK_ERROR");
      } else {
        if ((INKMimeHdrFieldValueStringGet
             (bufp1, mime_loc1, field_loc15, 2, &fieldValueDeleteGet, &lengthFieldValueDeleteGet)) != INK_SUCCESS) {
          SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValueStringGet doesn't return INK_SUCCESS. Cannot test for INKMimeHdrFieldValueDelete");
        } else {
          if ((strncmp(fieldValueDeleteGet, field5Value3, lengthFieldValueDeleteGet) == 0) &&
              (lengthFieldValueDeleteGet == (int) strlen(field5Value3))) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_FAIL,
                       "Value not deleted from field or incorrect index deleted from field.");
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_PASS, "ok");
            test_passed_Mime_Hdr_Field_Value_Delete = true;
          }
          if (INKHandleStringRelease(bufp1, field_loc15, fieldValueDeleteGet) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "", TC_FAIL, "Unable to release handle to string");
          }
        }
      }

      if (INKMimeHdrFieldValuesClear(bufp1, mime_loc1, field_loc15) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldValuesClear returns INK_ERROR");
      } else {
        if ((numberOfValueInField = INKMimeHdrFieldValuesCount(bufp1, mime_loc1, field_loc15)) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_FAIL,
                     "INKMimeHdrFieldValuesCount returns INK_ERROR. Cannot test INKMimeHdrFieldValuesClear");
        } else {
          if (numberOfValueInField == 0) {
            SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_PASS, "ok");
            test_passed_Mime_Hdr_Field_Values_Clear = true;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_FAIL, "Values don't match");
          }
        }
      }
    }

    // INKMimeHdrFieldDestroy
    if (INKMimeHdrFieldDestroy(bufp1, mime_loc1, field_loc15) == INK_ERROR) {
      SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_FAIL, "INKMimeHdrFieldDestroy returns INK_ERROR");
    } else {
      if ((test_field_loc15 = INKMimeHdrFieldFind(bufp1, mime_loc1, field5Name, -1)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_FAIL, "INKMimeHdrFieldFind returns INK_ERROR_PTR");
      } else {
        if (test_field_loc15 == NULL) {
          SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Field_Destroy = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_FAIL, "Field not destroyed");
          if (INKHandleMLocRelease(bufp1, mime_loc1, test_field_loc15) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_FAIL,
                       "Unable to release handle using INKHandleMLocRelease");
          }
        }
      }
      if (INKHandleMLocRelease(bufp1, mime_loc1, field_loc15) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase2", TC_FAIL,
                   "Unable to release handle using INKHandleMLocRelease");
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldValueAppend", "TestCase1", TC_FAIL,
               "Cannot run test as INKMimeHdrFieldCreate has failed");
    SDK_RPRINT(test, "INKMimeHdrFieldValueDelete", "TestCase1", TC_FAIL,
               "Cannot run test as INKMimeHdrFieldCreate has failed");
    SDK_RPRINT(test, "INKMimeHdrFieldValuesCount", "TestCase1", TC_FAIL,
               "Cannot run test as INKMimeHdrFieldCreate has failed");
    SDK_RPRINT(test, "INKMimeHdrFieldValuesClear", "TestCase1", TC_FAIL,
               "Cannot run test as INKMimeHdrFieldCreate has failed");
    SDK_RPRINT(test, "INKMimeHdrFieldDestroy", "TestCase1", TC_FAIL,
               "Cannot run test as INKMimeHdrFieldCreate has failed");
  }

  // Mime Hdr Fields Clear
  if (test_passed_Mime_Hdr_Field_Append == true) {
    if (INKMimeHdrFieldsClear(bufp1, mime_loc1) != INK_SUCCESS) {
      SDK_RPRINT(test, "INKMimeHdrFieldsClear", "TestCase1", TC_FAIL, "INKMimeHdrFieldsClear returns INK_ERROR");
    } else {
      if ((numberOfFields = INKMimeHdrFieldsCount(bufp1, mime_loc1)) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldsClear", "TestCase1", TC_FAIL, "INKMimeHdrFieldsCount returns INK_ERROR");
      } else {
        if (numberOfFields == 0) {
          SDK_RPRINT(test, "INKMimeHdrFieldsClear", "TestCase1", TC_PASS, "ok");
          test_passed_Mime_Hdr_Fields_Clear = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldsClear", "TestCase1", TC_FAIL, "Fields still exist");
        }
      }
      if ((INKHandleMLocRelease(bufp1, mime_loc1, field_loc11) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, field_loc12) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, field_loc13) == INK_ERROR) ||
          (INKHandleMLocRelease(bufp1, mime_loc1, field_loc14) == INK_ERROR)
        ) {
        SDK_RPRINT(test, "INKMimeHdrFieldsDestroy", "", TC_FAIL, "Unable to release handle using INKHandleMLocRelease");
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldsClear", "TestCase1", TC_FAIL,
               "Cannot run test as Fields have not been inserted in the mime header");
  }

  // Mime Hdr Destroy
  if (test_passed_Mime_Hdr_Create == true) {
    if (INKMimeHdrDestroy(bufp1, mime_loc1) == INK_ERROR) {
      SDK_RPRINT(test, "INKMimeHdrDestroy", "TestCase1", TC_FAIL, "INKMimeHdrDestroy return INK_ERROR");
      SDK_RPRINT(test, "INKMimeHdrDestroy", "TestCase1", TC_FAIL, "Probably INKMimeHdrCreate failed.");
    } else {
      SDK_RPRINT(test, "INKMimeHdrDestroy", "TestCase1", TC_PASS, "ok");
      test_passed_Mime_Hdr_Destroy = true;
    }
      /** Commented out as Traffic Server was crashing. Will have to look into it. */
    /*
       if (INKHandleMLocRelease(bufp1,INK_NULL_MLOC,mime_loc1)==INK_ERROR) {
       SDK_RPRINT(test,"INKHandlMLocRelease","INKMimeHdrDestroy",TC_FAIL,"unable to release handle using INKHandleMLocRelease");
       }
     */
  } else {
    SDK_RPRINT(test, "INKMimeHdrDestroy", "TestCase1", TC_FAIL, "Cannot run test as INKMimeHdrCreate failed");
  }

  // MBuffer Destroy
  if (test_passed_MBuffer_Create == true) {
    if (INKMBufferDestroy(bufp1) == INK_ERROR) {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase1", TC_FAIL, "INKMBufferDestroy return INK_ERROR");
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase1", TC_FAIL, "Probably INKMBufferCreate failed.");
    } else {
      SDK_RPRINT(test, "INKMBufferDestroy", "TestCase1", TC_PASS, "ok");
      test_passed_MBuffer_Destroy = true;
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrDestroy", "TestCase1", TC_FAIL, "Cannot run test as INKMimeHdrCreate failed");
  }


  if ((test_passed_MBuffer_Create == true) &&
      (test_passed_Mime_Hdr_Create == true) &&
      (test_passed_Mime_Hdr_Field_Create == true) &&
      (test_passed_Mime_Hdr_Field_Name == true) &&
      (test_passed_Mime_Hdr_Field_Append == true) &&
      (test_passed_Mime_Hdr_Field_Get == true) &&
      (test_passed_Mime_Hdr_Field_Next == true) &&
      (test_passed_Mime_Hdr_Fields_Count == true) &&
      (test_passed_Mime_Hdr_Field_Value_String_Insert == true) &&
      (test_passed_Mime_Hdr_Field_Value_String_Get == true) &&
      (test_passed_Mime_Hdr_Field_Value_String_Set == true) &&
      (test_passed_Mime_Hdr_Field_Value_Date_Insert == true) &&
      (test_passed_Mime_Hdr_Field_Value_Date_Get == true) &&
      (test_passed_Mime_Hdr_Field_Value_Date_Set == true) &&
      (test_passed_Mime_Hdr_Field_Value_Int_Insert == true) &&
      (test_passed_Mime_Hdr_Field_Value_Int_Get == true) &&
      (test_passed_Mime_Hdr_Field_Value_Int_Set == true) &&
      (test_passed_Mime_Hdr_Field_Value_Uint_Insert == true) &&
      (test_passed_Mime_Hdr_Field_Value_Uint_Get == true) &&
      (test_passed_Mime_Hdr_Field_Value_Uint_Set == true) &&
      (test_passed_Mime_Hdr_Field_Value_Append == true) &&
      (test_passed_Mime_Hdr_Field_Value_Delete == true) &&
      (test_passed_Mime_Hdr_Field_Values_Clear == true) &&
      (test_passed_Mime_Hdr_Field_Values_Count == true) &&
      (test_passed_Mime_Hdr_Field_Destroy == true) &&
      (test_passed_Mime_Hdr_Fields_Clear == true) &&
      (test_passed_Mime_Hdr_Destroy == true) &&
      (test_passed_MBuffer_Destroy == true) && (test_passed_Mime_Hdr_Field_Length_Get == true)
    ) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }
  return;

}


//////////////////////////////////////////////
//       SDK_API_INKHttpHdrParse
//
// Unit Test for API: INKHttpParserCreate
//                    INKHttpParserDestroy
//                    INKHttpParserClear
//                    INKHttpHdrParseReq
//                    INKHttpHdrParseResp
//////////////////////////////////////////////

char *
convert_http_hdr_to_string(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int64 block_avail;

  char *output_string;
  int output_len;

  output_buffer = INKIOBufferCreate();

  if (!output_buffer) {
    INKError("couldn't allocate IOBuffer\n");
  }

  reader = INKIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  INKHttpHdrPrint(bufp, hdr_loc, output_buffer);

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);
  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    INKIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  INKIOBufferReaderFree(reader);
  INKIOBufferDestroy(output_buffer);

  return output_string;
}

REGRESSION_TEST(SDK_API_INKHttpHdrParse) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  const char *req =
    "GET http://www.example.com/ HTTP/1.1\r\nmimefield1:field1value1,field1value2\r\nmimefield2:field2value1,field2value2\r\n\r\n";
  const char *resp =
    "HTTP/1.1 200 OK\r\n1mimefield:1field1value,1field2value\r\n2mimefield:2field1value,2field2value\r\n\r\n";
  const char *start;
  const char *end;
  char *temp;

  int retval;

  INKMBuffer reqbufp;
  INKMBuffer respbufp = (INKMBuffer) INK_ERROR_PTR;

  INKMLoc req_hdr_loc = (INKMLoc) INK_ERROR_PTR;
  INKMLoc resp_hdr_loc = (INKMLoc) INK_ERROR_PTR;

  INKHttpParser parser;

  bool test_passed_parse_req = false;
  bool test_passed_parse_resp = false;
  bool test_passed_parser_clear = false;
  bool test_passed_parser_destroy = false;
  bool resp_run = true;


  //Create Parser
  parser = INKHttpParserCreate();
  if (parser == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKHttpParserCreate", "TestCase1", TC_FAIL, "INKHttpParserCreate returns INK_ERROR_PTR");
    SDK_RPRINT(test, "INKHttpParserDestroy", "TestCase1", TC_FAIL, "Unable to run test as INKHttpParserCreate failed");
    SDK_RPRINT(test, "INKHttpParserClear", "TestCase1", TC_FAIL, "Unable to run test as INKHttpParserCreate failed");
    SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Unable to run test as INKHttpParserCreate failed");
    SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Unable to run test as INKHttpParserCreate failed");

    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKHttpParserCreate", "TestCase1", TC_PASS, "ok");
  }

  // Request
  reqbufp = INKMBufferCreate();
  if (reqbufp == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Cannot create buffer for parsing request");
  } else {
    req_hdr_loc = INKHttpHdrCreate(reqbufp);
    if (req_hdr_loc == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Cannot create Http hdr for parsing request");
      if (INKMBufferDestroy(reqbufp) == INK_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Error in Destroying MBuffer");
      }
    } else {
      start = req;
      end = req + strlen(req) + 1;
      if ((retval = INKHttpHdrParseReq(parser, reqbufp, req_hdr_loc, &start, end)) == INK_PARSE_ERROR) {
        SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "INKHttpHdrParseReq returns INK_PARSE_ERROR");
      } else {
        if (retval == INK_PARSE_DONE) {
          test_passed_parse_req = true;
        } else {
          SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Parsing Error");
        }
      }
    }
  }

  if (INKHttpParserClear(parser) == INK_ERROR) {
    SDK_RPRINT(test, "INKHttpParserClear", "TestCase1", TC_FAIL, "INKHttpParserClear returns INK_ERROR");
    SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Cannot run test as INKHttpParserClear Failed");
  } else {
    SDK_RPRINT(test, "INKHttpParserClear", "TestCase1", TC_PASS, "ok");
    test_passed_parser_clear = true;
  }

  // Response
  if (test_passed_parser_clear == true) {
    respbufp = INKMBufferCreate();
    if (respbufp == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Cannot create buffer for parsing response");
    } else {
      resp_hdr_loc = INKHttpHdrCreate(respbufp);
      if (resp_hdr_loc == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Cannot create Http hdr for parsing response");
        if (INKMBufferDestroy(respbufp) == INK_ERROR) {
          SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Error in Destroying MBuffer");
        }
      } else {
        start = resp;
        end = resp + strlen(resp) + 1;
        if ((retval = INKHttpHdrParseResp(parser, respbufp, resp_hdr_loc, &start, end)) == INK_PARSE_ERROR) {
          SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL,
                     "INKHttpHdrParseReq returns INK_PARSE_ERROR. Maybe an error with INKHttpParserClear.");
        } else {
          if (retval == INK_PARSE_DONE) {
            test_passed_parse_resp = true;
          } else {
            SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Parsing Error");
          }
        }
      }
    }
  } else {
    resp_run = false;
  }

  if (test_passed_parse_req == true) {
    temp = convert_http_hdr_to_string(reqbufp, req_hdr_loc);
    if (strcmp(req, temp) == 0) {
      SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_PASS, "ok");
    } else {
      SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Incorrect parsing");
      test_passed_parse_req = false;
    }
    INKfree(temp);
  }

  if (test_passed_parse_resp == true) {
    temp = convert_http_hdr_to_string(respbufp, resp_hdr_loc);
    if (strcmp(resp, temp) == 0) {
      SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_PASS, "ok");
    } else {
      SDK_RPRINT(test, "INKHttpHdrParseResp", "TestCase1", TC_FAIL, "Incorrect parsing");
      test_passed_parse_resp = false;
    }
    INKfree(temp);
  }

  if (INKHttpParserDestroy(parser) != INK_SUCCESS) {
    SDK_RPRINT(test, "INKHttpParserDestroy", "TestCase1", TC_FAIL, "INKHttpParserDestroy doesn't return INK_SUCCESS");
  } else {
    SDK_RPRINT(test, "INKHttpParserDestroy", "TestCase1", TC_PASS, "ok");
    test_passed_parser_destroy = true;
  }

  if ((test_passed_parse_req != true) ||
      (test_passed_parse_resp != true) || (test_passed_parser_clear != true) || (test_passed_parser_destroy != true)
    ) {
    *pstatus = REGRESSION_TEST_FAILED;
  } else {
    *pstatus = REGRESSION_TEST_PASSED;
  }

  INKMimeHdrDestroy(reqbufp, req_hdr_loc);
  if (resp_run == true)
    INKMimeHdrDestroy(respbufp, resp_hdr_loc);

  INKHandleMLocRelease(reqbufp, INK_NULL_MLOC, req_hdr_loc);
  if (resp_run == true)
    INKHandleMLocRelease(respbufp, INK_NULL_MLOC, resp_hdr_loc);

  INKMBufferDestroy(reqbufp);
  if (resp_run == true)
    INKMBufferDestroy(respbufp);

  return;
}




//////////////////////////////////////////////
//       SDK_API_INKMimeHdrParse
//
// Unit Test for API: INKMimeHdrCopy
//                    INKMimeHdrClone
//                    INKMimeHdrFieldCopy
//                    INKMimeHdrFieldClone
//                    INKMimeHdrFieldCopyValues
//                    INKMimeHdrFieldNextDup
//                    INKMimeHdrFieldRemove
//                    INKMimeHdrLengthGet
//                    INKMimeHdrParse
//                    INKMimeHdrPrint
//                    INKMimeParserClear
//                    INKMimeParserCreate
//                    INKMimeParserDestroy
//                    INKHandleMLocRelease
//                    INKHandleStringRelease
//////////////////////////////////////////////

char *
convert_mime_hdr_to_string(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int64 block_avail;

  char *output_string;
  int output_len;

  output_buffer = INKIOBufferCreate();

  if (!output_buffer) {
    INKError("couldn't allocate IOBuffer\n");
  }

  reader = INKIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  INKMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);
  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    INKIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  INKIOBufferReaderFree(reader);
  INKIOBufferDestroy(output_buffer);

  return output_string;
}

INKReturnCode
compare_field_values(RegressionTest * test, INKMBuffer bufp1, INKMLoc hdr_loc1, INKMLoc field_loc1, INKMBuffer bufp2,
                     INKMBuffer hdr_loc2, INKMLoc field_loc2, bool * test_handle_string_release, bool first_time)
{

  int no_of_values1;
  int no_of_values2;
  int i;

  const char *str1 = NULL;
  const char *str2 = NULL;

  int length1 = 0;
  int length2 = 0;

  if (first_time == true) {
    *test_handle_string_release = true;
  }
  no_of_values1 = INKMimeHdrFieldValuesCount(bufp1, hdr_loc1, field_loc1);
  no_of_values2 = INKMimeHdrFieldValuesCount(bufp2, hdr_loc2, field_loc2);
  if ((no_of_values1 == INK_ERROR) || (no_of_values2 == INK_ERROR)) {
    SDK_RPRINT(test, "compare_field_values", "TestCase", TC_FAIL, "INKMimeHdrFieldValuesCount returns INK_ERROR");
    return INK_ERROR;
  }

  if (no_of_values1 != no_of_values2) {
    SDK_RPRINT(test, "compare_field_values", "TestCase", TC_FAIL, "Field Values not equal");
    return INK_ERROR;
  }

  for (i = 0; i < no_of_values1; i++) {
    if ((INKMimeHdrFieldValueStringGet(bufp1, hdr_loc1, field_loc1, i, &str1, &length1) != INK_SUCCESS) ||
        (INKMimeHdrFieldValueStringGet(bufp2, hdr_loc2, field_loc2, i, &str2, &length2) != INK_SUCCESS)
      ) {
      SDK_RPRINT(test, "compare_field_values", "TestCase", TC_FAIL,
                 "INKMimeHdrFieldValueStringGet doesn't return INK_SUCCESS");
      if ((str1 != INK_ERROR_PTR) || (str1 != NULL)) {
        if (INKHandleStringRelease(bufp1, field_loc1, str1) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleStringRelease", "TestCase1", TC_FAIL, "INKHandleStringRelease returns INK_ERROR");
          *test_handle_string_release = false;
        }
      }
      if ((str2 != INK_ERROR_PTR) || (str2 != NULL)) {
        if (INKHandleStringRelease(bufp2, field_loc2, str2) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleStringRelease", "TestCase2", TC_FAIL, "INKHandleStringRelease returns INK_ERROR");
          *test_handle_string_release = false;
        }
      }
      return INK_ERROR;
    }
    if (!((length1 == length2) && (strncmp(str1, str2, length1) == 0)
        )) {
      SDK_RPRINT(test, "compare_field_values", "TestCase", TC_FAIL, "Field Value %d differ from each other", i);
      if (INKHandleStringRelease(bufp1, field_loc1, str1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleStringRelease", "TestCase3", TC_FAIL, "INKHandleStringRelease returns INK_ERROR");
        *test_handle_string_release = false;
      }

      if (INKHandleStringRelease(bufp2, field_loc2, str2) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleStringRelease", "TestCase4", TC_FAIL, "INKHandleStringRelease returns INK_ERROR");
        *test_handle_string_release = false;
      }
      return INK_ERROR;
    }
  }

  return INK_SUCCESS;
}


REGRESSION_TEST(SDK_API_INKMimeHdrParse) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  const char *parse_string =
    "field1:field1Value1,field1Value2\r\nfield2:10,-34,45\r\nfield3:field3Value1,23\r\nfield2: 2345, field2Value2\r\n\r\n";
  const char *DUPLICATE_FIELD_NAME = "field2";
  const char *REMOVE_FIELD_NAME = "field3";

  INKMimeParser parser;

  INKMBuffer bufp1 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp2 = (INKMBuffer) INK_ERROR_PTR;
  INKMBuffer bufp3 = (INKMBuffer) INK_ERROR_PTR;

  INKMLoc mime_hdr_loc1 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc mime_hdr_loc2 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc mime_hdr_loc3 = (INKMLoc) INK_ERROR_PTR;

  INKMLoc field_loc1 = (INKMLoc) INK_ERROR_PTR;
  INKMLoc field_loc2 = (INKMLoc) INK_ERROR_PTR;

  const char *start;
  const char *end;
  char *temp;

  int retval;
  int hdrLength;

  bool test_passed_parser_create = false;
  bool test_passed_parse = false;
  bool test_passed_parser_clear = false;
  bool test_passed_parser_destroy = false;
  bool test_passed_mime_hdr_print = false;
  bool test_passed_mime_hdr_length_get = false;
  bool test_passed_mime_hdr_field_next_dup = false;
  bool test_passed_mime_hdr_copy = false;
  bool test_passed_mime_hdr_clone = false;
  bool test_passed_mime_hdr_field_remove = false;
  bool test_passed_mime_hdr_field_copy = false;
  bool test_passed_mime_hdr_field_copy_values = false;
  bool test_passed_handle_mloc_release = false;
  bool test_passed_handle_string_release = false;
  bool test_passed_mime_hdr_field_find = false;

  //Create Parser
  parser = INKMimeParserCreate();
  if (parser == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKMimeParserCreate", "TestCase1", TC_FAIL, "INKMimeParserCreate returns INK_ERROR_PTR");
  } else {
    SDK_RPRINT(test, "INKMimeParserCreate", "TestCase1", TC_PASS, "ok");
    test_passed_parser_create = true;
  }

  if (test_passed_parser_create == true) {
    // Parsing
    bufp1 = INKMBufferCreate();
    if (bufp1 == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "Cannot create buffer for parsing");
      SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_FAIL,
                 "Cannot run test as unable to create a buffer for parsing");
      SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL,
                 "Cannot run test as unable to create a buffer for parsing");
    } else {
      mime_hdr_loc1 = INKMimeHdrCreate(bufp1);
      if (mime_hdr_loc1 == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "Cannot create Mime hdr for parsing");
        SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_FAIL,
                   "Cannot run test as unable to create Mime Header for parsing");
        SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL,
                   "Cannot run test as unable to create Mime Header for parsing");

        if (INKMBufferDestroy(bufp1) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "Error in Destroying MBuffer");
        }
      } else {
        start = parse_string;
        end = parse_string + strlen(parse_string) + 1;
        if ((retval = INKMimeHdrParse(parser, bufp1, mime_hdr_loc1, &start, end)) == INK_PARSE_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "INKMimeHdrParse returns INK_PARSE_ERROR");
          SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_FAIL,
                     "Cannot run test as INKMimeHdrParse returned Error.");
          SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL,
                     "Cannot run test as INKMimeHdrParse returned Error.");
        } else {
          if (retval == INK_PARSE_DONE) {
            temp = convert_mime_hdr_to_string(bufp1, mime_hdr_loc1);    // Implements INKMimeHdrPrint.
            if (strcmp(parse_string, temp) == 0) {
              SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_PASS, "ok");
              SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_PASS, "ok");

              // INKMimeHdrLengthGet
              if ((hdrLength = INKMimeHdrLengthGet(bufp1, mime_hdr_loc1)) == INK_ERROR) {
                SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL, "INKMimeHdrLengthGet returns INK_ERROR");
              } else {
                if (hdrLength == (int) strlen(temp)) {
                  SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_PASS, "ok");
                  test_passed_mime_hdr_length_get = true;
                } else {
                  SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL, "Value's Mismatch");
                }
              }

              test_passed_parse = true;
              test_passed_mime_hdr_print = true;
            } else {
              SDK_RPRINT(test, "INKMimeHdrParse|INKMimeHdrPrint", "TestCase1", TC_FAIL,
                         "Incorrect parsing or incorrect Printing");
              SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL,
                         "Cannot run test as INKMimeHdrParse|INKMimeHdrPrint failed.");
            }

            INKfree(temp);
          } else {
            SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "Parsing Error");
            SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_FAIL,
                       "Cannot run test as INKMimeHdrParse returned error.");
            SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL,
                       "Cannot run test as INKMimeHdrParse returned error.");
          }
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrParse", "TestCase1", TC_FAIL, "Cannot run test as unable to create a parser");
    SDK_RPRINT(test, "INKMimeHdrPrint", "TestCase1", TC_FAIL, "Cannot run test as unable to create a parser");
    SDK_RPRINT(test, "INKMimeHdrLengthGet", "TestCase1", TC_FAIL, "Cannot run test as unable to create a parser");
  }


  // HOW DO I CHECK FOR PARSER CLEAR????
  if (test_passed_parser_create == true) {
    if (INKMimeParserClear(parser) == INK_ERROR) {
      SDK_RPRINT(test, "INKMimeParserClear", "TestCase1", TC_FAIL, "INKMimeParserClear returns INK_ERROR");
    } else {
      SDK_RPRINT(test, "INKMimeParserClear", "TestCase1", TC_PASS, "ok");
      test_passed_parser_clear = true;
    }
  } else {
    SDK_RPRINT(test, "INKMimeParserClear", "TestCase1", TC_FAIL, "Cannot run test as unable to create a parser");
  }


  if (test_passed_parser_create == true) {
    if (INKMimeParserDestroy(parser) != INK_SUCCESS) {
      SDK_RPRINT(test, "INKMimeParserDestroy", "TestCase1", TC_FAIL, "INKMimeParserDestroy doesn't return INK_SUCCESS");
    } else {
      SDK_RPRINT(test, "INKMimeParserDestroy", "TestCase1", TC_PASS, "ok");
      test_passed_parser_destroy = true;
    }
  } else {
    SDK_RPRINT(test, "INKMimeParserDestroy", "TestCase1", TC_FAIL, "Cannot run test as unable to create a parser");
  }

  //INKMimeHdrFieldNextDup
  if (test_passed_parse == true) {
    if ((field_loc1 = INKMimeHdrFieldFind(bufp1, mime_hdr_loc1, DUPLICATE_FIELD_NAME, -1)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrFieldNextDup", "TestCase1", TC_FAIL, "INKMimeHdrFieldFind returns INK_ERROR_PTR");
      SDK_RPRINT(test, "INKMimeHdrFieldFind", "TestCase1", TC_PASS, "INKMimeHdrFieldFind returns INK_ERROR_PTR");
    } else {
      const char *fieldName;
      int length;
      if ((fieldName = INKMimeHdrFieldNameGet(bufp1, mime_hdr_loc1, field_loc1, &length)) != INK_ERROR_PTR) {
        if (strcmp(fieldName, DUPLICATE_FIELD_NAME) == 0) {
          SDK_RPRINT(test, "INKMimeHdrFieldFind", "TestCase1", TC_PASS, "ok");
          test_passed_mime_hdr_field_find = true;
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldFind", "TestCase1", TC_PASS,
                     "INKMimeHdrFieldFind returns incorrect field pointer");
        }
        if (INKHandleStringRelease(bufp1, mime_hdr_loc1, fieldName) != INK_SUCCESS) {
          SDK_RPRINT(test, "INKMimeHdrFieldFind", "TestCase1", TC_PASS,
                     "Unable to release handle to field name acquired using INKMimeHdrFieldNameGet");
        }
      } else {
        SDK_RPRINT(test, "INKMimeHdrFieldFind", "TestCase1", TC_PASS, "INKMimeHdrFieldNameGet returns INK_ERROR_PTR");
      }

      if ((field_loc2 = INKMimeHdrFieldNextDup(bufp1, mime_hdr_loc1, field_loc1)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldNextDup", "TestCase1", TC_FAIL,
                   "INKMimeHdrFieldNextDup returns INK_ERROR_PTR");
      } else {
        if (compare_field_names(test, bufp1, mime_hdr_loc1, field_loc1, bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrFieldNextDup", "TestCase1", TC_FAIL, "Incorrect Pointer");
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldNextDup", "TestCase1", TC_PASS, "ok");
          test_passed_mime_hdr_field_next_dup = true;
        }
      }

      // INKHandleMLocRelease
      if (INKHandleMLocRelease(bufp1, mime_hdr_loc1, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase1", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase1", TC_PASS, "ok");
        test_passed_handle_mloc_release = true;
      }

      if ((field_loc2 != NULL) && (field_loc2 != INK_ERROR_PTR)) {
        if (INKHandleMLocRelease(bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase2", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
          test_passed_handle_mloc_release = false;
        } else {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase2", TC_PASS, "ok");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase1", TC_FAIL, "Unable to run test as parsing failed.");
  }

  //INKMimeHdrCopy
  if (test_passed_parse == true) {
    // Parsing
    bufp2 = INKMBufferCreate();
    if (bufp2 == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "Cannot create buffer for copying.");
    } else {
      mime_hdr_loc2 = INKMimeHdrCreate(bufp2);
      if (mime_hdr_loc2 == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "Cannot create Mime hdr for copying");
        if (INKMBufferDestroy(bufp2) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "Error in Destroying MBuffer");
        }
      } else {
        if (INKMimeHdrCopy(bufp2, mime_hdr_loc2, bufp1, mime_hdr_loc1) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "INKMimeHdrCopy returns INK_ERROR");
        } else {
          temp = convert_mime_hdr_to_string(bufp2, mime_hdr_loc2);      // Implements INKMimeHdrPrint.
          if (strcmp(parse_string, temp) == 0) {
            SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_PASS, "ok");
            test_passed_mime_hdr_copy = true;
          } else {
            SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "Value's Mismatch");
          }
          INKfree(temp);
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrCopy", "TestCase1", TC_FAIL, "Unable to run test as parsing failed.");
  }

  bufp3 = INKMBufferCreate();
  mime_hdr_loc3 = INKMimeHdrCreate(bufp3);
  test_passed_mime_hdr_clone = true;

  // INKMimeHdrFieldRemove
  if (test_passed_mime_hdr_copy == true) {
    if ((field_loc1 = INKMimeHdrFieldFind(bufp2, mime_hdr_loc2, REMOVE_FIELD_NAME, -1)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_FAIL, "INKMimeHdrFieldFind returns INK_ERROR_PTR");
    } else {
      if (INKMimeHdrFieldRemove(bufp2, mime_hdr_loc2, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_FAIL, "INKMimeHdrFieldRemove returns INK_ERROR_PTR");
      } else {
        if ((field_loc2 = INKMimeHdrFieldFind(bufp2, mime_hdr_loc2, REMOVE_FIELD_NAME, -1)) == INK_ERROR_PTR) {
          SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_FAIL, "INKMimeHdrFieldFind returns INK_ERROR_PTR");
        } else {
          if ((field_loc2 == NULL) || (field_loc1 != field_loc2)) {
            test_passed_mime_hdr_field_remove = true;
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_FAIL, "Field Not Removed");
          }

          if ((test_passed_mime_hdr_field_remove == true)) {
            if (INKMimeHdrFieldAppend(bufp2, mime_hdr_loc2, field_loc1) == INK_ERROR) {
              SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_FAIL,
                         "Unable to readd the field to mime header. Probably destroyed");
              test_passed_mime_hdr_field_remove = false;
            } else {
              SDK_RPRINT(test, "INKMimeHdrFieldRemove", "TestCase1", TC_PASS, "ok");
            }
          }
        }
      }

      // INKHandleMLocRelease
      if (INKHandleMLocRelease(bufp2, mime_hdr_loc2, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase3", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
        test_passed_handle_mloc_release = false;
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase3", TC_PASS, "ok");
      }

      if ((field_loc2 != NULL) && (field_loc2 != INK_ERROR_PTR)) {
        if (INKHandleMLocRelease(bufp2, mime_hdr_loc2, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase4", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
          test_passed_handle_mloc_release = false;
        } else {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase4", TC_PASS, "ok");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldNext", "TestCase1", TC_FAIL, "Unable to run test as parsing failed.");
  }

  // INKMimeHdrFieldCopy && INKHandleStringRelease
  if (test_passed_mime_hdr_copy == true) {
    if ((field_loc1 = INKMimeHdrFieldCreate(bufp2, mime_hdr_loc2)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL, "Unable to create field for Copying");
    } else {
      if ((field_loc2 = INKMimeHdrFieldGet(bufp1, mime_hdr_loc1, 0)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL, "Unable to get source field for copying");
      } else {
        if (INKMimeHdrFieldCopy(bufp2, mime_hdr_loc2, field_loc1, bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL, "INKMimeHdrFieldCopy returns INK_ERROR");
        } else {
          if ((compare_field_names(test, bufp2, mime_hdr_loc2, field_loc1, bufp1, mime_hdr_loc1, field_loc2) ==
               INK_ERROR) ||
              (compare_field_values
               (test, bufp2, mime_hdr_loc2, field_loc1, bufp1, mime_hdr_loc1, field_loc2,
                &test_passed_handle_string_release, true) == INK_ERROR)
            ) {
            SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL, "Value's Mismatch");
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_PASS, "ok");
            test_passed_mime_hdr_field_copy = true;
          }
        }
      }
      if (INKHandleMLocRelease(bufp2, mime_hdr_loc2, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase5", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
        test_passed_handle_mloc_release = false;
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase5", TC_PASS, "ok");
      }

      if ((field_loc2 != NULL) && (field_loc2 != INK_ERROR_PTR)) {
        if (INKHandleMLocRelease(bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase6", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
          test_passed_handle_mloc_release = false;
        } else {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase6", TC_PASS, "ok");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL,
               "Unable to run test as bufp2 might not have been created");
  }

  // INKMimeHdrFieldClone && INKHandleStringRelease
  if (test_passed_mime_hdr_clone == true) {
    field_loc1 = NULL;
    field_loc2 = NULL;
    if ((field_loc2 = INKMimeHdrFieldGet(bufp1, mime_hdr_loc1, 0)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrFieldClone", "TestCase1", TC_FAIL, "Unable to get source field for copying");
    } else {
      if ((field_loc1 = INKMimeHdrFieldClone(bufp3, mime_hdr_loc3, bufp1, mime_hdr_loc1, field_loc2)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldClone", "TestCase1", TC_FAIL, "INKMimeHdrFieldClone returns INK_ERROR_PTR");
      } else {
        if ((compare_field_names(test, bufp3, mime_hdr_loc3, field_loc1, bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR)
            ||
            (compare_field_values
             (test, bufp3, mime_hdr_loc3, field_loc1, bufp1, mime_hdr_loc1, field_loc2,
              &test_passed_handle_string_release, true) == INK_ERROR)
          ) {
          SDK_RPRINT(test, "INKMimeHdrFieldClone", "TestCase1", TC_FAIL, "Value's Mismatch");
        } else {
          SDK_RPRINT(test, "INKMimeHdrFieldClone", "TestCase1", TC_PASS, "ok");
        }
      }
    }
    if ((field_loc1 != NULL) && (field_loc1 != INK_ERROR_PTR)) {
      if (INKHandleMLocRelease(bufp3, mime_hdr_loc3, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase7", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
        test_passed_handle_mloc_release = false;
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase7", TC_PASS, "ok");
      }
    }

    if ((field_loc2 != NULL) && (field_loc2 != INK_ERROR_PTR)) {
      if (INKHandleMLocRelease(bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase8", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
        test_passed_handle_mloc_release = false;
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase8", TC_PASS, "ok");
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldClone", "TestCase1", TC_FAIL,
               "Unable to run test as bufp3 might not have been created");
  }

  // INKMimeHdrFieldCopyValues && INKHandleStringRelease
  if (test_passed_mime_hdr_copy == true) {
    if ((field_loc1 = INKMimeHdrFieldCreate(bufp2, mime_hdr_loc2)) == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKMimeHdrFieldCopyValues", "TestCase1", TC_FAIL, "Unable to create field for Copying");
    } else {
      if ((field_loc2 = INKMimeHdrFieldGet(bufp1, mime_hdr_loc1, 0)) == INK_ERROR_PTR) {
        SDK_RPRINT(test, "INKMimeHdrFieldCopyValues", "TestCase1", TC_FAIL, "Unable to get source field for copying");
      } else {
        if (INKMimeHdrFieldCopyValues(bufp2, mime_hdr_loc2, field_loc1, bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKMimeHdrFieldCopyValues", "TestCase1", TC_FAIL, "INKMimeHdrFieldCopy returns INK_ERROR");
        } else {
          if (compare_field_values
              (test, bufp2, mime_hdr_loc2, field_loc1, bufp1, mime_hdr_loc1, field_loc2,
               &test_passed_handle_string_release, false) == INK_ERROR) {
            SDK_RPRINT(test, "INKMimeHdrFieldCopyValues", "TestCase1", TC_FAIL, "Value's Mismatch");
          } else {
            SDK_RPRINT(test, "INKMimeHdrFieldCopyValues", "TestCase1", TC_PASS, "ok");
            test_passed_mime_hdr_field_copy_values = true;
          }
        }
      }
      if (INKHandleMLocRelease(bufp2, mime_hdr_loc2, field_loc1) == INK_ERROR) {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase9", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
        test_passed_handle_mloc_release = false;
      } else {
        SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase9", TC_PASS, "ok");
      }

      if ((field_loc2 != NULL) && (field_loc2 != INK_ERROR_PTR)) {
        if (INKHandleMLocRelease(bufp1, mime_hdr_loc1, field_loc2) == INK_ERROR) {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase10", TC_FAIL, "INKHandleMLocRelease returns INK_ERROR");
          test_passed_handle_mloc_release = false;
        } else {
          SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase10", TC_PASS, "ok");
        }
      }
    }
  } else {
    SDK_RPRINT(test, "INKMimeHdrFieldCopy", "TestCase1", TC_FAIL,
               "Unable to run test as bufp2 might not have been created");
  }

  if ((INKMimeHdrDestroy(bufp1, mime_hdr_loc1) == INK_ERROR) ||
      (INKMimeHdrDestroy(bufp2, mime_hdr_loc2) == INK_ERROR) || (INKMimeHdrDestroy(bufp3, mime_hdr_loc3) == INK_ERROR)
    ) {
    SDK_RPRINT(test, "", "TestCase", TC_FAIL, "INKMimeHdrDestroy returns INK_ERROR");
  }

  if ((INKHandleMLocRelease(bufp1, INK_NULL_MLOC, mime_hdr_loc1) == INK_ERROR) ||
      (INKHandleMLocRelease(bufp2, INK_NULL_MLOC, mime_hdr_loc2) == INK_ERROR) ||
      (INKHandleMLocRelease(bufp3, INK_NULL_MLOC, mime_hdr_loc3) == INK_ERROR)
    ) {
    SDK_RPRINT(test, "INKHandleMLocRelease", "TestCase11|12|13", TC_FAIL, "Unable to release handle to Mime Hdrs");
    test_passed_handle_mloc_release = false;
  }

  if ((INKMBufferDestroy(bufp1) == INK_ERROR) ||
      (INKMBufferDestroy(bufp2) == INK_ERROR) || (INKMBufferDestroy(bufp3) == INK_ERROR)
    ) {
    SDK_RPRINT(test, "", "TestCase", TC_FAIL, "INKMBufferDestroy returns INK_ERROR");
  }

  if (test_passed_handle_string_release == true) {
    SDK_RPRINT(test, "INKHandleStringRelease", "All", TC_PASS, "ok");
  } else {
    SDK_RPRINT(test, "INKHandleStringRelease", "TestCase", TC_PASS, "Returned INK_ERROR");
  }

  if ((test_passed_parser_create != true) ||
      (test_passed_parse != true) ||
      (test_passed_parser_clear != true) ||
      (test_passed_parser_destroy != true) ||
      (test_passed_mime_hdr_print != true) ||
      (test_passed_mime_hdr_length_get != true) ||
      (test_passed_mime_hdr_field_next_dup != true) ||
      (test_passed_mime_hdr_copy != true) ||
      (test_passed_mime_hdr_clone != true) ||
      (test_passed_mime_hdr_field_remove != true) ||
      (test_passed_mime_hdr_field_copy != true) ||
      (test_passed_mime_hdr_field_copy_values != true) ||
      (test_passed_handle_mloc_release != true) ||
      (test_passed_handle_string_release != true) || (test_passed_mime_hdr_field_find != true)
    ) {
    *pstatus = REGRESSION_TEST_FAILED;
  } else {
    *pstatus = REGRESSION_TEST_PASSED;
  }

}

//////////////////////////////////////////////
//       SDK_API_INKUrlParse
//
// Unit Test for API: INKUrlParse
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKUrlParse) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  const char *url = "http://abc:def@www.example.com:3426/homepage.cgi;ab?abc=def#abc";
  const char *start;
  const char *end;
  char *temp;

  int retval;

  INKMBuffer bufp;
  INKMLoc url_loc = (INKMLoc) INK_ERROR_PTR;
  bool test_passed_parse_url = false;
  int length;

  *pstatus = REGRESSION_TEST_INPROGRESS;


  bufp = INKMBufferCreate();
  if (bufp == INK_ERROR_PTR) {
    SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "Cannot create buffer for parsing url");
  } else {
    url_loc = INKUrlCreate(bufp);
    if (url_loc == INK_ERROR_PTR) {
      SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "Cannot create Url for parsing the url");
      if (INKMBufferDestroy(bufp) == INK_ERROR) {
        SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "Error in Destroying MBuffer");
      }
    } else {
      start = url;
      end = url + strlen(url) + 1;
      if ((retval = INKUrlParse(bufp, url_loc, &start, end)) == INK_PARSE_ERROR) {
        SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "INKUrlParse returns INK_PARSE_ERROR");
      } else {
        if (retval == INK_PARSE_DONE) {
          temp = INKUrlStringGet(bufp, url_loc, &length);
          if (temp == INK_ERROR_PTR) {
            SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "INKUrlStringGet returns INK_ERROR_PTR");
          } else {
            if (strncmp(url, temp, length) == 0) {
              SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_PASS, "ok");
              test_passed_parse_url = true;
            } else {
              SDK_RPRINT(test, "INKUrlParse", "TestCase1", TC_FAIL, "Value's Mismatch");
            }
            INKfree(temp);
          }
        } else {
          SDK_RPRINT(test, "INKHttpHdrParseReq", "TestCase1", TC_FAIL, "Parsing Error");
        }
      }
    }
  }

  if (test_passed_parse_url != true) {
    *pstatus = REGRESSION_TEST_FAILED;
  } else {
    *pstatus = REGRESSION_TEST_PASSED;
  }

  INKUrlDestroy(bufp, url_loc);

  INKHandleMLocRelease(bufp, INK_NULL_MLOC, url_loc);

  INKMBufferDestroy(bufp);

  return;
}

//////////////////////////////////////////////
//       SDK_API_INKTextLog
//
// Unit Test for APIs: INKTextLogObjectCreate
//                     INKTextLogObjectWrite
//                     INKTextLogObjectDestroy
//                     INKTextLogObjectFlush
//////////////////////////////////////////////
#define LOG_TEST_PATTERN "SDK team rocks"

typedef struct
{
  RegressionTest *test;
  int *pstatus;
  char *fullpath_logname;
  unsigned long magic;
} LogTestData;


static int
log_test_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(edata);
  INKFile filep;
  char buf[1024];
  bool str_found;

  INKAssert(event == INK_EVENT_TIMEOUT);

  LogTestData *data = (LogTestData *) INKContDataGet(contp);
  INKAssert(data->magic == MAGIC_ALIVE);

  // Verify content was correctly written into log file

  if ((filep = INKfopen(data->fullpath_logname, "r")) == NULL) {
    SDK_RPRINT(data->test, "INKTextLogObject", "TestCase1", TC_FAIL,
               "can not open log file %s", data->fullpath_logname);
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    return -1;
  }

  str_found = false;
  while (INKfgets(filep, buf, 1024) != NULL) {
    if (strstr(buf, LOG_TEST_PATTERN) != NULL) {
      str_found = true;
      break;
    }
  }
  INKfclose(filep);
  if (str_found == false) {
    SDK_RPRINT(data->test, "INKTextLogObject", "TestCase1", TC_FAIL,
               "can not find pattern %s in log file", LOG_TEST_PATTERN);
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    return -1;
  }

  *(data->pstatus) = REGRESSION_TEST_PASSED;

  data->magic = MAGIC_DEAD;
  INKfree(data->fullpath_logname);
  INKfree(data);
  data = NULL;

  return -1;
}

REGRESSION_TEST(SDK_API_INKTextLog) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKTextLogObject log;
  INKReturnCode retVal;

  char logname[128];
  char fullpath_logname[128];

  /* Generate a random log file name, so if we run the test several times, we won't use the
     same log file name. */
  char *tmp = REC_ConfigReadString("proxy.config.log2.logfile_dir");
  snprintf(logname, sizeof(logname), "RegressionTestLog%d.log", (int) getpid());
  snprintf(fullpath_logname, sizeof(fullpath_logname), "%s/%s", tmp, logname);
  // xfree(tmp);

  retVal = INKTextLogObjectCreate(logname, INK_LOG_MODE_ADD_TIMESTAMP, &log);
  if (retVal != INK_SUCCESS) {
    SDK_RPRINT(test, "INKTextLogObjectCreate", "TestCase1", TC_FAIL, "can not create log object");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKTextLogObjectCreate", "TestCase1", TC_PASS, "ok");
  }

  retVal = INKTextLogObjectWrite(log, (char*)LOG_TEST_PATTERN);
  if (retVal != INK_SUCCESS) {
    SDK_RPRINT(test, "INKTextLogObjectWrite", "TestCase1", TC_FAIL, "can not write to log object");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKTextLogObjectWrite", "TestCase1", TC_PASS, "ok");
  }

  retVal = INKTextLogObjectFlush(log);
  if (retVal != INK_SUCCESS) {
    SDK_RPRINT(test, "INKTextLogObjectFlush", "TestCase1", TC_FAIL, "can not flush log object");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKTextLogObjectFlush", "TestCase1", TC_PASS, "ok");
  }

  retVal = INKTextLogObjectDestroy(log);
  if (retVal != INK_SUCCESS) {
    SDK_RPRINT(test, "INKTextLogObjectDestroy", "TestCase1", TC_FAIL, "can not destroy log object");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  } else {
    SDK_RPRINT(test, "INKTextLogObjectDestroy", "TestCase1", TC_PASS, "ok");
  }


  INKCont log_test_cont = INKContCreate(log_test_handler, INKMutexCreate());
  LogTestData *data = (LogTestData *) INKmalloc(sizeof(LogTestData));
  data->test = test;
  data->pstatus = pstatus;
  data->fullpath_logname = INKstrdup(fullpath_logname);
  data->magic = MAGIC_ALIVE;
  INKContDataSet(log_test_cont, data);

  INKContSchedule(log_test_cont, 5000);
  return;
}


//////////////////////////////////////////////
//       SDK_API_INKMgmtGet
//
// Unit Test for APIs: INKMgmtCounterGet
//                     INKMgmtFloatGet
//                     INKMgmtIntGet
//                     INKMgmtStringGet
//////////////////////////////////////////////

REGRESSION_TEST(SDK_API_INKMgmtGet) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  const char *CONFIG_PARAM_COUNTER_NAME = "proxy.process.http.total_parent_proxy_connections";
  int CONFIG_PARAM_COUNTER_VALUE = 0;

  const char *CONFIG_PARAM_FLOAT_NAME = "proxy.config.http.background_fill_completed_threshold";
  float CONFIG_PARAM_FLOAT_VALUE = 0.5;

  const char *CONFIG_PARAM_INT_NAME = "proxy.config.http.cache.http";
  int CONFIG_PARAM_INT_VALUE = 1;

  const char *CONFIG_PARAM_STRING_NAME = "proxy.config.product_name";
  const char *CONFIG_PARAM_STRING_VALUE = "Traffic Server";

  *pstatus = REGRESSION_TEST_INPROGRESS;

  int retVal;
  int err = 0;
  INKMgmtCounter cvalue = 0;
  INKMgmtFloat fvalue = 0.0;
  INKMgmtInt ivalue = -1;
  INKMgmtString svalue = NULL;

  retVal = INKMgmtCounterGet(CONFIG_PARAM_COUNTER_NAME, &cvalue);
  if (retVal == 0) {
    SDK_RPRINT(test, "INKMgmtCounterGet", "TestCase1.1", TC_FAIL, "can not get value of param %s",
               CONFIG_PARAM_COUNTER_NAME);
    err = 1;
  } else if (cvalue != CONFIG_PARAM_COUNTER_VALUE) {
    SDK_RPRINT(test, "INKMgmtCounterGet", "TestCase1.1", TC_FAIL,
               "got incorrect value of param %s, should have been %d, found %d", CONFIG_PARAM_COUNTER_NAME,
               CONFIG_PARAM_COUNTER_VALUE, cvalue);
    err = 1;
  } else {
    SDK_RPRINT(test, "INKMgmtCounterGet", "TestCase1.1", TC_PASS, "ok");
  }

  retVal = INKMgmtFloatGet(CONFIG_PARAM_FLOAT_NAME, &fvalue);
  if ((retVal == 0) || (fvalue != CONFIG_PARAM_FLOAT_VALUE)) {
    SDK_RPRINT(test, "INKMgmtFloatGet", "TestCase2", TC_FAIL, "can not get value of param %s", CONFIG_PARAM_FLOAT_NAME);
    err = 1;
  } else {
    SDK_RPRINT(test, "INKMgmtFloatGet", "TestCase1.2", TC_PASS, "ok");
  }

  retVal = INKMgmtIntGet(CONFIG_PARAM_INT_NAME, &ivalue);
  if ((retVal == 0) || (ivalue != CONFIG_PARAM_INT_VALUE)) {
    SDK_RPRINT(test, "INKMgmtIntGet", "TestCase1.3", TC_FAIL, "can not get value of param %s", CONFIG_PARAM_INT_NAME);
    err = 1;
  } else {
    SDK_RPRINT(test, "INKMgmtIntGet", "TestCase1.3", TC_PASS, "ok");
  }

  retVal = INKMgmtStringGet(CONFIG_PARAM_STRING_NAME, &svalue);
  if (retVal == 0) {
    SDK_RPRINT(test, "INKMgmtStringGet", "TestCase1.4", TC_FAIL, "can not get value of param %s",
               CONFIG_PARAM_STRING_NAME);
    err = 1;
  } else if (strcmp(svalue, CONFIG_PARAM_STRING_VALUE) != 0) {
    SDK_RPRINT(test, "INKMgmtStringGet", "TestCase1.4", TC_FAIL,
               "got incorrect value of param %s, should have been \"%s\", found \"%s\"", CONFIG_PARAM_STRING_NAME,
               CONFIG_PARAM_STRING_VALUE, svalue);
    err = 1;
  } else {
    SDK_RPRINT(test, "INKMgmtStringGet", "TestCase1.4", TC_PASS, "ok");
  }

  if (err) {
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  *pstatus = REGRESSION_TEST_PASSED;
  SDK_RPRINT(test, "INKMgmtGet", "TestCase1", TC_PASS, "ok");
  return;
}


//////////////////////////////////////////////
//       SDK_API_INKMgmtUpdateRegister
//
// Unit Test for APIs: INKMgmtUpdateRegister
//
// FIX ME: How to test this API automatically
// as it requires a GUI action ??
//////////////////////////////////////////////

// dummy handler. Should never get called.
int
gui_update_handler(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(contp);
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(edata);
  INKReleaseAssert(!"gui_update_handler should not be called");
  return 0;
}

REGRESSION_TEST(SDK_API_INKMgmtUpdateRegister) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKCont mycont = INKContCreate(gui_update_handler, INKMutexCreate());

  if (INKMgmtUpdateRegister(mycont, "myPlugin", "myPluginPath/myGui.cgi") != INK_SUCCESS) {
    SDK_RPRINT(test, "INKMgmtUpdateRegister", "TestCase1", TC_FAIL, "can not register plugin interface");
    *pstatus = REGRESSION_TEST_FAILED;
  } else {
    *pstatus = REGRESSION_TEST_PASSED;
  }
  return;
}


//////////////////////////////////////////////
//       SDK_API_INKConstant
//
// Unit Test for APIs: All INK_XXX constants
//
//////////////////////////////////////////////

#define PRINT_DIFF( _x ) \
{ \
      if ( _x - ORIG_##_x != 0) { \
          test_passed = false; \
          SDK_RPRINT (test, "##_x", "TestCase1", TC_FAIL, \
		      "%s:Original Value = %d; New Value = %d \n", #_x,_x, ORIG_##_x); \
      } \
}


typedef enum
{
  ORIG_INK_PARSE_ERROR = -1,
  ORIG_INK_PARSE_DONE = 0,
  ORIG_INK_PARSE_OK = 1,
  ORIG_INK_PARSE_CONT = 2
} ORIG_INKParseResult;

typedef enum
{
  ORIG_INK_HTTP_TYPE_UNKNOWN,
  ORIG_INK_HTTP_TYPE_REQUEST,
  ORIG_INK_HTTP_TYPE_RESPONSE
} ORIG_INKHttpType;

typedef enum
{
  ORIG_INK_HTTP_STATUS_NONE = 0,

  ORIG_INK_HTTP_STATUS_CONTINUE = 100,
  ORIG_INK_HTTP_STATUS_SWITCHING_PROTOCOL = 101,

  ORIG_INK_HTTP_STATUS_OK = 200,
  ORIG_INK_HTTP_STATUS_CREATED = 201,
  ORIG_INK_HTTP_STATUS_ACCEPTED = 202,
  ORIG_INK_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION = 203,
  ORIG_INK_HTTP_STATUS_NO_CONTENT = 204,
  ORIG_INK_HTTP_STATUS_RESET_CONTENT = 205,
  ORIG_INK_HTTP_STATUS_PARTIAL_CONTENT = 206,

  ORIG_INK_HTTP_STATUS_MULTIPLE_CHOICES = 300,
  ORIG_INK_HTTP_STATUS_MOVED_PERMANENTLY = 301,
  ORIG_INK_HTTP_STATUS_MOVED_TEMPORARILY = 302,
  ORIG_INK_HTTP_STATUS_SEE_OTHER = 303,
  ORIG_INK_HTTP_STATUS_NOT_MODIFIED = 304,
  ORIG_INK_HTTP_STATUS_USE_PROXY = 305,

  ORIG_INK_HTTP_STATUS_BAD_REQUEST = 400,
  ORIG_INK_HTTP_STATUS_UNAUTHORIZED = 401,
  ORIG_INK_HTTP_STATUS_PAYMENT_REQUIRED = 402,
  ORIG_INK_HTTP_STATUS_FORBIDDEN = 403,
  ORIG_INK_HTTP_STATUS_NOT_FOUND = 404,
  ORIG_INK_HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
  ORIG_INK_HTTP_STATUS_NOT_ACCEPTABLE = 406,
  ORIG_INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED = 407,
  ORIG_INK_HTTP_STATUS_REQUEST_TIMEOUT = 408,
  ORIG_INK_HTTP_STATUS_CONFLICT = 409,
  ORIG_INK_HTTP_STATUS_GONE = 410,
  ORIG_INK_HTTP_STATUS_LENGTH_REQUIRED = 411,
  ORIG_INK_HTTP_STATUS_PRECONDITION_FAILED = 412,
  ORIG_INK_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE = 413,
  ORIG_INK_HTTP_STATUS_REQUEST_URI_TOO_LONG = 414,
  ORIG_INK_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415,

  ORIG_INK_HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
  ORIG_INK_HTTP_STATUS_NOT_IMPLEMENTED = 501,
  ORIG_INK_HTTP_STATUS_BAD_GATEWAY = 502,
  ORIG_INK_HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
  ORIG_INK_HTTP_STATUS_GATEWAY_TIMEOUT = 504,
  ORIG_INK_HTTP_STATUS_HTTPVER_NOT_SUPPORTED = 505
} ORIG_INKHttpStatus;

typedef enum
{
  ORIG_INK_HTTP_READ_REQUEST_HDR_HOOK,
  ORIG_INK_HTTP_OS_DNS_HOOK,
  ORIG_INK_HTTP_SEND_REQUEST_HDR_HOOK,
  ORIG_INK_HTTP_READ_CACHE_HDR_HOOK,
  ORIG_INK_HTTP_READ_RESPONSE_HDR_HOOK,
  ORIG_INK_HTTP_SEND_RESPONSE_HDR_HOOK,
  ORIG_INK_HTTP_REQUEST_TRANSFORM_HOOK,
  ORIG_INK_HTTP_RESPONSE_TRANSFORM_HOOK,
  ORIG_INK_HTTP_SELECT_ALT_HOOK,
  ORIG_INK_HTTP_TXN_START_HOOK,
  ORIG_INK_HTTP_TXN_CLOSE_HOOK,
  ORIG_INK_HTTP_SSN_START_HOOK,
  ORIG_INK_HTTP_SSN_CLOSE_HOOK,
  ORIG_INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK,
  ORIG_INK_HTTP_PRE_REMAP_HOOK,
  ORIG_INK_HTTP_POST_REMAP_HOOK,
  ORIG_INK_HTTP_LAST_HOOK
} ORIG_INKHttpHookID;

typedef enum
{
  ORIG_INK_EVENT_NONE = 0,
  ORIG_INK_EVENT_IMMEDIATE = 1,
  ORIG_INK_EVENT_TIMEOUT = 2,
  ORIG_INK_EVENT_ERROR = 3,
  ORIG_INK_EVENT_CONTINUE = 4,

  ORIG_INK_EVENT_VCONN_READ_READY = 100,
  ORIG_INK_EVENT_VCONN_WRITE_READY = 101,
  ORIG_INK_EVENT_VCONN_READ_COMPLETE = 102,
  ORIG_INK_EVENT_VCONN_WRITE_COMPLETE = 103,
  ORIG_INK_EVENT_VCONN_EOS = 104,

  ORIG_INK_EVENT_NET_CONNECT = 200,
  ORIG_INK_EVENT_NET_CONNECT_FAILED = 201,
  ORIG_INK_EVENT_NET_ACCEPT = 202,
  ORIG_INK_EVENT_NET_ACCEPT_FAILED = 204,

  ORIG_INK_EVENT_HOST_LOOKUP = 500,

  ORIG_INK_EVENT_CACHE_OPEN_READ = 1102,
  ORIG_INK_EVENT_CACHE_OPEN_READ_FAILED = 1103,
  ORIG_INK_EVENT_CACHE_OPEN_WRITE = 1108,
  ORIG_INK_EVENT_CACHE_OPEN_WRITE_FAILED = 1109,
  ORIG_INK_EVENT_CACHE_REMOVE = 1112,
  ORIG_INK_EVENT_CACHE_REMOVE_FAILED = 1113,
  ORIG_INK_EVENT_CACHE_SCAN = 1120,
  ORIG_INK_EVENT_CACHE_SCAN_FAILED = 1121,
  ORIG_INK_EVENT_CACHE_SCAN_OBJECT = 1122,
  ORIG_INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED = 1123,
  ORIG_INK_EVENT_CACHE_SCAN_OPERATION_FAILED = 1124,
  ORIG_INK_EVENT_CACHE_SCAN_DONE = 1125,

  ORIG_INK_EVENT_HTTP_CONTINUE = 60000,
  ORIG_INK_EVENT_HTTP_ERROR = 60001,
  ORIG_INK_EVENT_HTTP_READ_REQUEST_HDR = 60002,
  ORIG_INK_EVENT_HTTP_OS_DNS = 60003,
  ORIG_INK_EVENT_HTTP_SEND_REQUEST_HDR = 60004,
  ORIG_INK_EVENT_HTTP_READ_CACHE_HDR = 60005,
  ORIG_INK_EVENT_HTTP_READ_RESPONSE_HDR = 60006,
  ORIG_INK_EVENT_HTTP_SEND_RESPONSE_HDR = 60007,
  ORIG_INK_EVENT_HTTP_REQUEST_TRANSFORM = 60008,
  ORIG_INK_EVENT_HTTP_RESPONSE_TRANSFORM = 60009,
  ORIG_INK_EVENT_HTTP_SELECT_ALT = 60010,
  ORIG_INK_EVENT_HTTP_TXN_START = 60011,
  ORIG_INK_EVENT_HTTP_TXN_CLOSE = 60012,
  ORIG_INK_EVENT_HTTP_SSN_START = 60013,
  ORIG_INK_EVENT_HTTP_SSN_CLOSE = 60014,
  ORIG_INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE = 60015,

  ORIG_INK_EVENT_MGMT_UPDATE = 60100
} ORIG_INKEvent;

typedef enum
{
  ORIG_INK_CACHE_LOOKUP_MISS,
  ORIG_INK_CACHE_LOOKUP_HIT_STALE,
  ORIG_INK_CACHE_LOOKUP_HIT_FRESH
} ORIG_INKCacheLookupResult;

typedef enum
{
  ORIG_INK_CACHE_DATA_TYPE_NONE,
  ORIG_INK_CACHE_DATA_TYPE_HTTP,
  ORIG_INK_CACHE_DATA_TYPE_OTHER
} ORIG_INKCacheDataType;

typedef enum
{
  ORIG_INK_CACHE_ERROR_NO_DOC = -20400,
  ORIG_INK_CACHE_ERROR_DOC_BUSY = -20401,
  ORIG_INK_CACHE_ERROR_NOT_READY = -20407
} ORIG_INKCacheError;

typedef enum
{
  ORIG_INK_CACHE_SCAN_RESULT_DONE = 0,
  ORIG_INK_CACHE_SCAN_RESULT_CONTINUE = 1,
  ORIG_INK_CACHE_SCAN_RESULT_DELETE = 10,
  ORIG_INK_CACHE_SCAN_RESULT_DELETE_ALL_ALTERNATES,
  ORIG_INK_CACHE_SCAN_RESULT_UPDATE,
  ORIG_INK_CACHE_SCAN_RESULT_RETRY
} ORIG_INKCacheScanResult;

typedef enum
{
  ORIG_INK_DATA_ALLOCATE,
  ORIG_INK_DATA_MALLOCED,
  ORIG_INK_DATA_CONSTANT
} ORIG_INKIOBufferDataFlags;

typedef enum
{
  ORIG_INK_VC_CLOSE_ABORT = -1,
  ORIG_INK_VC_CLOSE_NORMAL = 1
} ORIG_INKVConnCloseFlags;

typedef enum
{
  ORIG_INK_SDK_VERSION_2_0 = 0
} ORIG_INKSDKVersion;

typedef enum
{
  ORIG_INK_ERROR = -1,
  ORIG_INK_SUCCESS = 0
} ORIG_INKReturnCode;


REGRESSION_TEST(SDK_API_INKConstant) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;
  bool test_passed = true;

  PRINT_DIFF(INK_PARSE_ERROR);
  PRINT_DIFF(INK_PARSE_DONE);
  PRINT_DIFF(INK_PARSE_OK);
  PRINT_DIFF(INK_PARSE_CONT);

  PRINT_DIFF(INK_HTTP_STATUS_NONE);
  PRINT_DIFF(INK_HTTP_STATUS_CONTINUE);
  PRINT_DIFF(INK_HTTP_STATUS_SWITCHING_PROTOCOL);
  PRINT_DIFF(INK_HTTP_STATUS_OK);
  PRINT_DIFF(INK_HTTP_STATUS_CREATED);


  PRINT_DIFF(INK_HTTP_STATUS_ACCEPTED);
  PRINT_DIFF(INK_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION);
  PRINT_DIFF(INK_HTTP_STATUS_NO_CONTENT);
  PRINT_DIFF(INK_HTTP_STATUS_RESET_CONTENT);
  PRINT_DIFF(INK_HTTP_STATUS_PARTIAL_CONTENT);

  PRINT_DIFF(INK_HTTP_STATUS_MULTIPLE_CHOICES);
  PRINT_DIFF(INK_HTTP_STATUS_MOVED_PERMANENTLY);
  PRINT_DIFF(INK_HTTP_STATUS_MOVED_TEMPORARILY);
  PRINT_DIFF(INK_HTTP_STATUS_SEE_OTHER);
  PRINT_DIFF(INK_HTTP_STATUS_NOT_MODIFIED);
  PRINT_DIFF(INK_HTTP_STATUS_USE_PROXY);
  PRINT_DIFF(INK_HTTP_STATUS_BAD_REQUEST);
  PRINT_DIFF(INK_HTTP_STATUS_UNAUTHORIZED);
  PRINT_DIFF(INK_HTTP_STATUS_FORBIDDEN);
  PRINT_DIFF(INK_HTTP_STATUS_NOT_FOUND);
  PRINT_DIFF(INK_HTTP_STATUS_METHOD_NOT_ALLOWED);
  PRINT_DIFF(INK_HTTP_STATUS_NOT_ACCEPTABLE);
  PRINT_DIFF(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  PRINT_DIFF(INK_HTTP_STATUS_REQUEST_TIMEOUT);
  PRINT_DIFF(INK_HTTP_STATUS_CONFLICT);
  PRINT_DIFF(INK_HTTP_STATUS_GONE);
  PRINT_DIFF(INK_HTTP_STATUS_PRECONDITION_FAILED);
  PRINT_DIFF(INK_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE);
  PRINT_DIFF(INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
  PRINT_DIFF(INK_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
  PRINT_DIFF(INK_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  PRINT_DIFF(INK_HTTP_STATUS_NOT_IMPLEMENTED);
  PRINT_DIFF(INK_HTTP_STATUS_BAD_GATEWAY);
  PRINT_DIFF(INK_HTTP_STATUS_GATEWAY_TIMEOUT);
  PRINT_DIFF(INK_HTTP_STATUS_HTTPVER_NOT_SUPPORTED);

  PRINT_DIFF(INK_HTTP_READ_REQUEST_HDR_HOOK);
  PRINT_DIFF(INK_HTTP_OS_DNS_HOOK);
  PRINT_DIFF(INK_HTTP_SEND_REQUEST_HDR_HOOK);
  PRINT_DIFF(INK_HTTP_READ_RESPONSE_HDR_HOOK);
  PRINT_DIFF(INK_HTTP_SEND_RESPONSE_HDR_HOOK);
  PRINT_DIFF(INK_HTTP_REQUEST_TRANSFORM_HOOK);
  PRINT_DIFF(INK_HTTP_RESPONSE_TRANSFORM_HOOK);
  PRINT_DIFF(INK_HTTP_SELECT_ALT_HOOK);
  PRINT_DIFF(INK_HTTP_TXN_START_HOOK);
  PRINT_DIFF(INK_HTTP_TXN_CLOSE_HOOK);
  PRINT_DIFF(INK_HTTP_SSN_START_HOOK);
  PRINT_DIFF(INK_HTTP_SSN_CLOSE_HOOK);
  PRINT_DIFF(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK);
  PRINT_DIFF(INK_HTTP_LAST_HOOK);

  PRINT_DIFF(INK_EVENT_NONE);
  PRINT_DIFF(INK_EVENT_IMMEDIATE);
  PRINT_DIFF(INK_EVENT_TIMEOUT);
  PRINT_DIFF(INK_EVENT_ERROR);

  PRINT_DIFF(INK_EVENT_CONTINUE);
  PRINT_DIFF(INK_EVENT_VCONN_READ_READY);
  PRINT_DIFF(INK_EVENT_VCONN_WRITE_READY);
  PRINT_DIFF(INK_EVENT_VCONN_READ_COMPLETE);
  PRINT_DIFF(INK_EVENT_VCONN_WRITE_COMPLETE);
  PRINT_DIFF(INK_EVENT_VCONN_EOS);

  PRINT_DIFF(INK_EVENT_NET_CONNECT);
  PRINT_DIFF(INK_EVENT_NET_CONNECT_FAILED);
  PRINT_DIFF(INK_EVENT_NET_ACCEPT);
  PRINT_DIFF(INK_EVENT_NET_ACCEPT_FAILED);

  PRINT_DIFF(INK_EVENT_HOST_LOOKUP);

  PRINT_DIFF(INK_EVENT_CACHE_OPEN_READ);
  PRINT_DIFF(INK_EVENT_CACHE_OPEN_READ_FAILED);
  PRINT_DIFF(INK_EVENT_CACHE_OPEN_WRITE);
  PRINT_DIFF(INK_EVENT_CACHE_OPEN_WRITE_FAILED);
  PRINT_DIFF(INK_EVENT_CACHE_REMOVE);
  PRINT_DIFF(INK_EVENT_CACHE_REMOVE_FAILED);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN_FAILED);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN_OBJECT);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN_OPERATION_BLOCKED);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN_OPERATION_FAILED);
  PRINT_DIFF(INK_EVENT_CACHE_SCAN_DONE);

  PRINT_DIFF(INK_EVENT_HTTP_CONTINUE);
  PRINT_DIFF(INK_EVENT_HTTP_ERROR);
  PRINT_DIFF(INK_EVENT_HTTP_READ_REQUEST_HDR);
  PRINT_DIFF(INK_EVENT_HTTP_OS_DNS);
  PRINT_DIFF(INK_EVENT_HTTP_SEND_REQUEST_HDR);
  PRINT_DIFF(INK_EVENT_HTTP_READ_CACHE_HDR);
  PRINT_DIFF(INK_EVENT_HTTP_READ_RESPONSE_HDR);
  PRINT_DIFF(INK_EVENT_HTTP_SEND_RESPONSE_HDR);
  PRINT_DIFF(INK_EVENT_HTTP_REQUEST_TRANSFORM);
  PRINT_DIFF(INK_EVENT_HTTP_RESPONSE_TRANSFORM);
  PRINT_DIFF(INK_EVENT_HTTP_SELECT_ALT);
  PRINT_DIFF(INK_EVENT_HTTP_TXN_START);
  PRINT_DIFF(INK_EVENT_HTTP_TXN_CLOSE);
  PRINT_DIFF(INK_EVENT_HTTP_SSN_START);
  PRINT_DIFF(INK_EVENT_HTTP_SSN_CLOSE);
  PRINT_DIFF(INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE);

  PRINT_DIFF(INK_EVENT_MGMT_UPDATE);

  PRINT_DIFF(INK_CACHE_LOOKUP_MISS);
  PRINT_DIFF(INK_CACHE_LOOKUP_HIT_STALE);
  PRINT_DIFF(INK_CACHE_LOOKUP_HIT_FRESH);

  PRINT_DIFF(INK_CACHE_DATA_TYPE_NONE);
  PRINT_DIFF(INK_CACHE_DATA_TYPE_HTTP);
  PRINT_DIFF(INK_CACHE_DATA_TYPE_OTHER);

  PRINT_DIFF(INK_CACHE_ERROR_NO_DOC);
  PRINT_DIFF(INK_CACHE_ERROR_DOC_BUSY);
  PRINT_DIFF(INK_CACHE_ERROR_NOT_READY);

  PRINT_DIFF(INK_CACHE_SCAN_RESULT_DONE);
  PRINT_DIFF(INK_CACHE_SCAN_RESULT_CONTINUE);
  PRINT_DIFF(INK_CACHE_SCAN_RESULT_DELETE);
  PRINT_DIFF(INK_CACHE_SCAN_RESULT_DELETE_ALL_ALTERNATES);
  PRINT_DIFF(INK_CACHE_SCAN_RESULT_UPDATE);
  PRINT_DIFF(INK_CACHE_SCAN_RESULT_RETRY);

  PRINT_DIFF(INK_DATA_ALLOCATE);
  PRINT_DIFF(INK_DATA_MALLOCED);
  PRINT_DIFF(INK_DATA_CONSTANT);

  PRINT_DIFF(INK_VC_CLOSE_ABORT);
  PRINT_DIFF(INK_VC_CLOSE_NORMAL);

  PRINT_DIFF(INK_SDK_VERSION_2_0);

  PRINT_DIFF(INK_ERROR);
  PRINT_DIFF(INK_SUCCESS);


  if (test_passed) {
    *pstatus = REGRESSION_TEST_PASSED;
  } else {
    *pstatus = REGRESSION_TEST_FAILED;
  }

}

//////////////////////////////////////////////
//       SDK_API_INKHttpSsn
//
// Unit Test for API: INKHttpSsnHookAdd
//                    INKHttpSsnReenable
//                    INKHttpTxnHookAdd
//                    INKHttpTxnErrorBodySet
//                    INKHttpTxnParentProxyGet
//                    INKHttpTxnParentProxySet
//////////////////////////////////////////////


typedef struct
{
  RegressionTest *test;
  int *pstatus;
  SocketServer *os;
  ClientTxn *browser;
  INKHttpSsn ssnp;
  int test_passed_ssn_hook_add;
  int test_passed_ssn_reenable;
  int test_passed_txn_ssn_get;
  int test_passed_txn_hook_add;
  int test_passed_txn_error_body_set;
  bool test_passed_Parent_Proxy;
  int magic;
} ContData;

static int
checkHttpTxnParentProxy(ContData * data, INKHttpTxn txnp)
{

  const char *hostname = "txnpp.example.com";
  int port = 10180;
  char *hostnameget = NULL;
  int portget = 0;

  if (INKHttpTxnParentProxySet(txnp, (char*)hostname, port) != INK_SUCCESS) {
    SDK_RPRINT(data->test, "INKHttpTxnParentProxySet", "TestCase1", TC_FAIL,
               "INKHttpTxnParentProxySet doesn't return INK_SUCCESS");
    SDK_RPRINT(data->test, "INKHttpTxnParentProxyGet", "TestCase1", TC_FAIL,
               "INKHttpTxnParentProxySet doesn't return INK_SUCCESS");
    return INK_EVENT_CONTINUE;
  }

  if (INKHttpTxnParentProxyGet(txnp, &hostnameget, &portget) != INK_SUCCESS) {
    SDK_RPRINT(data->test, "INKHttpTxnParentProxySet", "TestCase1", TC_FAIL,
               "INKHttpTxnParentProxyGet doesn't return INK_SUCCESS");
    SDK_RPRINT(data->test, "INKHttpTxnParentProxyGet", "TestCase1", TC_FAIL,
               "INKHttpTxnParentProxyGet doesn't return INK_SUCCESS");
    return INK_EVENT_CONTINUE;
  }

  if ((strcmp(hostname, hostnameget) == 0) && (port == portget)) {
    SDK_RPRINT(data->test, "INKHttpTxnParentProxySet", "TestCase1", TC_PASS, "ok");
    SDK_RPRINT(data->test, "INKHttpTxnParentProxyGet", "TestCase1", TC_PASS, "ok");
    data->test_passed_Parent_Proxy = true;
  } else {
    SDK_RPRINT(data->test, "INKHttpTxnParentProxySet", "TestCase1", TC_FAIL, "Value's Mismatch");
    SDK_RPRINT(data->test, "INKHttpTxnParentProxyGet", "TestCase1", TC_FAIL, "Value's Mismatch");
  }

  return INK_EVENT_CONTINUE;
}


static int
ssn_handler(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = NULL;
  ContData *data = NULL;
  data = (ContData *) INKContDataGet(contp);
  if ((data == INK_ERROR_PTR) || (data == NULL)) {
    switch (event) {
    case INK_EVENT_HTTP_SSN_START:
      INKHttpSsnReenable((INKHttpSsn) edata, INK_EVENT_HTTP_CONTINUE);
      break;
    case INK_EVENT_IMMEDIATE:
    case INK_EVENT_TIMEOUT:
      break;
    case INK_EVENT_HTTP_TXN_START:
    default:
      INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
      break;
    }
    return 0;
  }

  switch (event) {
  case INK_EVENT_HTTP_SSN_START:
    data->ssnp = (INKHttpSsn) edata;
    if (INKHttpSsnHookAdd(data->ssnp, INK_HTTP_TXN_START_HOOK, contp) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpSsnHookAdd", "TestCase1", TC_FAIL, "INKHttpSsnHookAdd doesn't return INK_SUCCESS");
      data->test_passed_ssn_hook_add--;
    }
    if (INKHttpSsnReenable(data->ssnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpSsnReenable", "TestCase1", TC_FAIL,
                 "INKHttpSsnReenable doesn't return INK_SUCCESS");
      data->test_passed_ssn_reenable--;
    }
    break;

  case INK_EVENT_HTTP_TXN_START:
    SDK_RPRINT(data->test, "INKHttpSsnReenable", "TestCase", TC_PASS, "ok");
    data->test_passed_ssn_reenable++;
    {
      txnp = (INKHttpTxn) edata;
      INKHttpSsn ssnp = INKHttpTxnSsnGet(txnp);
      if (ssnp != data->ssnp) {
        SDK_RPRINT(data->test, "INKHttpSsnHookAdd", "TestCase", TC_FAIL, "Value's mismatch");
        data->test_passed_ssn_hook_add--;
        SDK_RPRINT(data->test, "INKHttpTxnSsnGet", "TestCase", TC_FAIL, "Session doesn't match");
        data->test_passed_txn_ssn_get--;
      } else {
        SDK_RPRINT(data->test, "INKHttpSsnHookAdd", "TestCase1", TC_PASS, "ok");
        data->test_passed_ssn_hook_add++;
        SDK_RPRINT(data->test, "INKHttpTxnSsnGet", "TestCase1", TC_PASS, "ok");
        data->test_passed_txn_ssn_get++;
      }
      if (INKHttpTxnHookAdd(txnp, INK_HTTP_OS_DNS_HOOK, contp) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpTxnHookAdd", "TestCase1", TC_FAIL,
                   "INKHttpTxnHookAdd doesn't return INK_SUCCESS");
        data->test_passed_txn_hook_add--;
      }
      if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpSsn", "TestCase1", TC_FAIL, "INKHttpTxnReenable doesn't return INK_SUCCESS");
      }
    }
    break;

  case INK_EVENT_HTTP_OS_DNS:
    SDK_RPRINT(data->test, "INKHttpTxnHookAdd", "TestCase1", TC_PASS, "ok");
    data->test_passed_txn_hook_add++;
    txnp = (INKHttpTxn) edata;

    if (INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpTxnHookAdd", "TestCase1", TC_FAIL, "INKHttpTxnHookAdd doesn't return INK_SUCCESS");
      data->test_passed_txn_hook_add--;
    }

    checkHttpTxnParentProxy(data, txnp);

    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpSsn", "TestCase1", TC_FAIL, "INKHttpTxnReenable doesn't return INK_SUCCESS");
    }
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    SDK_RPRINT(data->test, "INKHttpTxnHookAdd", "TestCase2", TC_PASS, "ok");
    data->test_passed_txn_hook_add++;
    txnp = (INKHttpTxn) edata;
    if (1) {
      char *temp = INKstrdup(ERROR_BODY);
      if (INKHttpTxnErrorBodySet(txnp, temp, strlen(temp), NULL) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpTxnErrorBodySet", "TestCase1", TC_FAIL,
                   "INKHttpTxnErrorBodySet doesn't return INK_SUCCESS");
        data->test_passed_txn_error_body_set--;
      }
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpSsn", "TestCase1", TC_FAIL, "INKHttpTxnReenable doesn't return INK_SUCCESS");
    }
    break;

  case INK_EVENT_IMMEDIATE:
  case INK_EVENT_TIMEOUT:
    /* Browser still waiting the response ? */
    if (data->browser->status == REQUEST_INPROGRESS) {
      INKContSchedule(contp, 25);
    }
    /* Browser got the response. test is over. clean up */
    else {
      /* Check if browser response body is the one we expected */
      char *temp = data->browser->response;
      temp = strstr(temp, "\r\n\r\n");
      if (temp != NULL) {
        temp += strlen("\r\n\r\n");
        if ((temp[0] == '\0') || (strncmp(temp, "\r\n\r\n", 4) == 0)) {
          SDK_RPRINT(data->test, "INKHttpTxnErrorBodySet", "TestCase1", TC_FAIL, "No Error Body found");
          data->test_passed_txn_error_body_set--;
        }
        if (strncmp(temp, ERROR_BODY, strlen(ERROR_BODY)) == 0) {
          SDK_RPRINT(data->test, "INKHttpTxnErrorBodySet", "TestCase1", TC_PASS, "ok");
          data->test_passed_txn_error_body_set++;
        }
      } else {
        SDK_RPRINT(data->test, "INKHttpTxnErrorBodySet", "TestCase1", TC_FAIL,
                   "strstr returns NULL. Didn't find end of headers.");
        data->test_passed_txn_error_body_set--;
      }

      /* Note: response is available using test->browser->response pointer */
      if ((data->browser->status == REQUEST_SUCCESS) &&
          (data->test_passed_ssn_hook_add == 1) &&
          (data->test_passed_ssn_reenable == 1) &&
          (data->test_passed_txn_ssn_get == 1) &&
          (data->test_passed_txn_hook_add == 2) && (data->test_passed_txn_error_body_set == 1)
          && (data->test_passed_Parent_Proxy == true)
        ) {
        *(data->pstatus) = REGRESSION_TEST_PASSED;
      } else {
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }

      // transaction is over. clean up.
      synclient_txn_delete(data->browser);
      /* Don't need it as didn't initialize the server
         synserver_delete(data->os);
       */
      data->magic = MAGIC_DEAD;
      INKfree(data);
      INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(data->test, "INKHttpSsn", "TestCase1", TC_FAIL, "Unexpected event %d", event);
    break;
  }
  return 0;
}


EXCLUSIVE_REGRESSION_TEST(SDK_API_HttpSsn) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKCont cont = INKContCreate(ssn_handler, INKMutexCreate());
  if ((cont == NULL) || (cont == INK_ERROR_PTR)) {
    SDK_RPRINT(test, "INKHttSsn", "TestCase1", TC_FAIL, "Unable to create Continuation.");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  ContData *socktest = (ContData *) INKmalloc(sizeof(ContData));
  socktest->test = test;
  socktest->pstatus = pstatus;
  socktest->test_passed_ssn_hook_add = 0;
  socktest->test_passed_ssn_reenable = 0;
  socktest->test_passed_txn_ssn_get = 0;
  socktest->test_passed_txn_hook_add = 0;
  socktest->test_passed_txn_error_body_set = 0;
  socktest->test_passed_Parent_Proxy = false;
  socktest->magic = MAGIC_ALIVE;
  INKContDataSet(cont, socktest);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, cont);

  /* Create a client transaction */
  socktest->browser = synclient_txn_create();
  char *request = generate_request(3);  // response is expected to be error case
  synclient_txn_send_request(socktest->browser, request);
  INKfree(request);

  /* Wait until transaction is done */
  if (socktest->browser->status == REQUEST_INPROGRESS) {
    INKContSchedule(cont, 25);
  }

  return;
}

/////////////////////////////////////////////////////
//       SDK_API_INKHttpTxnCache
//
// Unit Test for API: INKHttpTxnCachedReqGet
//                    INKHttpTxnCachedRespGet
//                    INKHttpTxnCacheLookupStatusGet
/////////////////////////////////////////////////////

typedef struct
{
  RegressionTest *test;
  int *pstatus;
  SocketServer *os;
  ClientTxn *browser1;
  ClientTxn *browser2;
  char *request;
  bool test_passed_txn_cached_req_get;
  bool test_passed_txn_cached_resp_get;
  bool test_passed_txn_cache_lookup_status;
  bool first_time;
  int magic;
} CacheTestData;

static int
cache_hook_handler(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = NULL;
  CacheTestData *data = NULL;
  data = (CacheTestData *) INKContDataGet(contp);
  if ((data == INK_ERROR_PTR) || (data == NULL)) {
    switch (event) {
    case INK_EVENT_IMMEDIATE:
    case INK_EVENT_TIMEOUT:
      break;
    case INK_EVENT_HTTP_READ_CACHE_HDR:
    default:
      INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
      break;
    }
    return 0;
  }

  switch (event) {
  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    {
      int lookup_status;
      if (data->first_time == true) {
        txnp = (INKHttpTxn) edata;
        if (INKHttpTxnCacheLookupStatusGet(txnp, &lookup_status) != INK_SUCCESS) {
          SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase1", TC_FAIL,
                     "INKHttpTxnCacheLookupStatus doesn't return INK_SUCCESS");
        } else {
          if (lookup_status == INK_CACHE_LOOKUP_MISS) {
            SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase1", TC_PASS, "ok");
            data->test_passed_txn_cache_lookup_status = true;
          } else {
            SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase1", TC_FAIL,
                       "Incorrect Value returned by INKHttpTxnCacheLookupStatusGet");
          }
        }
      } else {
        txnp = (INKHttpTxn) edata;
        if (INKHttpTxnCacheLookupStatusGet(txnp, &lookup_status) != INK_SUCCESS) {
          SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase2", TC_FAIL,
                     "INKHttpTxnCacheLookupStatus doesn't return INK_SUCCESS");
          data->test_passed_txn_cache_lookup_status = false;
        } else {
          if (lookup_status == INK_CACHE_LOOKUP_HIT_FRESH) {
            SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase2", TC_PASS, "ok");
          } else {
            SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "TestCase2", TC_FAIL,
                       "Incorrect Value returned by INKHttpTxnCacheLookupStatusGet");
            data->test_passed_txn_cache_lookup_status = false;
          }
        }
      }
      if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpTxnCacheLookupStatusGet", "", TC_FAIL, "Unable to reenable the transaction");
      }
    }
    break;
  case INK_EVENT_HTTP_READ_CACHE_HDR:
    {
      INKMBuffer reqbuf;
      INKMBuffer respbuf;

      INKMLoc reqhdr;
      INKMLoc resphdr;

      txnp = (INKHttpTxn) edata;

      if (INKHttpTxnCachedReqGet(txnp, &reqbuf, &reqhdr) == 0) {
        SDK_RPRINT(data->test, "INKHttpTxnCachedReqGet", "TestCase1", TC_FAIL, "INKHttpTxnCachedReqGet returns 0");
      } else {
        if ((reqbuf == (((HttpSM *) txnp)->t_state.cache_req_hdr_heap_handle)) &&
            (reqhdr == ((((HttpSM *) txnp)->t_state.cache_info.object_read->request_get())->m_http))
          ) {
          SDK_RPRINT(data->test, "INKHttpTxnCachedReqGet", "TestCase1", TC_PASS, "ok");
          data->test_passed_txn_cached_req_get = true;
        } else {
          SDK_RPRINT(data->test, "INKHttpTxnCachedReqGet", "TestCase1", TC_FAIL, "Value's Mismatch");
        }
      }

      if (INKHttpTxnCachedRespGet(txnp, &respbuf, &resphdr) == 0) {
        SDK_RPRINT(data->test, "INKHttpTxnCachedRespGet", "TestCase1", TC_FAIL, "INKHttpTxnCachedRespGet returns 0");
      } else {
        if ((respbuf == (((HttpSM *) txnp)->t_state.cache_resp_hdr_heap_handle)) &&
            (resphdr == ((((HttpSM *) txnp)->t_state.cache_info.object_read->response_get())->m_http))
          ) {
          SDK_RPRINT(data->test, "INKHttpTxnCachedRespGet", "TestCase1", TC_PASS, "ok");
          data->test_passed_txn_cached_resp_get = true;
        } else {
          SDK_RPRINT(data->test, "INKHttpTxnCachedRespGet", "TestCase1", TC_FAIL, "Value's Mismatch");
        }
      }

      if ((INKHandleMLocRelease(reqbuf, INK_NULL_MLOC, reqhdr) != INK_SUCCESS) ||
          (INKHandleMLocRelease(respbuf, INK_NULL_MLOC, resphdr) != INK_SUCCESS)
        ) {
        SDK_RPRINT(data->test, "INKHttpTxnCache", "", TC_FAIL, "Unable to release handle to headers.");
      }

      if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpTxnCache", "", TC_FAIL, "Unable to reenable the transaction.");
      }
    }

    break;

  case INK_EVENT_IMMEDIATE:
  case INK_EVENT_TIMEOUT:
    /* Browser still waiting the response ? */
    if (data->first_time == true) {
      if (data->browser1->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
    } else {
      if (data->browser2->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
    }

    /* Browser got the response. test is over. clean up */
    {
      /* If this is the first time, then the response is in cache and we should make */
      /* another request to get cache hit */
      if (data->first_time == true) {
        data->first_time = false;
        /* Kill the origin server */
        synserver_delete(data->os);
        /* Send another similar client request */
        synclient_txn_send_request(data->browser2, data->request);
        INKfree(data->request);
        INKContSchedule(contp, 25);
        return 0;
      }

      /* Note: response is available using test->browser->response pointer */
      if ((data->browser1->status == REQUEST_SUCCESS) &&
          (data->browser2->status == REQUEST_SUCCESS) &&
          (data->test_passed_txn_cached_req_get == true) &&
          (data->test_passed_txn_cached_resp_get == true) && (data->test_passed_txn_cache_lookup_status == true)
        ) {
        *(data->pstatus) = REGRESSION_TEST_PASSED;
      } else {
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }

      // transaction is over. clean up.
      synclient_txn_delete(data->browser1);
      synclient_txn_delete(data->browser2);

      data->magic = MAGIC_DEAD;
      INKfree(data);
      INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(data->test, "INKHttpTxnCache", "TestCase1", TC_FAIL, "Unexpected event %d", event);
    break;
  }
  return 0;
}


EXCLUSIVE_REGRESSION_TEST(SDK_API_HttpTxnCache) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKCont cont = INKContCreate(cache_hook_handler, INKMutexCreate());
  if ((cont == NULL) || (cont == INK_ERROR_PTR)) {
    SDK_RPRINT(test, "INKHttSsn", "TestCase1", TC_FAIL, "Unable to create Continuation.");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  CacheTestData *socktest = (CacheTestData *) INKmalloc(sizeof(CacheTestData));
  socktest->test = test;
  socktest->pstatus = pstatus;
  socktest->test_passed_txn_cached_req_get = false;
  socktest->test_passed_txn_cached_resp_get = false;
  socktest->first_time = true;
  socktest->magic = MAGIC_ALIVE;
  INKContDataSet(cont, socktest);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  INKHttpHookAdd(INK_HTTP_READ_CACHE_HDR_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);

  /* Create a new synthetic server */
  socktest->os = synserver_create(SYNSERVER_LISTEN_PORT);
  synserver_start(socktest->os);

  /* Create a client transaction */
  socktest->browser1 = synclient_txn_create();
  socktest->browser2 = synclient_txn_create();
  socktest->request = generate_request(2);
  synclient_txn_send_request(socktest->browser1, socktest->request);

  /* Wait until transaction is done */
  INKContSchedule(cont, 25);

  return;
}

///////////////////////////////////////////////////////
//       SDK_API_INKHttpTxnTransform
//
// Unit Test for API: INKHttpTxnTransformRespGet
//                    INKHttpTxnTransformedRespCache
//                    INKHttpTxnUntransformedRespCache
///////////////////////////////////////////////////////

/** Append Transform Data Structure **/
typedef struct
{
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
  int append_needed;
} MyTransformData;
/** Append Transform Data Structure Ends **/

typedef struct
{
  RegressionTest *test;
  int *pstatus;
  SocketServer *os;
  ClientTxn *browser1;
  ClientTxn *browser2;
  ClientTxn *browser3;
  ClientTxn *browser4;
  char *request1;
  char *request2;
  bool test_passed_txn_transform_resp_get;
  bool test_passed_txn_transformed_resp_cache;
  bool test_passed_txn_untransformed_resp_cache;
  bool test_passed_transform_create;
  int req_no;
  MyTransformData *transformData;
  int magic;
} TransformTestData;

/**** Append Transform Code (Tailored to needs)****/

static INKIOBuffer append_buffer;
static INKIOBufferReader append_buffer_reader;
static int append_buffer_length;

static MyTransformData *
my_data_alloc()
{
  MyTransformData *data;

  data = (MyTransformData *) INKmalloc(sizeof(MyTransformData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->append_needed = 1;

  return data;
}

static void
my_data_destroy(MyTransformData * data)
{
  if (data) {
    if (data->output_buffer) {
      INKIOBufferDestroy(data->output_buffer);
    }
    INKfree(data);
  }
}

static void
handle_transform(INKCont contp)
{
  INKVConn output_conn;
  INKVIO write_vio;
  TransformTestData *contData;
  MyTransformData *data;
  int towrite;
  int avail;

  /* Get the output connection where we'll write data to. */
  output_conn = INKTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = INKVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */
  contData = (TransformTestData *) INKContDataGet(contp);
  data = contData->transformData;
  if (!data) {
    towrite = INKVIONBytesGet(write_vio);
    if (towrite != INT_MAX) {
      towrite += append_buffer_length;
    }
    contData->transformData = my_data_alloc();
    data = contData->transformData;
    data->output_buffer = INKIOBufferCreate();
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);
    data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader, towrite);
    // Don't need this as the structure is encapsulated in another structure
    // which is set to be Continuation's Data.
    // INKContDataSet (contp, data);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this simplistic
     transformation that means we're done. In a more complex
     transformation we might have to finish writing the transformed
     data to our output connection. */
  if (!INKVIOBufferGet(write_vio)) {
    if (data->append_needed) {
      data->append_needed = 0;
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(write_vio) + append_buffer_length);
    INKVIOReenable(data->output_vio);
    return;
  }

  /* Determine how much data we have left to read. For this append
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = INKVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = INKIOBufferReaderAvail(INKVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      INKIOBufferReaderConsume(INKVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      INKVIONDoneSet(write_vio, INKVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (INKVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
         connection by reenabling the output VIO. This will wakeup
         the output connection and allow it to consume data from the
         output buffer. */
      INKVIOReenable(data->output_vio);

      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    if (data->append_needed) {
      data->append_needed = 0;
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    /* If there is no data left to read, then we modify the output
       VIO to reflect how much data the output connection should
       expect. This allows the output connection to know when it
       is done reading. We then reenable the output connection so
       that it can consume the data we just gave it. */
    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(write_vio) + append_buffer_length);
    INKVIOReenable(data->output_vio);

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
}

static int
transformtest_transform(INKCont contp, INKEvent event, void *edata)
{
  NOWARN_UNUSED(edata);
  TransformTestData *contData = (TransformTestData *) INKContDataGet(contp);
  if (contData->test_passed_transform_create == false) {
    contData->test_passed_transform_create = true;
    SDK_RPRINT(contData->test, "INKTransformCreate", "TestCase1", TC_PASS, "ok");
  }
  /* Check to see if the transformation has been closed by a call to
     INKVConnClose. */
  if (INKVConnClosedGet(contp)) {
    my_data_destroy(contData->transformData);
    contData->transformData = NULL;
    INKContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case INK_EVENT_ERROR:
      {
        INKVIO write_vio;

        /* Get the write VIO for the write operation that was
           performed on ourself. This VIO contains the continuation of
           our parent transformation. */
        write_vio = INKVConnWriteVIOGet(contp);

        /* Call back the write VIO continuation to let it know that we
           have completed the write operation. */
        INKContCall(INKVIOContGet(write_vio), INK_EVENT_ERROR, write_vio);
      }
      break;
    case INK_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */
      INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1);
      break;
    case INK_EVENT_VCONN_WRITE_READY:
    default:
      /* If we get a WRITE_READY event or any other type of
         event (sent, perhaps, because we were reenabled) then
         we'll attempt to transform more data. */
      handle_transform(contp);
      break;
    }
  }

  return 0;
}

static int
transformable(INKHttpTxn txnp, TransformTestData * data)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;

  if (INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) == 0) {
    SDK_RPRINT(data->test, "INKHttpTxnTransform", "", TC_FAIL, "[transformable]: INKHttpTxnServerRespGet return 0");
  }

  /*
   *  We are only interested in "200 OK" responses.
   */

  if (INK_HTTP_STATUS_OK == INKHttpHdrStatusGet(bufp, hdr_loc)) {
    return 1;
  }
// XXX - Can't return INK_ERROR because that is a different type
// -bcall 7/24/07
//     if (resp_status == INK_ERROR) {
//      SDK_RPRINT(data->test,"INKHttpTxnTransform","",TC_FAIL,"[transformable]: INKHttpHdrStatusGet returns INK_ERROR");
//     }

  return 0;                     /* not a 200 */
}

static void
transform_add(INKHttpTxn txnp, TransformTestData * data)
{
  INKVConn connp;

  connp = INKTransformCreate(transformtest_transform, txnp);
  INKContDataSet(connp, data);
  if ((connp == NULL) || (connp == INK_ERROR_PTR)) {
    SDK_RPRINT(data->test, "INKHttpTxnTransform", "", TC_FAIL, "Unable to create Transformation.");
    return;
  }

  if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, connp) != INK_SUCCESS) {
    SDK_RPRINT(data->test, "INKHttpTxnTransform", "", TC_FAIL, "Unable to add Transformation to the transform hook.");
  }
  return;
}

static int
load(const char *append_string)
{
  INKIOBufferBlock blk;
  char *p;
  int64 avail;

  append_buffer = INKIOBufferCreate();
  append_buffer_reader = INKIOBufferReaderAlloc(append_buffer);

  blk = INKIOBufferStart(append_buffer);
  p = INKIOBufferBlockWriteStart(blk, &avail);

  ink_strncpy(p, append_string, avail);
  if (append_string != NULL) {
    INKIOBufferProduce(append_buffer, strlen(append_string));
  }

  append_buffer_length = INKIOBufferReaderAvail(append_buffer_reader);

  return 1;
}

/**** Append Transform Code Ends ****/

static int
transform_hook_handler(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = NULL;
  TransformTestData *data = NULL;
  data = (TransformTestData *) INKContDataGet(contp);
  if ((data == INK_ERROR_PTR) || (data == NULL)) {
    switch (event) {
    case INK_EVENT_IMMEDIATE:
    case INK_EVENT_TIMEOUT:
      break;
    case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    default:
      INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
      break;
    }
    return 0;
  }


  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    txnp = (INKHttpTxn) edata;
    /* Setup hooks for Transformation */
    if (transformable(txnp, data)) {
      transform_add(txnp, data);
    }
    /* Call TransformedRespCache or UntransformedRespCache depending on request */
    {
      INKMBuffer bufp;
      INKMLoc hdr;
      INKMLoc field;

      if (INKHttpTxnClientReqGet(txnp, &bufp, &hdr) == 0) {
        SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL, "INKHttpTxnClientReqGet returns 0");
      } else {
        field = INKMimeHdrFieldFind(bufp, hdr, "Request", -1);
        if ((field == NULL) || (field == INK_ERROR_PTR)) {
          SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL, "Didn't find field request");
        } else {
          int reqid;
          if (INKMimeHdrFieldValueIntGet(bufp, hdr, field, 0, &reqid) != INK_SUCCESS) {
            SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL, "Error in getting field Value");
          } else {
            if (reqid == 1) {
              if ((INKHttpTxnTransformedRespCache(txnp, 0) != INK_SUCCESS) ||
                  (INKHttpTxnUntransformedRespCache(txnp, 1) != INK_SUCCESS)
                ) {
                SDK_RPRINT(data->test, "INKHttpTxnTransformedRespCache", "TestCase", TC_FAIL,
                           "INKHttpTxnTransformedRespCache or INKHttpTxnUntransformedRespCache doesn't return INK_SUCCESS.reqid=%d",
                           reqid);
              }
            }
            if (reqid == 2) {
              if ((INKHttpTxnTransformedRespCache(txnp, 1) != INK_SUCCESS) ||
                  (INKHttpTxnUntransformedRespCache(txnp, 0) != INK_SUCCESS)
                ) {
                SDK_RPRINT(data->test, "INKHttpTxnTransformedRespCache", "TestCase", TC_FAIL,
                           "INKHttpTxnTransformedRespCache or INKHttpTxnUntransformedRespCache doesn't return INK_SUCCESS.reqid=%d",
                           reqid);
              }
            }
          }
          if (INKHandleMLocRelease(bufp, hdr, field) != INK_SUCCESS) {
            SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL,
                       "Unable to release handle to field in Client request");
          }
        }
        if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr) != INK_SUCCESS) {
          SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL,
                     "Unable to release handle to Client request");
        }
      }
    }

    /* Add the transaction hook to SEND_RESPONSE_HDR_HOOK */
    if (INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpTxnTransform", "", TC_FAIL,
                 "Cannot add transaction hook to SEND_RESPONSE_HDR_HOOK");
    }
    /* Reenable the transaction */
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpTxnTransform", "", TC_FAIL,
                 "Reenabling the transaction doesn't return INK_SUCCESS");
    }
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    {
      INKMBuffer bufp;
      INKMLoc hdr;
      txnp = (INKHttpTxn) edata;
      if (INKHttpTxnTransformRespGet(txnp, &bufp, &hdr) == 0) {
        SDK_RPRINT(data->test, "INKHttpTxnTransformRespGet", "TestCase", TC_FAIL,
                   "INKHttpTxnTransformRespGet returns 0");
        data->test_passed_txn_transform_resp_get = false;
      } else {
        if ((bufp == &(((HttpSM *) txnp)->t_state.hdr_info.transform_response)) &&
            (hdr == (&(((HttpSM *) txnp)->t_state.hdr_info.transform_response))->m_http)
          ) {
          SDK_RPRINT(data->test, "INKHttpTxnTransformRespGet", "TestCase", TC_PASS, "ok");
        } else {
          SDK_RPRINT(data->test, "INKHttpTxnTransformRespGet", "TestCase", TC_FAIL, "Value's Mismatch");
          data->test_passed_txn_transform_resp_get = false;
        }
        if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr) != INK_SUCCESS) {
          SDK_RPRINT(data->test, "INKHttpTxnTransformRespGet", "TestCase", TC_FAIL,
                     "Unable to release handle to Transform header handle");
        }

      }
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      SDK_RPRINT(data->test, "INKHttpTxnTransformRespGet", "", TC_FAIL,
                 "Reenabling the transaction doesn't return INK_SUCCESS");
    }
    break;

  case INK_EVENT_IMMEDIATE:
  case INK_EVENT_TIMEOUT:

    switch (data->req_no) {
    case 1:
      if (data->browser1->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
      data->req_no++;
      Debug(UTDBG_TAG "_transform", "Running Browser 2");
      synclient_txn_send_request(data->browser2, data->request2);
      INKContSchedule(contp, 25);
      return 0;
    case 2:
      if (data->browser2->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
      data->req_no++;
      synserver_delete(data->os);
      Debug(UTDBG_TAG "_transform", "Running Browser 3");
      synclient_txn_send_request(data->browser3, data->request1);
      INKContSchedule(contp, 25);
      return 0;
    case 3:
      if (data->browser3->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
      data->req_no++;
      Debug(UTDBG_TAG "_transform", "Running Browser 4");
      synclient_txn_send_request(data->browser4, data->request2);
      INKContSchedule(contp, 25);
      return 0;
    case 4:
      if (data->browser4->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
      data->req_no++;
      INKfree(data->request1);
      INKfree(data->request2);
      // for squid log: if this is the last (or only) test in your
      // regression run you will not see any log entries in squid
      // (because logging is buffered and not flushed before
      // termination when running regressions)
      // sleep(10);
      break;
    default:
      SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase", TC_FAIL, "Something terribly wrong with the test");
      exit(0);

    }
    /* Browser got the response. test is over */
    {
      /* Check if we got the response we were expecting or not */
      if ((strstr(data->browser1->response, TRANSFORM_APPEND_STRING) != NULL) &&
          (strstr(data->browser3->response, TRANSFORM_APPEND_STRING) == NULL)
        ) {
        SDK_RPRINT(data->test, "INKHttpTxnUntransformedResponseCache", "TestCase1", TC_PASS, "ok");
        data->test_passed_txn_untransformed_resp_cache = true;
      } else {
        SDK_RPRINT(data->test, "INKHttpTxnUntransformedResponseCache", "TestCase1", TC_FAIL, "Value's Mismatch");
      }

      if ((strstr(data->browser2->response, TRANSFORM_APPEND_STRING) != NULL) &&
          (strstr(data->browser4->response, TRANSFORM_APPEND_STRING) != NULL)
        ) {
        SDK_RPRINT(data->test, "INKHttpTxnTransformedResponseCache", "TestCase1", TC_PASS, "ok");
        data->test_passed_txn_transformed_resp_cache = true;
      } else {
        SDK_RPRINT(data->test, "INKHttpTxnTransformedResponseCache", "TestCase1", TC_FAIL, "Value's Mismatch");
      }

      /* Note: response is available using test->browser->response pointer */
      *(data->pstatus) = REGRESSION_TEST_PASSED;
      if (data->browser1->status != REQUEST_SUCCESS) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "Browser 1 status was not REQUEST_SUCCESS");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->browser2->status != REQUEST_SUCCESS) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "Browser 2 status was not REQUEST_SUCCESS");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->browser3->status != REQUEST_SUCCESS) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "Browser 3 status was not REQUEST_SUCCESS");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->browser4->status != REQUEST_SUCCESS) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "Browser 4 status was not REQUEST_SUCCESS");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->test_passed_txn_transform_resp_get != true) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "did not pass transform_resp_get");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->test_passed_txn_transformed_resp_cache != true) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "did not pass transformed_resp_cache");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->test_passed_txn_untransformed_resp_cache != true) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "did not pass untransformed_resp_cache");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      if (data->test_passed_transform_create != true) {
        SDK_RPRINT(data->test, "INKTransformCreate", "TestCase1", TC_FAIL, "did not pass transform_create");
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }
      // transaction is over. clean up.
      synclient_txn_delete(data->browser1);
      synclient_txn_delete(data->browser2);
      synclient_txn_delete(data->browser3);
      synclient_txn_delete(data->browser4);

      data->magic = MAGIC_DEAD;
      INKfree(data);
      INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(data->test, "INKHttpTxnTransform", "TestCase1", TC_FAIL, "Unexpected event %d", event);
    break;
  }
  return 0;
}


EXCLUSIVE_REGRESSION_TEST(SDK_API_HttpTxnTransform) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  Debug(UTDBG_TAG "_transform", "Starting test");

  INKCont cont = INKContCreate(transform_hook_handler, INKMutexCreate());
  if ((cont == NULL) || (cont == INK_ERROR_PTR)) {
    SDK_RPRINT(test, "INKHttSsn", "TestCase1", TC_FAIL, "Unable to create Continuation.");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  TransformTestData *socktest = (TransformTestData *) INKmalloc(sizeof(TransformTestData));
  socktest->test = test;
  socktest->pstatus = pstatus;
  socktest->test_passed_txn_transform_resp_get = true;
  socktest->test_passed_txn_transformed_resp_cache = false;
  socktest->test_passed_txn_transformed_resp_cache = false;
  socktest->test_passed_transform_create = false;
  socktest->transformData = NULL;
  socktest->req_no = 1;
  socktest->magic = MAGIC_ALIVE;
  INKContDataSet(cont, socktest);

  /* Prepare the buffer to be appended to responses */
  load(TRANSFORM_APPEND_STRING);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, cont);

  /* Create a new synthetic server */
  socktest->os = synserver_create(SYNSERVER_LISTEN_PORT);
  synserver_start(socktest->os);

  /* Create a client transaction */
  socktest->browser1 = synclient_txn_create();
  socktest->browser2 = synclient_txn_create();
  socktest->browser3 = synclient_txn_create();
  socktest->browser4 = synclient_txn_create();
  socktest->request1 = generate_request(4);
  socktest->request2 = generate_request(5);
  Debug(UTDBG_TAG "_transform", "Running Browser 1");
  synclient_txn_send_request(socktest->browser1, socktest->request1);
  // synclient_txn_send_request(socktest->browser2, socktest->request2);

  /* Wait until transaction is done */
  INKContSchedule(cont, 25);

  return;
}

//////////////////////////////////////////////
//       SDK_API_INKHttpTxnAltInfo
//
// Unit Test for API: INKHttpTxnCachedReqGet
//                    INKHttpTxnCachedRespGet
//////////////////////////////////////////////

typedef struct
{
  RegressionTest *test;
  int *pstatus;
  SocketServer *os;
  ClientTxn *browser1;
  ClientTxn *browser2;
  ClientTxn *browser3;
  char *request1;
  char *request2;
  char *request3;
  bool test_passed_txn_alt_info_client_req_get;
  bool test_passed_txn_alt_info_cached_req_get;
  bool test_passed_txn_alt_info_cached_resp_get;
  bool test_passed_txn_alt_info_quality_set;
  bool run_at_least_once;
  bool first_time;
  int magic;
} AltInfoTestData;

static int
altinfo_hook_handler(INKCont contp, INKEvent event, void *edata)
{

  AltInfoTestData *data = NULL;
  data = (AltInfoTestData *) INKContDataGet(contp);
  if ((data == INK_ERROR_PTR) || (data == NULL)) {
    switch (event) {
    case INK_EVENT_IMMEDIATE:
    case INK_EVENT_TIMEOUT:
      break;
    case INK_EVENT_HTTP_SELECT_ALT:
      break;
    default:
      INKHttpTxnReenable((INKHttpTxn) edata, INK_EVENT_HTTP_CONTINUE);
      break;
    }
    return 0;
  }

  switch (event) {
  case INK_EVENT_HTTP_SELECT_ALT:
    {
      INKMBuffer clientreqbuf;
      INKMBuffer cachereqbuf;
      INKMBuffer cacherespbuf;

      INKMLoc clientreqhdr;
      INKMLoc cachereqhdr;
      INKMLoc cacheresphdr;

      INKHttpAltInfo infop = (INKHttpAltInfo) edata;

      data->run_at_least_once = true;
      if (INKHttpAltInfoClientReqGet(infop, &clientreqbuf, &clientreqhdr) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpAltInfoClientReqGet", "TestCase", TC_FAIL,
                   "INKHttpAltInfoClientReqGet doesn't return INK_SUCCESS");
        data->test_passed_txn_alt_info_client_req_get = false;
      } else {
        if ((clientreqbuf == (&(((HttpAltInfo *) infop)->m_client_req))) &&
            (clientreqhdr == ((HttpAltInfo *) infop)->m_client_req.m_http)
          ) {
          SDK_RPRINT(data->test, "INKHttpAltInfoClientReqGet", "TestCase", TC_PASS, "ok");
        } else {
          SDK_RPRINT(data->test, "INKHttpAltInfoClientReqGet", "TestCase", TC_FAIL, "Value's Mismatch");
          data->test_passed_txn_alt_info_client_req_get = false;
        }
      }

      if (INKHttpAltInfoCachedReqGet(infop, &cachereqbuf, &cachereqhdr) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpAltInfoCachedReqGet", "TestCase", TC_FAIL,
                   "INKHttpAltInfoCachedReqGet doesn't return INK_SUCCESS");
        data->test_passed_txn_alt_info_cached_req_get = false;
      } else {
        if ((cachereqbuf == (&(((HttpAltInfo *) infop)->m_cached_req))) &&
            (cachereqhdr == ((HttpAltInfo *) infop)->m_cached_req.m_http)
          ) {
          SDK_RPRINT(data->test, "INKHttpAltInfoCachedReqGet", "TestCase", TC_PASS, "ok");
        } else {
          SDK_RPRINT(data->test, "INKHttpAltInfoCachedReqGet", "TestCase", TC_FAIL, "Value's Mismatch");
          data->test_passed_txn_alt_info_cached_req_get = false;
        }
      }

      if (INKHttpAltInfoCachedRespGet(infop, &cacherespbuf, &cacheresphdr) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpAltInfoCachedRespGet", "TestCase", TC_FAIL,
                   "INKHttpAltInfoCachedRespGet doesn't return INK_SUCCESS");
        data->test_passed_txn_alt_info_cached_resp_get = false;
      } else {
        if ((cacherespbuf == (&(((HttpAltInfo *) infop)->m_cached_resp))) &&
            (cacheresphdr == ((HttpAltInfo *) infop)->m_cached_resp.m_http)
          ) {
          SDK_RPRINT(data->test, "INKHttpAltInfoCachedRespGet", "TestCase", TC_PASS, "ok");
        } else {
          SDK_RPRINT(data->test, "INKHttpAltInfoCachedRespGet", "TestCase", TC_FAIL, "Value's Mismatch");
          data->test_passed_txn_alt_info_cached_resp_get = false;
        }
      }

      if (INKHttpAltInfoQualitySet(infop, 0.5) != INK_SUCCESS) {
        SDK_RPRINT(data->test, "INKHttpAltInfoQualityset", "TestCase", TC_FAIL,
                   "INKHttpAltInfoQualitySet doesn't return INK_SUCCESS");
        data->test_passed_txn_alt_info_quality_set = false;
      } else {
        SDK_RPRINT(data->test, "INKHttpAltInfoQualitySet", "TestCase", TC_PASS, "ok");
      }
    }

    break;

  case INK_EVENT_IMMEDIATE:
  case INK_EVENT_TIMEOUT:
    /* Browser still waiting the response ? */
    if (data->first_time == true) {
      if ((data->browser1->status == REQUEST_INPROGRESS) || (data->browser2->status == REQUEST_INPROGRESS)
        ) {
        INKContSchedule(contp, 25);
        return 0;
      }
    } else {
      if (data->browser3->status == REQUEST_INPROGRESS) {
        INKContSchedule(contp, 25);
        return 0;
      }
    }

    /* Browser got the response. test is over. clean up */
    {
      /* If this is the first time, then both the responses are in cache and we should make */
      /* another request to get cache hit */
      if (data->first_time == true) {
        data->first_time = false;
        /* Kill the origin server */
        synserver_delete(data->os);
// ink_release_assert(0);
        /* Send another similar client request */
        synclient_txn_send_request(data->browser3, data->request3);

        /* Register to HTTP hooks that are called in case of alternate selection */
        if (INKHttpHookAdd(INK_HTTP_SELECT_ALT_HOOK, contp) != INK_SUCCESS) {
          SDK_RPRINT(data->test, "INKHttpAltInfo", "", TC_FAIL, "INKHttpHookAdd doesn't return INK_SUCCESS");
        }

        INKContSchedule(contp, 25);
        return 0;
      }

      /* Note: response is available using test->browser->response pointer */
      if ((data->browser3->status == REQUEST_SUCCESS) &&
          (data->test_passed_txn_alt_info_client_req_get == true) &&
          (data->test_passed_txn_alt_info_cached_req_get == true) &&
          (data->test_passed_txn_alt_info_cached_resp_get == true) &&
          (data->test_passed_txn_alt_info_quality_set == true) && (data->run_at_least_once == true)
        ) {
        *(data->pstatus) = REGRESSION_TEST_PASSED;
      } else {
        if (data->run_at_least_once == false) {
          SDK_RPRINT(data->test, "INKHttpAltInfo", "All", TC_FAIL, "Test not executed even once");
        }
        *(data->pstatus) = REGRESSION_TEST_FAILED;
      }

      // transaction is over. clean up.
      synclient_txn_delete(data->browser1);
      synclient_txn_delete(data->browser2);
      synclient_txn_delete(data->browser3);

      INKfree(data->request1);
      INKfree(data->request2);
      INKfree(data->request3);

      data->magic = MAGIC_DEAD;
      INKfree(data);
      INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(data->test, "INKHttpTxnCache", "TestCase1", TC_FAIL, "Unexpected event %d", event);
    break;
  }
  return 0;
}



EXCLUSIVE_REGRESSION_TEST(SDK_API_HttpAltInfo) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKCont cont = INKContCreate(altinfo_hook_handler, INKMutexCreate());
  if ((cont == NULL) || (cont == INK_ERROR_PTR)) {
    SDK_RPRINT(test, "INKHttSsn", "TestCase1", TC_FAIL, "Unable to create Continuation.");
    *pstatus = REGRESSION_TEST_FAILED;
    return;
  }

  AltInfoTestData *socktest = (AltInfoTestData *) INKmalloc(sizeof(AltInfoTestData));
  socktest->test = test;
  socktest->pstatus = pstatus;
  socktest->test_passed_txn_alt_info_client_req_get = true;
  socktest->test_passed_txn_alt_info_cached_req_get = true;
  socktest->test_passed_txn_alt_info_cached_resp_get = true;
  socktest->test_passed_txn_alt_info_quality_set = true;
  socktest->run_at_least_once = false;
  socktest->first_time = true;
  socktest->magic = MAGIC_ALIVE;
  INKContDataSet(cont, socktest);

  /* Create a new synthetic server */
  socktest->os = synserver_create(SYNSERVER_LISTEN_PORT);
  synserver_start(socktest->os);

  /* Create a client transaction */
  socktest->browser1 = synclient_txn_create();
  socktest->browser2 = synclient_txn_create();
  socktest->browser3 = synclient_txn_create();
  socktest->request1 = generate_request(6);
  socktest->request2 = generate_request(7);
  socktest->request3 = generate_request(8);
  synclient_txn_send_request(socktest->browser1, socktest->request1);
  synclient_txn_send_request(socktest->browser2, socktest->request2);

  /* Wait until transaction is done */
  INKContSchedule(cont, 25);

  return;
}


//////////////////////////////////////////////
//       SDK_API_INKHttpConnect
//
// Unit Test for APIs:
//      - INKHttpConnect
//      - INKHttpTxnIntercept
//      - INKHttpTxnInterceptServer
//
//
// 2 Test cases.
//
// Same test strategy:
//  - create a synthetic server listening on port A
//  - use HttpConnect to send a request to TS for an url on a remote host H, port B
//  - use TxnIntercept or TxnServerIntercept to forward the request
//    to the synthetic server on local host, port A
//  - make sure response is correct
//
//////////////////////////////////////////////

// Important: we create servers listening on different port than the default one
// to make sure our synthetix servers are called

#define TEST_CASE_CONNECT_ID1 9 //INKHttpTxnIntercept
#define TEST_CASE_CONNECT_ID2 10        //INKHttpTxnServerIntercept

#define SYNSERVER_DUMMY_PORT -1

typedef struct
{
  RegressionTest *test;
  int *pstatus;
  int test_case;
  INKVConn vc;
  SocketServer *os;
  ClientTxn *browser;
  char *request;
  unsigned long magic;
} ConnectTestData;


static int
cont_test_handler(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;
  ConnectTestData *data = (ConnectTestData *) INKContDataGet(contp);
  int request_id = -1;

  INKReleaseAssert(data->magic == MAGIC_ALIVE);
  INKReleaseAssert((data->test_case == TEST_CASE_CONNECT_ID1) || (data->test_case == TEST_CASE_CONNECT_ID2));

  INKDebug(UTDBG_TAG, "Calling cont_test_handler with event %d", event);

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    INKDebug(UTDBG_TAG, "cont_test_handler: event READ_REQUEST");

    // First make sure we're getting called for either request 9 or txn 10
    // Otherwise, this is a request sent by another test. do nothing.
    request_id = get_request_id(txnp);
    INKReleaseAssert(request_id != -1);

    INKDebug(UTDBG_TAG, "cont_test_handler: Request id = %d", request_id);

    if ((request_id != TEST_CASE_CONNECT_ID1) && (request_id != TEST_CASE_CONNECT_ID2)) {
      INKDebug(UTDBG_TAG, "This is not an event for this test !");
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      goto done;
    }

    if ((request_id == TEST_CASE_CONNECT_ID1) && (data->test_case == TEST_CASE_CONNECT_ID1)) {
      INKDebug(UTDBG_TAG, "Calling INKHttpTxnIntercept");
      INKHttpTxnIntercept(data->os->accept_cont, txnp);
    } else if ((request_id == TEST_CASE_CONNECT_ID2) && (data->test_case == TEST_CASE_CONNECT_ID2)) {
      INKDebug(UTDBG_TAG, "Calling INKHttpTxnServerIntercept");
      INKHttpTxnServerIntercept(data->os->accept_cont, txnp);
    }

    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_TIMEOUT:
    /* Browser still waiting the response ? */
    if (data->browser->status == REQUEST_INPROGRESS) {
      INKDebug(UTDBG_TAG, "Browser still waiting response...");
      INKContSchedule(contp, 25);
    }
    /* Browser got the response */
    else {

      /* Check if browser response body is the one we expected */
      char *body_response = get_body_ptr(data->browser->response);
      const char *body_expected;
      if (data->test_case == TEST_CASE_CONNECT_ID1) {
        body_expected = "Body for response 9";
      } else {
        body_expected = "Body for response 10";
      }
      INKDebug(UTDBG_TAG, "Body Response = \n|%s|\nBody Expected = \n|%s|", body_response, body_expected);

      if (strncmp(body_response, body_expected, strlen(body_expected)) != 0) {
        if (data->test_case == TEST_CASE_CONNECT_ID1) {
          SDK_RPRINT(data->test, "INKHttpConnect", "TestCase1", TC_FAIL, "Unexpected response");
          SDK_RPRINT(data->test, "INKHttpTxnIntercept", "TestCase1", TC_FAIL, "Unexpected response");
        } else {
          SDK_RPRINT(data->test, "INKHttpConnect", "TestCase2", TC_FAIL, "Unexpected response");
          SDK_RPRINT(data->test, "INKHttpTxnServerIntercept", "TestCase2", TC_FAIL, "Unexpected response");
        }
        *(data->pstatus) = REGRESSION_TEST_FAILED;

      } else {
        if (data->test_case == TEST_CASE_CONNECT_ID1) {
          SDK_RPRINT(data->test, "INKHttpConnect", "TestCase1", TC_PASS, "ok");
          SDK_RPRINT(data->test, "INKHttpTxnIntercept", "TestCase1", TC_PASS, "ok");
        } else {
          SDK_RPRINT(data->test, "INKHttpConnect", "TestCase2", TC_PASS, "ok");
          SDK_RPRINT(data->test, "INKHttpTxnServerIntercept", "TestCase2", TC_PASS, "ok");
        }
        *(data->pstatus) = REGRESSION_TEST_PASSED;
      }

      // transaction is over. clean it up.
      synclient_txn_delete(data->browser);
      synserver_delete(data->os);

      // As we registered to a global hook, we may be called back again.
      // Do not destroy the continuation...
      // data->magic = MAGIC_DEAD;
      // INKfree(data);
      // INKContDataSet(contp, NULL);
    }
    break;

  default:
    *(data->pstatus) = REGRESSION_TEST_FAILED;
    SDK_RPRINT(data->test, "INKHttpConnect", "TestCase1 or 2", TC_FAIL, "Unexpected event %d", event);
    break;
  }

done:
  return INK_EVENT_IMMEDIATE;
}


EXCLUSIVE_REGRESSION_TEST(SDK_API_INKHttpConnectIntercept) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKDebug(UTDBG_TAG, "Starting test INKHttpConnectIntercept");

  INKCont cont_test = INKContCreate(cont_test_handler, INKMutexCreate());
  ConnectTestData *data = (ConnectTestData *) INKmalloc(sizeof(ConnectTestData));
  INKContDataSet(cont_test, data);

  data->test = test;
  data->pstatus = pstatus;
  data->magic = MAGIC_ALIVE;
  data->test_case = TEST_CASE_CONNECT_ID1;

  /* Register to hook READ_REQUEST */
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, cont_test);

  // Create a synthetic server which won't really listen on a socket port
  // It will be called by the Http SM with a VC
  data->os = synserver_create(SYNSERVER_DUMMY_PORT);

  data->browser = synclient_txn_create();
  data->request = generate_request(9);

  /* Now send a request to the OS via TS using INKHttpConnect */

  /* ip and log do not matter as it is used for logging only */
  INKHttpConnect(1, 1, &(data->vc));

  synclient_txn_send_request_to_vc(data->browser, data->request, data->vc);

  /* Wait until transaction is done */
  INKContSchedule(cont_test, 25);

  return;
}


EXCLUSIVE_REGRESSION_TEST(SDK_API_INKHttpConnectServerIntercept) (RegressionTest * test, int atype, int *pstatus)
{
  NOWARN_UNUSED(atype);
  *pstatus = REGRESSION_TEST_INPROGRESS;

  INKDebug(UTDBG_TAG, "Starting test INKHttpConnectServerintercept");

  INKCont cont_test = INKContCreate(cont_test_handler, INKMutexCreate());
  ConnectTestData *data = (ConnectTestData *) INKmalloc(sizeof(ConnectTestData));
  INKContDataSet(cont_test, data);

  data->test = test;
  data->pstatus = pstatus;
  data->magic = MAGIC_ALIVE;
  data->test_case = TEST_CASE_CONNECT_ID2;

  /* Register to hook READ_REQUEST */
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, cont_test);

  /* This is cool ! we can use the code written for the synthetic server and client in InkAPITest.cc */
  data->os = synserver_create(SYNSERVER_DUMMY_PORT);

  data->browser = synclient_txn_create();
  data->request = generate_request(10);

  /* Now send a request to the OS via TS using INKHttpConnect */

  /* ip and log do not matter as it is used for logging only */
  INKHttpConnect(2, 2, &(data->vc));

  synclient_txn_send_request_to_vc(data->browser, data->request, data->vc);

  /* Wait until transaction is done */
  INKContSchedule(cont_test, 25);

  return;
}
