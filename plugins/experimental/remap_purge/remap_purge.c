/** @file

  Per-remap purge RESTful API for stateful generation ID management.

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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "tscore/ink_defs.h"

static const char *PLUGIN_NAME = "remap_purge";
static const char *DEFAULT_DIR = "var/trafficserver"; /* Not perfect, but no better API) */

typedef struct PurgeInstance_t {
  char *id;
  char *secret;
  int secret_len;
  char *header;
  int header_len;
  char *state_file;
  bool allow_get;
  int64_t gen_id;
  TSMutex lock;
} PurgeInstance;

static char *
make_state_path(const char *filename)
{
  if ('/' == *filename) {
    return TSstrdup(filename);
  } else {
    char buf[8192];
    const char *dir = TSInstallDirGet();

    snprintf(buf, sizeof(buf), "%s/%s/%s", dir, DEFAULT_DIR, PLUGIN_NAME);
    if (-1 == mkdir(buf, S_IRWXU)) {
      if (EEXIST != errno) {
        TSError("[%s] Unable to create directory %s: %s (%d)", PLUGIN_NAME, buf, strerror(errno), errno);
        return NULL;
      }
    }
    snprintf(buf, sizeof(buf), "%s/%s/%s/%s.genid", dir, DEFAULT_DIR, PLUGIN_NAME, filename);
    return TSstrdup(buf);
  }

  return NULL;
}

/* Constructor and destructor for the PurgeInstance */
static void
init_purge_instance(PurgeInstance *purge)
{
  FILE *file = fopen(purge->state_file, "r");

  if (file) {
    if (fscanf(file, "%" PRId64 "", &purge->gen_id) > 0) {
      TSDebug(PLUGIN_NAME, "Read genID from %s for %s", purge->state_file, purge->id);
    }
    fclose(file);
  } else {
    TSError("[%s] Can not open file %s: %s (%d)", PLUGIN_NAME, purge->state_file, strerror(errno), errno);
  }

  purge->lock = TSMutexCreate();
}

static void
delete_purge_instance(PurgeInstance *purge)
{
  if (purge) {
    TSfree(purge->id);
    TSfree(purge->state_file);
    TSfree(purge->secret);
    TSfree(purge->header);
    TSMutexDestroy(purge->lock);
    TSfree(purge);
  }
}

/* This is where we start the PURGE events, setting up the transactino to fail,
   and bump the generation ID, and finally save the state. */
static int
on_http_cache_lookup_complete(TSHttpTxn txnp, TSCont contp, PurgeInstance *purge)
{
  FILE *file;

  TSMutexLock(purge->lock);

  ++purge->gen_id;
  TSDebug(PLUGIN_NAME, "Bumping the Generation ID to %" PRId64 " for %s", purge->gen_id, purge->id);

  if ((file = fopen(purge->state_file, "w"))) {
    TSDebug(PLUGIN_NAME, "\tsaving state to %s", purge->state_file);
    fprintf(file, "%" PRId64 "", purge->gen_id);
    fclose(file);
  } else {
    TSError("[%s] Unable to save state to file %s: errno=%d", PLUGIN_NAME, purge->state_file, errno);
  }

  TSMutexUnlock(purge->lock);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  return TS_SUCCESS;
}

/* Before we can send the response, we want to modify it to a "200 OK" again,
   and produce some reasonable body output. */
static int
on_send_response_header(TSHttpTxn txnp, TSCont contp, PurgeInstance *purge)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  TSDebug(PLUGIN_NAME, "Fixing up the response on the successful PURGE");
  if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    char response[1024];
    int len = snprintf(response, sizeof(response), "PURGED %s\r\n\r\n", purge->id);

    TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_OK);
    TSHttpHdrReasonSet(bufp, hdr_loc, "OK", 2);
    TSHttpTxnErrorBodySet(txnp, TSstrdup(response), len >= (int)sizeof(response) ? (int)sizeof(response) - 1 : len, NULL);

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } else {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  }

  return TS_SUCCESS;
}

/* This is the main continuation, triggered after DoRemap has decided we should
   handle this request internally. */
static int
purge_cont(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp       = (TSHttpTxn)edata;
  PurgeInstance *purge = (PurgeInstance *)TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    return on_send_response_header(txnp, contp, purge);
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    return on_http_cache_lookup_complete(txnp, contp, purge);
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unexpected event: %d", event);
    break;
  }

  return TS_SUCCESS;
}

static void
handle_purge(TSHttpTxn txnp, PurgeInstance *purge)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc = NULL, url_loc = NULL;
  bool should_purge = false;

  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc)) {
    int method_len     = 0;
    const char *method = TSHttpHdrMethodGet(reqp, hdr_loc, &method_len);

    if ((TS_HTTP_METHOD_PURGE == method) || ((TS_HTTP_METHOD_GET == method) && purge->allow_get)) {
      /* First see if we require the "secret" to be passed in a header, and then use that */
      if (purge->header) {
        TSMLoc field_loc = TSMimeHdrFieldFind(reqp, hdr_loc, purge->header, purge->header_len);

        if (field_loc) {
          const char *header;
          int header_len;

          header = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field_loc, -1, &header_len);
          TSDebug(PLUGIN_NAME, "Checking for %.*s == %s ?", header_len, header, purge->secret);
          if (header && (header_len == purge->secret_len) && !memcmp(header, purge->secret, header_len)) {
            should_purge = true;
          }
          TSHandleMLocRelease(reqp, hdr_loc, field_loc);
        }
      } else {
        /* We are matching on the path component instead of a header */
        if (TS_SUCCESS == TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc)) {
          int path_len     = 0;
          const char *path = TSUrlPathGet(reqp, url_loc, &path_len);

          TSDebug(PLUGIN_NAME, "Checking PATH = %.*s", path_len, path);
          if (path && (path_len >= purge->secret_len)) {
            int s_path = path_len - 1;

            while ((s_path >= 0) && ('/' != path[s_path])) { /* No memrchr in OSX */
              --s_path;
            }

            if (!memcmp(s_path > 0 ? path + s_path + 1 : path, purge->secret, purge->secret_len)) {
              should_purge = true;
            }
          }
          TSHandleMLocRelease(reqp, hdr_loc, url_loc);
        }
      }
    }
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  /* Setup the continuation to handle this request if appropriate, if not, set the GenID if needed */
  if (should_purge) {
    TSCont cont = TSContCreate(purge_cont, TSMutexCreate());

    TSContDataSet(cont, purge);
    TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  } else if (purge->gen_id > 0) {
    TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_CACHE_GENERATION, purge->gen_id);
  }
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  char *id                             = argv[0]; /* The ID is default to the "from" URL, so save it */
  PurgeInstance *purge                 = TSmalloc(sizeof(PurgeInstance));
  static const struct option longopt[] = {
    {(char *)"id", required_argument, NULL, 'i'},     {(char *)"secret", required_argument, NULL, 's'},
    {(char *)"header", required_argument, NULL, 'h'}, {(char *)"state-file", required_argument, NULL, 'f'},
    {(char *)"allow-get", no_argument, NULL, 'a'},    {NULL, no_argument, NULL, '\0'},
  };

  memset(purge, 0, sizeof(PurgeInstance));

  // The first two arguments are the "from" and "to" URL string. We need to
  // skip them, but we also require that there be an option to masquerade as
  // argv[0], so we increment the argument indexes by 1 rather than by 2.
  argc--;
  argv++;

  for (;;) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);

    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'a':
      purge->allow_get = true;
      break;
    case 'h':
      purge->header     = TSstrdup(optarg);
      purge->header_len = strlen(purge->header);
      break;
    case 'i':
      purge->id = TSstrdup(optarg);
      break;
    case 's':
      purge->secret     = TSstrdup(optarg);
      purge->secret_len = strlen(purge->secret);
      break;
    case 'f':
      purge->state_file = make_state_path(optarg);
      break;
    }
  }

  if ((NULL == purge->secret) || (NULL == purge->state_file) || !purge->secret_len) {
    TSError("[%s] Unable to create remap instance, need at least a secret (--secret) and state (--state_file)", PLUGIN_NAME);
    delete_purge_instance(purge);
    return TS_ERROR;
  } else {
    if (!purge->id) {
      purge->id = TSstrdup(id);
    }
    init_purge_instance(purge);
    *ih = (void *)purge;
    return TS_SUCCESS;
  }
}

void
TSRemapDeleteInstance(void *ih)
{
  PurgeInstance *purge = (PurgeInstance *)ih;

  delete_purge_instance(purge);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  PurgeInstance *purge = (PurgeInstance *)ih;

  handle_purge(txnp, purge);
  return TSREMAP_NO_REMAP; // This plugin never rewrites anything.
}
