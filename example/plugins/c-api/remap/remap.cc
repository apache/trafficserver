/** @file

  A simple remap plugin for ATS

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

  @section description
  Build this sample remap plugin using tsxs:

    $ tsxs -v -o remap.so remap.cc

  To install it:
    # tsxs -i -o remap.so
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <pthread.h>
#include <cstdint>
#include <atomic>

#include "tscore/ink_defs.h"
#include "ts/ts.h"
#include "ts/remap.h"

#define PLUGIN_NAME "remap"

class remap_entry
{
public:
  static remap_entry *active_list;
  static pthread_mutex_t mutex;
  remap_entry *next;
  int argc;
  char **argv;
  remap_entry(int _argc, char *_argv[]);
  ~remap_entry();
  static void add_to_list(remap_entry *re);
  static void remove_from_list(remap_entry *re);
};

static int plugin_init_counter = 0;               /* remap plugin initialization counter */
static pthread_mutex_t remap_plugin_global_mutex; /* remap plugin global mutex */
remap_entry *remap_entry::active_list = nullptr;
pthread_mutex_t remap_entry::mutex; /* remap_entry class mutex */

/* ----------------------- remap_entry::remap_entry ------------------------ */
remap_entry::remap_entry(int _argc, char *_argv[]) : next(nullptr), argc(0), argv(nullptr)
{
  if (_argc > 0 && _argv && (argv = static_cast<char **>(TSmalloc(sizeof(char *) * (_argc + 1)))) != nullptr) {
    int i;
    argc = _argc;
    for (i = 0; i < argc; i++) {
      argv[i] = TSstrdup(_argv[i]);
    }
    argv[i] = nullptr;
  }
}

/* ---------------------- remap_entry::~remap_entry ------------------------ */
remap_entry::~remap_entry()
{
  if (argc && argv) {
    for (int i = 0; i < argc; i++) {
      TSfree(argv[i]);
    }
    TSfree(argv);
  }
}

/* --------------------- remap_entry::add_to_list -------------------------- */
void
remap_entry::add_to_list(remap_entry *re)
{
  if (likely(re && plugin_init_counter)) {
    pthread_mutex_lock(&mutex);
    re->next    = active_list;
    active_list = re;
    pthread_mutex_unlock(&mutex);
  }
}

/* ------------------ remap_entry::remove_from_list ------------------------ */
void
remap_entry::remove_from_list(remap_entry *re)
{
  if (likely(re && plugin_init_counter)) {
    pthread_mutex_lock(&mutex);
    for (remap_entry **rre = &active_list; *rre; rre = &((*rre)->next)) {
      if (re == *rre) {
        *rre = re->next;
        break;
      }
    }
    pthread_mutex_unlock(&mutex);
  }
}

/* ----------------------- store_my_error_message -------------------------- */
static TSReturnCode
store_my_error_message(TSReturnCode retcode, char *err_msg_buf, int buf_size, const char *fmt, ...)
{
  if (likely(err_msg_buf && buf_size > 0 && fmt)) {
    va_list ap;
    va_start(ap, fmt);
    err_msg_buf[0] = 0;
    (void)vsnprintf(err_msg_buf, buf_size, fmt, ap);
    err_msg_buf[buf_size - 1] = 0;
    va_end(ap);
  }
  return retcode; /* error code here */
}

// Plugin initialization code. Called immediately after dlopen() Only once!
// Can perform internal initialization. For example, pthread_.... initialization.
/* ------------------------- TSRemapInit ---------------------------------- */
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "enter");

  if (!plugin_init_counter) {
    if (unlikely(!api_info)) {
      return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Invalid TSRemapInterface argument");
    }
    if (unlikely(api_info->size < sizeof(TSRemapInterface))) {
      return store_my_error_message(TS_ERROR, errbuf, errbuf_size,
                                    "Incorrect size of TSRemapInterface structure %d. Should be at least %d bytes",
                                    static_cast<int>(api_info->size), static_cast<int>(sizeof(TSRemapInterface)));
    }
    if (unlikely(api_info->tsremap_version < TSREMAP_VERSION)) {
      return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Incorrect API version %d.%d", (api_info->tsremap_version >> 16),
                                    (api_info->tsremap_version & 0xffff));
    }

    if (pthread_mutex_init(&remap_plugin_global_mutex, nullptr) ||
        pthread_mutex_init(&remap_entry::mutex, nullptr)) { /* pthread_mutex_init - always returns 0. :) - impossible error */
      return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Mutex initialization error");
    }
    plugin_init_counter++;
  }
  return TS_SUCCESS; /* success */
}

/* ------------------------ TSRemapNewInstance --------------------------- */
// Plugin new instance for new remapping rule.
// This function can be called multiple times (depends on remap.config)
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  remap_entry *ri;
  int i;

  TSDebug(PLUGIN_NAME, "enter"); // Debug output automatically includes the file, line #, and function.

  if (argc < 2) {
    return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Incorrect number of arguments - %d", argc);
  }
  if (!argv || !ih) {
    return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Invalid argument(s)");
  }
  // print all arguments for this particular remapping
  for (i = 0; i < argc; i++) {
    TSDebug(PLUGIN_NAME, "[%s] - argv[%d] = \"%s\"\n", __func__, i, argv[i]);
  }

  ri = new remap_entry(argc, argv);

  if (!ri) {
    return store_my_error_message(TS_ERROR, errbuf, errbuf_size, "Can't create remap_entry class");
  }

  remap_entry::add_to_list(ri);

  *ih = ri;

  return TS_SUCCESS;
}
/* ---------------------- TSRemapDeleteInstance -------------------------- */
void
TSRemapDeleteInstance(void *ih)
{
  remap_entry *ri = static_cast<remap_entry *>(ih);

  TSDebug(PLUGIN_NAME, "enter");

  remap_entry::remove_from_list(ri);

  delete ri;
}

static std::atomic<uint64_t> processing_counter; // sequential counter
static int arg_index;

/* -------------------------- TSRemapDoRemap -------------------------------- */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  const char *temp;
  const char *temp2;
  int len, len2, port;
  TSMLoc cfield;
  uint64_t _processing_counter = processing_counter++;

  remap_entry *ri = static_cast<remap_entry *>(ih);
  TSDebug(PLUGIN_NAME, "enter");

  if (!ri || !rri) {
    return TSREMAP_NO_REMAP; /* TS must remap this request */
  }

  TSDebug(PLUGIN_NAME, "From: \"%s\"  To: \"%s\"\n", ri->argv[0], ri->argv[1]);

  temp = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &len);
  TSDebug(PLUGIN_NAME, "Request Host(%d): \"%.*s\"\n", len, len, temp);

  temp = TSUrlHostGet(rri->requestBufp, rri->mapToUrl, &len);
  TSDebug(PLUGIN_NAME, "Remap To Host: \"%.*s\"\n", len, temp);

  temp = TSUrlHostGet(rri->requestBufp, rri->mapFromUrl, &len);
  TSDebug(PLUGIN_NAME, "Remap From Host: \"%.*s\"\n", len, temp);

  TSDebug(PLUGIN_NAME, "Request Port: %d\n", TSUrlPortGet(rri->requestBufp, rri->requestUrl));
  TSDebug(PLUGIN_NAME, "Remap From Port: %d\n", TSUrlPortGet(rri->requestBufp, rri->mapFromUrl));
  TSDebug(PLUGIN_NAME, "Remap To Port: %d\n", TSUrlPortGet(rri->requestBufp, rri->mapToUrl));

  temp = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &len);
  TSDebug(PLUGIN_NAME, "Request Path: \"%.*s\"\n", len, temp);

  temp = TSUrlPathGet(rri->requestBufp, rri->mapFromUrl, &len);
  TSDebug(PLUGIN_NAME, "Remap From Path: \"%.*s\"\n", len, temp);

  temp = TSUrlPathGet(rri->requestBufp, rri->mapToUrl, &len);
  TSDebug(PLUGIN_NAME, "Remap To Path: \"%.*s\"\n", len, temp);

  // InkAPI usage case
  const char *value;

  if ((cfield = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_DATE, -1)) != TS_NULL_MLOC) {
    TSDebug(PLUGIN_NAME, "We have \"Date\" header in request\n");
    value = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, cfield, -1, nullptr);
    TSDebug(PLUGIN_NAME, "Header value: %s\n", value);
  }
  if ((cfield = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, "MyHeader", sizeof("MyHeader") - 1)) != TS_NULL_MLOC) {
    TSDebug(PLUGIN_NAME, "We have \"MyHeader\" header in request\n");
    value = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, cfield, -1, nullptr);
    TSDebug(PLUGIN_NAME, "Header value: %s\n", value);
  }

  // How to store plugin private arguments inside Traffic Server request processing block.
  if (TSHttpTxnArgIndexReserve("remap_example", "Example remap plugin", &arg_index) == TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "Save processing counter %" PRIu64 " inside request processing block\n", _processing_counter);
    TSHttpTxnArgSet(rh, arg_index, (void *)_processing_counter); // save counter
  }
  // How to cancel request processing and return error message to the client
  // We will do it every other request
  if (_processing_counter & 1) {
    char *tmp                   = static_cast<char *>(TSmalloc(256));
    static int my_local_counter = 0;

    len = snprintf(tmp, 255, "This is very small example of TS API usage!\nIteration %d!\nHTTP return code %d\n", my_local_counter,
                   TS_HTTP_STATUS_CONTINUE + my_local_counter);
    TSHttpTxnStatusSet(rh, static_cast<TSHttpStatus>(static_cast<int>(TS_HTTP_STATUS_CONTINUE) + my_local_counter));
    TSHttpTxnErrorBodySet(rh, tmp, len, nullptr); // Defaults to text/html
    my_local_counter++;
  }
  // hardcoded case for remapping
  // You need to check host and port if you are using the same plugin for multiple remapping rules
  temp  = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &len);
  temp2 = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &len2);
  port  = TSUrlPortGet(rri->requestBufp, rri->requestUrl);

  if (len == 10 && !memcmp("flickr.com", temp, 10) && port == 80 && len2 >= 3 && !memcmp("47/", temp2, 3)) {
    char new_path[8192];

    // Ugly, but so is the rest of this "example"
    if (len2 + 7 >= 8192) {
      return TSREMAP_NO_REMAP;
    }

    if (TSUrlPortSet(rri->requestBufp, rri->mapToUrl, TSUrlPortGet(rri->requestBufp, rri->mapToUrl)) != TS_SUCCESS) {
      return TSREMAP_NO_REMAP;
    }

    if (TSUrlHostSet(rri->requestBufp, rri->requestUrl, "foo.bar.com", 11) != TS_SUCCESS) {
      return TSREMAP_NO_REMAP;
    }

    memcpy(new_path, "47_copy", 7);
    memcpy(&new_path[7], &temp2[2], len2 - 2);

    if (TSUrlPathSet(rri->requestBufp, rri->requestUrl, new_path, len2 + 5) == TS_SUCCESS) {
      return TSREMAP_DID_REMAP;
    }
  }

  // Failure ...
  return TSREMAP_NO_REMAP;
}

/* ----------------------- TSRemapOSResponse ----------------------------- */
void
TSRemapOSResponse(void *ih ATS_UNUSED, TSHttpTxn rh, int os_response_type)
{
  void *data     = TSHttpTxnArgGet(rh, arg_index); // read counter (we store it in TSRemapDoRemap function call)
  int request_id = data ? static_cast<int *>(data)[0] : -1;

  TSDebug(PLUGIN_NAME, "Read processing counter %d from request processing block\n", request_id);
  TSDebug(PLUGIN_NAME, "OS response status: %d\n", os_response_type);
}
