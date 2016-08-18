/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* cache-key-genid.c - Plugin to modify the URL used as a cache key for
 * requests, without modifying the URL used for actually fetching data from
 * the origin server.
 */

#include <ts/ts.h>
#include <stdio.h>
#include <string.h>
#include "kclangc.h"

#define PLUGIN_NAME "cache-key-genid"

static char genid_kyoto_db[PATH_MAX + 1];

// Find the host in url and set host to it
static void
get_genid_host(char **host, char *url)
{
  char *pt1;
  char *pt2;
  size_t host_len;
  unsigned num = 1;

  pt1 = strstr(url, "//");

  if (pt1) {
    pt1 = pt1 + 2;
    pt2 = strstr(pt1, "/");
  }

  if (pt1 && pt2 && pt2 > pt1) {
    host_len = pt2 - pt1;
    *host    = calloc(num, host_len + 1);
    strncpy(*host, pt1, host_len);
  }
}

/* get_genid
 * Looks up the host's genid in the host->genid database
 */
static int
get_genid(char *host)
{
  KCDB *db;
  char *vbuf;
  size_t vsiz;
  int answer = 0;
  int host_size;

  /* create the database object */
  db = kcdbnew();

  /* open the database */
  if (!kcdbopen(db, genid_kyoto_db, KCOREADER | KCONOLOCK)) {
    TSDebug(PLUGIN_NAME, "could not open the genid database %s", genid_kyoto_db);
    TSError("[%s] could not open the genid database %s: %s", PLUGIN_NAME, genid_kyoto_db, strerror(errno));
    return 0;
  }

  vbuf = kcdbget(db, host, strlen(host), &vsiz);

  if (vbuf) {
    TSDebug(PLUGIN_NAME, "kcdbget(%s) = %s", host, vbuf);
    answer = (int)strtol(vbuf, NULL, 10);
    kcfree(vbuf);
  } else {
    host_size = strlen(host);
    TSDebug(PLUGIN_NAME, "kcdbget(%s) - no record found, len(%d)", host, host_size);
    answer = 0;
  }

  kcdbclose(db);
  return answer;
}

/* handle_hook
 * Fires on TS_EVENT_HTTP_READ_REQUEST_HDR events, gets the effectiveUrl
 * finds the host, gets the generation ID, gen_id, for the host
 * and runs TSCacheUrlSet to change the cache key for the read
 */
static int
handle_hook(TSCont *contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  char *url = NULL, *host = NULL;
  int url_length;
  int gen_id;
  int ok = 1;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(PLUGIN_NAME, "handling TS_EVENT_HTTP_READ_REQUEST_HDR");

    if (ok) {
      url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
      if (!url) {
        TSError("[%s] could not retrieve request url", PLUGIN_NAME);
        ok = 0;
      }
    }

    if (ok) {
      get_genid_host(&host, url);
      if (!host) {
        TSError("[%s] could not retrieve request host", PLUGIN_NAME);
        ok = 0;
      }
    }

    if (ok) {
      TSDebug(PLUGIN_NAME, "From url (%s) discovered host (%s)", url, host);
      if ((gen_id = get_genid(host)) != 0) {
        if (TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_CACHE_GENERATION, gen_id) != TS_SUCCESS) {
          TSDebug(PLUGIN_NAME, "Error, unable to modify cache url");
          TSError("[%s] Unable to set cache generation for %s to %d", PLUGIN_NAME, url, gen_id);
          ok = 0;
        }
      }
    }

    /* Clean up */
    if (url)
      TSfree(url);
    if (host)
      TSfree(host);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  default:
    TSAssert(!"Unexpected event");
    ok = 0;
    break;
  }

  return ok;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (argc > 1) {
    TSstrlcpy(genid_kyoto_db, argv[1], sizeof(genid_kyoto_db));
  } else {
    TSError("[%s] plugin registration failed. check argv[1] for db path", PLUGIN_NAME);
    return;
  }

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed.  check version.", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate((TSEventFunc)handle_hook, NULL));
}
