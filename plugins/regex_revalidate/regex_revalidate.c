/** @file

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

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#define LOG_PREFIX "regex_revalidate"
#define CONFIG_TMOUT 60000
#define FREE_TMOUT 300000
#define OVECTOR_SIZE 30
#define LOG_ROLL_INTERVAL 86400
#define LOG_ROLL_OFFSET 0

static inline void *
ts_malloc(size_t s)
{
  return TSmalloc(s);
}

static inline void
ts_free(void *s)
{
  return TSfree(s);
}

typedef struct invalidate_t {
  const char *regex_text;
  pcre *regex;
  pcre_extra *regex_extra;
  time_t epoch;
  time_t expiry;
  struct invalidate_t *next;
} invalidate_t;

typedef struct {
  invalidate_t *invalidate_list;
  char *config_file;
  time_t last_load;
  TSTextLogObject log;
} plugin_state_t;

static invalidate_t *
init_invalidate_t(invalidate_t *i)
{
  i->regex_text  = NULL;
  i->regex       = NULL;
  i->regex_extra = NULL;
  i->epoch       = 0;
  i->expiry      = 0;
  i->next        = NULL;
  return i;
}

static void
free_invalidate_t(invalidate_t *i)
{
  if (i->regex_extra) {
#ifndef PCRE_STUDY_JIT_COMPILE
    pcre_free(i->regex_extra);
#else
    pcre_free_study(i->regex_extra);
#endif
  }
  if (i->regex) {
    pcre_free(i->regex);
  }
  if (i->regex_text) {
    pcre_free_substring(i->regex_text);
  }
  TSfree(i);
}

static void
free_invalidate_t_list(invalidate_t *i)
{
  if (i->next) {
    free_invalidate_t_list(i->next);
  }
  free_invalidate_t(i);
}

static plugin_state_t *
init_plugin_state_t(plugin_state_t *pstate)
{
  pstate->invalidate_list = NULL;
  pstate->config_file     = NULL;
  pstate->last_load       = 0;
  pstate->log             = NULL;
  return pstate;
}

static void
free_plugin_state_t(plugin_state_t *pstate)
{
  if (pstate->invalidate_list) {
    free_invalidate_t_list(pstate->invalidate_list);
  }
  if (pstate->config_file) {
    TSfree(pstate->config_file);
  }
  if (pstate->log) {
    TSTextLogObjectDestroy(pstate->log);
  }
  TSfree(pstate);
}

static invalidate_t *
copy_invalidate_t(invalidate_t *i)
{
  invalidate_t *iptr;
  const char *errptr;
  int erroffset;

  iptr              = (invalidate_t *)TSmalloc(sizeof(invalidate_t));
  iptr->regex_text  = TSstrdup(i->regex_text);
  iptr->regex       = pcre_compile(iptr->regex_text, 0, &errptr, &erroffset, NULL); // There is no pcre_copy :-(
  iptr->regex_extra = pcre_study(iptr->regex, 0, &errptr); // Assuming no errors since this worked before :-/
  iptr->epoch       = i->epoch;
  iptr->expiry      = i->expiry;
  iptr->next        = NULL;
  return iptr;
}

static invalidate_t *
copy_config(invalidate_t *old_list)
{
  invalidate_t *new_list = NULL;
  invalidate_t *iptr_old, *iptr_new;

  if (old_list) {
    new_list = copy_invalidate_t(old_list);
    iptr_old = old_list->next;
    iptr_new = new_list;
    while (iptr_old) {
      iptr_new->next = copy_invalidate_t(iptr_old);
      iptr_new       = iptr_new->next;
      iptr_old       = iptr_old->next;
    }
  }

  return new_list;
}

static bool
prune_config(invalidate_t **i)
{
  invalidate_t *iptr, *ilast;
  time_t now;
  bool pruned = false;

  now = time(NULL);

  if (*i) {
    iptr  = *i;
    ilast = NULL;
    while (iptr) {
      if (difftime(iptr->expiry, now) < 0) {
        TSDebug(LOG_PREFIX, "Removing %s expiry: %d now: %d", iptr->regex_text, (int)iptr->expiry, (int)now);
        if (ilast) {
          ilast->next = iptr->next;
          free_invalidate_t(iptr);
          iptr = ilast->next;
        } else {
          *i = iptr->next;
          free_invalidate_t(iptr);
          iptr = *i;
        }
        pruned = true;
      } else {
        ilast = iptr;
        iptr  = iptr->next;
      }
    }
  }
  return pruned;
}

static bool
load_config(plugin_state_t *pstate, invalidate_t **ilist)
{
  FILE *fs;
  struct stat s;
  size_t path_len;
  char *path;
  char line[LINE_MAX];
  time_t now;
  pcre *config_re;
  const char *errptr;
  int erroffset, ovector[OVECTOR_SIZE], rc;
  int ln = 0;
  invalidate_t *iptr, *i;

  if (pstate->config_file[0] != '/') {
    path_len = strlen(TSConfigDirGet()) + strlen(pstate->config_file) + 2;
    path     = alloca(path_len);
    snprintf(path, path_len, "%s/%s", TSConfigDirGet(), pstate->config_file);
  } else {
    path = pstate->config_file;
  }
  if (stat(path, &s) < 0) {
    TSDebug(LOG_PREFIX, "Could not stat %s", path);
    return false;
  }
  if (s.st_mtime > pstate->last_load) {
    now = time(NULL);
    if (!(fs = fopen(path, "r"))) {
      TSDebug(LOG_PREFIX, "Could not open %s for reading", path);
      return false;
    }
    config_re = pcre_compile("^([^#].+?)\\s+(\\d+)\\s*$", 0, &errptr, &erroffset, NULL);
    while (fgets(line, LINE_MAX, fs) != NULL) {
      ln++;
      TSDebug(LOG_PREFIX, "Processing: %d %s", ln, line);
      rc = pcre_exec(config_re, NULL, line, strlen(line), 0, 0, ovector, OVECTOR_SIZE);
      if (rc == 3) {
        i = (invalidate_t *)TSmalloc(sizeof(invalidate_t));
        init_invalidate_t(i);
        pcre_get_substring(line, ovector, rc, 1, &i->regex_text);
        i->epoch  = now;
        i->expiry = atoi(line + ovector[4]);
        i->regex  = pcre_compile(i->regex_text, 0, &errptr, &erroffset, NULL);
        if (i->expiry <= i->epoch) {
          TSDebug(LOG_PREFIX, "Rule is already expired!");
          free_invalidate_t(i);
        } else if (i->regex == NULL) {
          TSDebug(LOG_PREFIX, "%s did not compile", i->regex_text);
          free_invalidate_t(i);
        } else {
          i->regex_extra = pcre_study(i->regex, 0, &errptr);
          if (!*ilist) {
            *ilist = i;
            TSDebug(LOG_PREFIX, "Created new list and Loaded %s %d %d", i->regex_text, (int)i->epoch, (int)i->expiry);
          } else {
            iptr = *ilist;
            while (1) {
              if (strcmp(i->regex_text, iptr->regex_text) == 0) {
                if (iptr->expiry != i->expiry) {
                  TSDebug(LOG_PREFIX, "Updating duplicate %s", i->regex_text);
                  iptr->epoch  = i->epoch;
                  iptr->expiry = i->expiry;
                }
                free_invalidate_t(i);
                i = NULL;
                break;
              } else if (!iptr->next) {
                break;
              } else {
                iptr = iptr->next;
              }
            }
            if (i) {
              iptr->next = i;
              TSDebug(LOG_PREFIX, "Loaded %s %d %d", i->regex_text, (int)i->epoch, (int)i->expiry);
            }
          }
        }
      } else {
        TSDebug(LOG_PREFIX, "Skipping line %d", ln);
      }
    }
    pcre_free(config_re);
    fclose(fs);
    pstate->last_load = s.st_mtime;
    return true;
  } else {
    TSDebug(LOG_PREFIX, "File mod time is not newer: %d >= %d", (int)pstate->last_load, (int)s.st_mtime);
  }
  return false;
}

static void
list_config(plugin_state_t *pstate, invalidate_t *i)
{
  invalidate_t *iptr;

  TSDebug(LOG_PREFIX, "Current config:");
  if (pstate->log) {
    TSTextLogObjectWrite(pstate->log, "Current config:");
  }
  if (i) {
    iptr = i;
    while (iptr) {
      TSDebug(LOG_PREFIX, "%s epoch: %d expiry: %d", iptr->regex_text, (int)iptr->epoch, (int)iptr->expiry);
      if (pstate->log) {
        TSTextLogObjectWrite(pstate->log, "%s epoch: %d expiry: %d", iptr->regex_text, (int)iptr->epoch, (int)iptr->expiry);
      }
      iptr = iptr->next;
    }
  } else {
    TSDebug(LOG_PREFIX, "EMPTY");
    if (pstate->log) {
      TSTextLogObjectWrite(pstate->log, "EMPTY");
    }
  }
}

static int
free_handler(TSCont cont, TSEvent event, void *edata)
{
  invalidate_t *iptr;

  TSDebug(LOG_PREFIX, "Freeing old config");
  iptr = (invalidate_t *)TSContDataGet(cont);
  free_invalidate_t_list(iptr);
  TSContDestroy(cont);
  return 0;
}

static int
config_handler(TSCont cont, TSEvent event, void *edata)
{
  plugin_state_t *pstate;
  invalidate_t *i, *iptr;
  TSCont free_cont;
  bool updated;
  TSMutex mutex;

  mutex = TSContMutexGet(cont);
  TSMutexLock(mutex);

  TSDebug(LOG_PREFIX, "In config Handler");
  pstate = (plugin_state_t *)TSContDataGet(cont);
  i      = copy_config(pstate->invalidate_list);

  updated = prune_config(&i);
  updated = load_config(pstate, &i) || updated;

  if (updated) {
    list_config(pstate, i);
    iptr = __sync_val_compare_and_swap(&(pstate->invalidate_list), pstate->invalidate_list, i);

    if (iptr) {
      free_cont = TSContCreate(free_handler, TSMutexCreate());
      TSContDataSet(free_cont, (void *)iptr);
      TSContScheduleOnPool(free_cont, FREE_TMOUT, TS_THREAD_POOL_TASK);
    }
  } else {
    TSDebug(LOG_PREFIX, "No Changes");
    if (i) {
      free_invalidate_t_list(i);
    }
  }

  TSMutexUnlock(mutex);

  // Don't reschedule for TS_EVENT_MGMT_UPDATE
  if (event == TS_EVENT_TIMEOUT) {
    TSContScheduleOnPool(cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);
  }
  return 0;
}

static time_t
get_date_from_cached_hdr(TSHttpTxn txn)
{
  TSMBuffer buf;
  TSMLoc hdr_loc, date_loc;
  time_t date = 0;

  if (TSHttpTxnCachedRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
    date_loc = TSMimeHdrFieldFind(buf, hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (date_loc != TS_NULL_MLOC) {
      date = TSMimeHdrFieldValueDateGet(buf, hdr_loc, date_loc);
      TSHandleMLocRelease(buf, hdr_loc, date_loc);
    }
    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
  }

  return date;
}

static int
main_handler(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  int status;
  invalidate_t *iptr;
  plugin_state_t *pstate;

  time_t date = 0, now = 0;
  char *url   = NULL;
  int url_len = 0;

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_SUCCESS) {
      if (status == TS_CACHE_LOOKUP_HIT_FRESH) {
        pstate = (plugin_state_t *)TSContDataGet(cont);
        iptr   = pstate->invalidate_list;
        while (iptr) {
          if (!date) {
            date = get_date_from_cached_hdr(txn);
            now  = time(NULL);
          }
          if ((difftime(iptr->epoch, date) >= 0) && (difftime(iptr->expiry, now) >= 0)) {
            if (!url) {
              url = TSHttpTxnEffectiveUrlStringGet(txn, &url_len);
            }
            if (pcre_exec(iptr->regex, iptr->regex_extra, url, url_len, 0, 0, NULL, 0) >= 0) {
              TSHttpTxnCacheLookupStatusSet(txn, TS_CACHE_LOOKUP_HIT_STALE);
              iptr = NULL;
              TSDebug(LOG_PREFIX, "Forced revalidate - %.*s", url_len, url);
            }
          }
          if (iptr) {
            iptr = iptr->next;
          }
        }
        if (url) {
          TSfree(url);
        }
      }
    }
    break;
  default:
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static bool
check_ts_version()
{
  const char *ts_version = TSTrafficServerVersionGet();

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int micro_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &micro_ts_version) != 3) {
      return false;
    }

    if ((TS_VERSION_MAJOR == major_ts_version) && (TS_VERSION_MINOR == minor_ts_version) &&
        (TS_VERSION_MICRO == micro_ts_version)) {
      return true;
    }
  }

  return false;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont main_cont, config_cont;
  plugin_state_t *pstate;
  invalidate_t *iptr        = NULL;
  bool disable_timed_reload = false;

  TSDebug(LOG_PREFIX, "Starting plugin init");

  pstate = (plugin_state_t *)TSmalloc(sizeof(plugin_state_t));
  init_plugin_state_t(pstate);

  int c;
  static const struct option longopts[] = {{"config", required_argument, NULL, 'c'},
                                           {"log", required_argument, NULL, 'l'},
                                           {"disable-timed-reload", no_argument, NULL, 'd'},
                                           {NULL, 0, NULL, 0}};

  while ((c = getopt_long(argc, (char *const *)argv, "c:l:", longopts, NULL)) != -1) {
    switch (c) {
    case 'c':
      pstate->config_file = TSstrdup(optarg);
      break;
    case 'l':
      if (TS_SUCCESS == TSTextLogObjectCreate(optarg, TS_LOG_MODE_ADD_TIMESTAMP, &pstate->log)) {
        TSTextLogObjectRollingIntervalSecSet(pstate->log, LOG_ROLL_INTERVAL);
        TSTextLogObjectRollingOffsetHrSet(pstate->log, LOG_ROLL_OFFSET);
      }
      break;
    case 'd':
      disable_timed_reload = true;
      break;
    default:
      break;
    }
  }

  if (!pstate->config_file) {
    TSError("[regex_revalidate] Plugin requires a --config option along with a config file name");
    free_plugin_state_t(pstate);
    return;
  }

  if (!load_config(pstate, &iptr)) {
    TSDebug(LOG_PREFIX, "Problem loading config from file %s", pstate->config_file);
  } else {
    pstate->invalidate_list = iptr;
    list_config(pstate, iptr);
  }

  info.plugin_name   = LOG_PREFIX;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[regex_revalidate] Plugin registration failed");

    free_plugin_state_t(pstate);
    return;
  } else {
    TSDebug(LOG_PREFIX, "Plugin registration succeeded");
  }

  if (!check_ts_version()) {
    TSError("[regex_revalidate] Plugin requires Traffic Server %d.%d.%d", TS_VERSION_MAJOR, TS_VERSION_MINOR, TS_VERSION_MICRO);
    free_plugin_state_t(pstate);
    return;
  }

  pcre_malloc = &ts_malloc;
  pcre_free   = &ts_free;

  main_cont = TSContCreate(main_handler, NULL);
  TSContDataSet(main_cont, (void *)pstate);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, main_cont);

  config_cont = TSContCreate(config_handler, TSMutexCreate());
  TSContDataSet(config_cont, (void *)pstate);

  TSMgmtUpdateRegister(config_cont, LOG_PREFIX);

  if (!disable_timed_reload) {
    TSContScheduleOnPool(config_cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);
  }

  TSDebug(LOG_PREFIX, "Plugin Init Complete");
}
