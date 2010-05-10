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

/* ------------------------------------------------------------------------- */
/* -                            remap.cc                                   - */
/* ------------------------------------------------------------------------- */
// remap.cc - simple remap plugin for YTS
// g++ -D_FILE_OFFSET_BITS=64 -shared -I./ -o remap.so remap.cc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <pwd.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ts/remap.h>
#include <ts/ts.h>

#if __GNUC__ >= 3
#ifndef likely
#define likely(x)   __builtin_expect (!!(x),1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect (!!(x),0)
#endif
#else
#ifndef likely
#define likely(x)   (x)
#endif
#ifndef unlikely
#define unlikely(x) (x)
#endif
#endif /* #if __GNUC__ >= 3 */

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
  static void add_to_list(remap_entry * re);
  static void remove_from_list(remap_entry * re);
};

static int plugin_init_counter = 0;     /* remap plugin initialization counter */
static pthread_mutex_t remap_plugin_global_mutex;       /* remap plugin global mutex */
remap_entry *
  remap_entry::active_list = 0;
pthread_mutex_t
  remap_entry::mutex;           /* remap_entry class mutex */

/* ----------------------- remap_entry::remap_entry ------------------------ */
remap_entry::remap_entry(int _argc, char *_argv[]):
next(NULL),
argc(0),
argv(NULL)
{
  int i;
  if (_argc > 0 && _argv && (argv = (char **) malloc(sizeof(char *) * (_argc + 1))) != 0) {
    argc = _argc;
    for (i = 0; i < argc; i++)
      argv[i] = strdup(_argv[i]);
    argv[i] = NULL;
  }
}

/* ---------------------- remap_entry::~remap_entry ------------------------ */
remap_entry::~remap_entry()
{
  int i;
  if (argc && argv) {
    for (i = 0; i < argc; i++) {
      if (argv[i])
        free(argv[i]);
    }
    free(argv);
  }
}

/* --------------------- remap_entry::add_to_list -------------------------- */
void
remap_entry::add_to_list(remap_entry * re)
{
  if (likely(re && plugin_init_counter)) {
    pthread_mutex_lock(&mutex);
    re->next = active_list;
    active_list = re;
    pthread_mutex_unlock(&mutex);
  }
}

/* ------------------ remap_entry::remove_from_list ------------------------ */
void
remap_entry::remove_from_list(remap_entry * re)
{
  remap_entry **rre;
  if (likely(re && plugin_init_counter)) {
    pthread_mutex_lock(&mutex);
    for (rre = &active_list; *rre; rre = &((*rre)->next)) {
      if (re == *rre) {
        *rre = re->next;
        break;
      }
    }
    pthread_mutex_unlock(&mutex);
  }
}

/* ----------------------- store_my_error_message -------------------------- */
static int
store_my_error_message(int retcode, char *err_msg_buf, int buf_size, const char *fmt, ...)
{
  if (likely(err_msg_buf && buf_size > 0 && fmt)) {
    va_list ap;
    va_start(ap, fmt);
    err_msg_buf[0] = 0;
    (void) vsnprintf(err_msg_buf, buf_size - 1, fmt, ap);
    err_msg_buf[buf_size - 1] = 0;
    va_end(ap);
  }
  return retcode;               /* error code here */
}

/* -------------------------- my_print_ascii_string ------------------------ */
static void
my_print_ascii_string(const char *str, int str_size)
{
  char buf[1024];
  int i, j;

  if (str) {
    for (i = 0; i < str_size;) {
      if ((j = (str_size - i)) >= (int) sizeof(buf))
        j = (int) (sizeof(buf) - 1);
      memcpy(buf, str, j);
      buf[j] = 0;
      fprintf(stderr, "%s", buf);
      str += j;
      i += j;
    }
  }
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;
  info.plugin_name = (char*)"remap_plugin";
  info.vendor_name = (char*)"Apache";
  info.support_email = (char*)"";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed. \n");
  }
  INKDebug("debug-remap", "INKPluginInit: Remap plugin started\n");
}

// Plugin initialization code. Called immediately after dlopen() Only once!
// Can perform internal initialization. For example, pthread_.... initialization.
/* ------------------------- tsremap_init ---------------------------------- */
int
tsremap_init(TSRemapInterface * api_info, char *errbuf, int errbuf_size)
{
  fprintf(stderr, "Remap Plugin: tsremap_init()\n");

  if (!plugin_init_counter) {
    if (unlikely(!api_info)) {
      return store_my_error_message(-1, errbuf, errbuf_size, "[tsremap_init] - Invalid TSRemapInterface argument");
    }
    if (unlikely(api_info->size < sizeof(TSRemapInterface))) {
      return store_my_error_message(-2, errbuf, errbuf_size,
                                    "[tsremap_init] - Incorrect size of TSRemapInterface structure %d. Should be at least %d bytes",
                                    (int) api_info->size, (int) sizeof(TSRemapInterface));
    }
    if (unlikely(api_info->tsremap_version < TSREMAP_VERSION)) {
      return store_my_error_message(-3, errbuf, errbuf_size,
                                    "[tsremap_init] - Incorrect API version %d.%d",
                                    (api_info->tsremap_version >> 16), (api_info->tsremap_version & 0xffff));
    }

    if (pthread_mutex_init(&remap_plugin_global_mutex, 0) || pthread_mutex_init(&remap_entry::mutex, 0)) {      /* pthread_mutex_init - always returns 0. :) - impossible error */
      return store_my_error_message(-4, errbuf, errbuf_size, "[tsremap_init] - Mutex initialization error");
    }
    plugin_init_counter++;
  }
  return 0;                     /* success */
}

// Plugin shutdown
// Optional function.
/* -------------------------- tsremap_done --------------------------------- */
int
tsremap_done(void)
{
  fprintf(stderr, "Remap Plugin: tsremap_done()\n");
  /* do nothing */

  return 0;
}


// Plugin new instance for new remapping rule.
// This function can be called multiple times (depends on remap.config)
/* ------------------------ tsremap_new_instance --------------------------- */
int
tsremap_new_instance(int argc, char *argv[], ihandle * ih, char *errbuf, int errbuf_size)
{
  remap_entry *ri;
  int i;


  fprintf(stderr, "Remap Plugin: tsremap_new_instance()\n");

  if (argc < 2) {
    return store_my_error_message(-1, errbuf, errbuf_size,
                                  "[tsremap_new_instance] - Incorrect number of arguments - %d", argc);
  }
  if (!argv || !ih) {
    return store_my_error_message(-2, errbuf, errbuf_size, "[tsremap_new_instance] - Invalid argument(s)");
  }
  // print all arguments for this particular remapping
  for (i = 0; i < argc; i++) {
    fprintf(stderr, "[tsremap_new_instance] - argv[%d] = \"%s\"\n", i, argv[i]);
  }

  ri = new remap_entry(argc, argv);

  if (!ri) {
    return store_my_error_message(-3, errbuf, errbuf_size, "[tsremap_new_instance] - Can't create remap_entry class");
  }

  remap_entry::add_to_list(ri);

  *ih = (ihandle) ri;

  return 0;
}

/* ---------------------- tsremap_delete_instance -------------------------- */
void
tsremap_delete_instance(ihandle ih)
{
  remap_entry *ri = (remap_entry *) ih;

  fprintf(stderr, "Remap Plugin: tsremap_delete_instance()\n");

  remap_entry::remove_from_list(ri);

  delete ri;
}

static volatile unsigned long processing_counter = 0;   // sequential counter

/* -------------------------- tsremap_remap -------------------------------- */
int
tsremap_remap(ihandle ih, rhandle rh, TSRemapRequestInfo * rri)
{
  char *p;
  INKMBuffer cbuf;
  INKMLoc chdr;
  INKMLoc cfield;
  int retcode = 0;              // TS must perform actual remapping
  unsigned long _processing_counter = ++processing_counter;     // one more function call (in real life use mutex to protect this counter)

  remap_entry *ri = (remap_entry *) ih;
  fprintf(stderr, "Remap Plugin: tsremap_remap()\n");

  if (!ri || !rri)
    return 0;                   /* TS must remap this request */
  p = (char *) &rri->client_ip;
  fprintf(stderr, "[tsremap_remap] Client IP: %d.%d.%d.%d\n", (int) p[0], (int) p[1], (int) p[2], (int) p[3]);
  fprintf(stderr, "[tsremap_remap] From: \"%s\"  To: \"%s\"\n", ri->argv[0], ri->argv[1]);
  fprintf(stderr, "[tsremap_remap] OrigURL: \"");
  my_print_ascii_string(rri->orig_url, rri->orig_url_size);
  fprintf(stderr, "\"\n[tsremap_remap] Request Host(%d): \"", rri->request_host_size);
  my_print_ascii_string(rri->request_host, rri->request_host_size);
  fprintf(stderr, "\"\n[tsremap_remap] Remap To Host: \"");
  my_print_ascii_string(rri->remap_to_host, rri->remap_to_host_size);
  fprintf(stderr, "\"\n[tsremap_remap] Remap From Host: \"");
  my_print_ascii_string(rri->remap_from_host, rri->remap_from_host_size);
  fprintf(stderr, "\"\n[tsremap_remap] Request Port: %d\n", rri->request_port);
  fprintf(stderr, "[tsremap_remap] Remap From Port: %d\n", rri->remap_from_port);
  fprintf(stderr, "[tsremap_remap] Remap To Port: %d\n", rri->remap_to_port);
  fprintf(stderr, "[tsremap_remap] Request Path: \"");
  my_print_ascii_string(rri->request_path, rri->request_path_size);
  fprintf(stderr, "\"\n[tsremap_remap] Remap From Path: \"");
  my_print_ascii_string(rri->remap_from_path, rri->remap_from_path_size);
  fprintf(stderr, "\"\n[tsremap_remap] Remap To Path: \"");
  my_print_ascii_string(rri->remap_to_path, rri->remap_to_path_size);
  fprintf(stderr, "\"\n[tsremap_remap] Request cookie: \"");
  my_print_ascii_string(rri->request_cookie, rri->request_cookie_size);
  fprintf(stderr, "\"\n");



  // InkAPI usage case
  if (INKHttpTxnClientReqGet((INKHttpTxn) rh, &cbuf, &chdr)) {
    const char *value;
    if ((cfield = INKMimeHdrFieldFind(cbuf, chdr, INK_MIME_FIELD_DATE, -1)) != NULL) {
      fprintf(stderr, "We have \"Date\" header in request\n");
      if ((value = INKMimeHdrFieldValueGet(cbuf, chdr, cfield, 0, NULL)) != NULL) {
        fprintf(stderr, "Header value: %s\n", value);
      }
    }
    if ((cfield = INKMimeHdrFieldFind(cbuf, chdr, "MyHeader", sizeof("MyHeader") - 1)) != NULL) {
      fprintf(stderr, "We have \"MyHeader\" header in request\n");
      if ((value = INKMimeHdrFieldValueGet(cbuf, chdr, cfield, 0, NULL)) != NULL) {
        fprintf(stderr, "Header value: %s\n", value);
      }
    }
    INKHandleMLocRelease(cbuf, chdr, cfield);
    INKHandleMLocRelease(cbuf, INK_NULL_MLOC, chdr);
  }
  // How to store plugin private arguments inside Traffic Server request processing block.
  // note: You can store up to INKHttpTxnGetMaxArgCnt() variables.
  if (INKHttpTxnGetMaxArgCnt() > 0) {
    fprintf(stderr,
            "[tsremap_remap] Save processing counter %lu inside request processing block\n", _processing_counter);
    INKHttpTxnSetArg((INKHttpTxn) rh, 1, (void *) _processing_counter); // save counter
  }
  // How to cancel request processing and return error message to the client
  // We wiil do it each other request
  if (_processing_counter & 1) {
    char tmp[256];
    static int my_local_counter = 0;
    snprintf(tmp, sizeof(tmp) - 1,
             "This is very small example of INK API usage!\nIteration %d!\nHTTP return code %d\n",
             my_local_counter, INK_HTTP_STATUS_CONTINUE + my_local_counter);
    INKHttpTxnSetHttpRetStatus((INKHttpTxn) rh, (INKHttpStatus) ((int) INK_HTTP_STATUS_CONTINUE + my_local_counter));   //INK_HTTP_STATUS_SERVICE_UNAVAILABLE); //INK_HTTP_STATUS_NOT_ACCEPTABLE);
    INKHttpTxnSetHttpRetBody((INKHttpTxn) rh, (const char *) tmp, true);
    my_local_counter++;
  }
  // hardcoded case for remapping
  // You need to check host and port if you are using the same plugin for multiple remapping rules
  if (rri->request_host_size == 10 &&
      !memcmp("flickr.com", rri->request_host, 10) &&
      rri->request_port == 80 && rri->request_path_size >= 3 && !memcmp("47/", rri->request_path, 3)) {
    rri->new_port = rri->remap_to_port; /* set new port */

    strcpy(rri->new_host, "foo.bar.com");    /* set new host name */
    rri->new_host_size = strlen(rri->new_host);

    memcpy(rri->new_path, "47_copy", 7);
    memcpy(&rri->new_path[7], &rri->request_path[2], rri->request_path_size - 2);
    rri->new_path_size = rri->request_path_size + 5;
    rri->new_path[rri->new_path_size] = 0;

    retcode = 7;                // 0x1 - host, 0x2 - host, 0x4 - path
  }

  return retcode;
}

/* ----------------------- tsremap_os_response ----------------------------- */
void
tsremap_os_response(ihandle ih, rhandle rh, int os_response_type)
{
  int request_id;
  INKHttpTxnGetArg((INKHttpTxn) rh, 1, (void **) &request_id);  // read counter (we store it in tsremap_remap function call)
  fprintf(stderr, "[tsremap_os_response] Read processing counter %d from request processing block\n", request_id);
  fprintf(stderr, "[tsremap_os_response] OS response status: %d\n", os_response_type);
}
