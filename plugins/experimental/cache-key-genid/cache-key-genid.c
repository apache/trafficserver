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
#include "/opt/kyotocabinet/include/kclangc.h"

#define PLUGIN_NAME "cache-key-genid"
#define PLUGIN_VERSION "1.0.6"
#define VENDOR_NAME "GoDaddy.com, LLC"
#define VENDOR_SUPPORT_EMAIL "support@godaddy.com"

static char genid_kyoto_db[1024];

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
    *host = calloc(num, pt2 - pt1 + 1); // +1 for null term?
    host_len = pt2 - pt1;
    strncpy(*host, pt1, host_len);
  }
}

// create a new string from url, injecting gen_id, so http://foo.com/s.css becomes http://foo.com.7/s.css
static char *
get_genid_newurl(char *url, char *host, int gen_id)
{
  char *pt1;
  char *pt2;
  char *newurl;
  size_t newurl_len;
  unsigned num = 1;

  pt1 = strstr(url, host) + strlen(host);
  pt2 = pt1;
  // newurl_len = strlen(url) + ceil(log10(gen_id+1)) + 7; // 6 for '/GeNiD' and 1 for '\0'
  newurl_len = strlen(url) + ceil(log10(gen_id + 1)) + 2; // 1 for '.' and 1 for '\0'
  newurl = calloc(num, newurl_len * sizeof(char));
  // This injects it into the host:
  strncpy(newurl, url, pt1 - url);
  pt1 = newurl + strlen(newurl);
  sprintf(pt1, ".%d", gen_id);
  pt1 = newurl + strlen(newurl);
  strcpy(pt1, pt2);
  return newurl;
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
  int answer;
  int host_size;

  /* create the database object */
  db = kcdbnew();
  /* open the database */
  if (!kcdbopen(db, genid_kyoto_db, KCOREADER | KCONOLOCK)) {
    TSDebug(PLUGIN_NAME, "could not open the genid database %s\n", genid_kyoto_db);
    TSError("[%s] could not open the genid database\n", PLUGIN_NAME);
    return 0;
  }
  vbuf = kcdbget(db, host, strlen(host), &vsiz);
  if (vbuf) {
    TSDebug(PLUGIN_NAME, "kcdbget(%s) = %s\n", host, vbuf);
    answer = (int)strtol(vbuf, NULL, 10);
    kcfree(vbuf);
    kcdbclose(db);
    return answer;
  } else {
    // do I really want to set a record here?  This will make the db very large.
    // Will large volumes of lookups on records that don't exist be slower than
    // looking up records that do?  Probably not, I think it can instantly know.
    // Also, opening the DB in  'KCOREADER | KCONOLOCK' mode should be faster and
    // possible if not writing to the database
    // kcdbset(db, host, 3, "0", 3);
    // TSDebug(PLUGIN_NAME, "kcdbset(%s, 0)\n", host);
    host_size = strlen(host);
    TSDebug(PLUGIN_NAME, "kcdbget(%s) - no record found, len(%d)\n", host, host_size);
  }
  kcdbclose(db);
  return 0;
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
  char *newurl = 0;
  int gen_id;

  char *host = 0;

  char *url;
  int url_length;
  // size_t newurl_len;
  // unsigned num=1;
  int ok = 1;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(PLUGIN_NAME, "v%s\n", PLUGIN_VERSION);
    if (ok) {
      url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
      if (!url) {
        TSError("[%s] could not retrieve request url\n", PLUGIN_NAME);
        ok = 0;
      }
    }
    if (ok) {
      get_genid_host(&host, url);
      if (!host) {
        TSError("[%s] could not retrieve request host\n", PLUGIN_NAME);
        ok = 0;
      }
    }
    if (ok) {
      TSDebug(PLUGIN_NAME, "From url (%s) discovered host (%s)\n", url, host);
      gen_id = get_genid(host);
      if (gen_id) {
        newurl = get_genid_newurl(url, host, gen_id);
        // newurl_len = strlen(url) + 6 + ceil(log10(gen_id+1)) + 1; // URL + '/gEnId' + gen_id + '\0'
        // newurl = calloc(num, newurl_len * sizeof(char));
        // sprintf(newurl, "%s/gEnId%d", url, gen_id);
      }
      if (newurl) {
        TSDebug(PLUGIN_NAME, "Rewriting cache URL for %s to %s\n", url, newurl);
        if (TSCacheUrlSet(txnp, newurl, strlen(newurl)) != TS_SUCCESS) {
          TSDebug(PLUGIN_NAME, "Error, unable to modify cache url\n");
          TSError("[%s] Unable to modify cache url from %s to %s\n", PLUGIN_NAME, url, newurl);
          ok = 0;
        }
      }
    }
    /* Clean up */
    if (url)
      TSfree(url);
    if (newurl)
      TSfree(newurl);
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
  // KCDB* db;

  info.plugin_name = PLUGIN_NAME;
  info.vendor_name = VENDOR_NAME;
  info.support_email = VENDOR_SUPPORT_EMAIL;

  if (argc > 1 && strlen(argv[1]) < 1024) {
    strcpy(genid_kyoto_db, argv[1]);
    /*
    db = kcdbnew();
    if (!kcdbopen(db, genid_kyoto_db, KCOWRITER | KCOCREATE)) {
        TSError("[%s] plugin registration failed. Could not open %s", PLUGIN_NAME, genid_kyoto_db);
        return;
    }
    kcdbclose(db);
    kcdbdel(db);
    */
  } else {
    TSError("[%s] plugin registration failed. check argv[1] for db path", PLUGIN_NAME);
    return;
  }

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed.  check version.", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate((TSEventFunc)handle_hook, NULL));
}
