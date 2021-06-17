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

#define CONFIG_TMOUT 60000
#define FREE_TMOUT 300000
#define OVECTOR_SIZE 30
#define LOG_ROLL_INTERVAL 86400
#define LOG_ROLL_OFFSET 0

static const char *const PLUGIN_NAME = "regex_revalidate";
static const char *const DEFAULT_DIR = "var/trafficserver"; /* Not perfect, but no better API) */

static char const *const RESULT_MISS    = "MISS";
static char const *const RESULT_STALE   = "STALE";
static char const *const RESULT_UNKNOWN = "UNKNOWN";

static int stat_id_stale                 = TS_ERROR;
static char const *const stat_name_stale = "plugin.regex_revalidate.stale";
static int stat_id_miss                  = TS_ERROR;
static char const *const stat_name_miss  = "plugin.regex_revalidate.miss";

static void
create_stats()
{
  if (TS_ERROR == stat_id_stale && TS_ERROR == TSStatFindName(stat_name_stale, &stat_id_stale)) {
    stat_id_stale = TSStatCreate(stat_name_stale, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
    if (TS_ERROR != stat_id_stale) {
      TSDebug(PLUGIN_NAME, "Created stat '%s'", stat_name_stale);
    }
  }

  if (TS_ERROR == stat_id_miss && TS_ERROR == TSStatFindName(stat_name_miss, &stat_id_miss)) {
    stat_id_miss = TSStatCreate(stat_name_miss, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_COUNT);
    if (TS_ERROR != stat_id_miss) {
      TSDebug(PLUGIN_NAME, "Created stat '%s'", stat_name_miss);
    }
  }
}

static void
increment_stat(TSCacheLookupResult const result)
{
  switch (result) {
  case TS_CACHE_LOOKUP_MISS:
    if (TS_ERROR != stat_id_miss) {
      TSStatIntIncrement(stat_id_miss, 1);
      TSDebug(PLUGIN_NAME, "Incrementing stat '%s'", stat_name_miss);
    }
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    if (TS_ERROR != stat_id_stale) {
      TSStatIntIncrement(stat_id_stale, 1);
      TSDebug(PLUGIN_NAME, "Incrementing stat '%s'", stat_name_stale);
    }
    break;
  default:
    break;
  }
}

static char const *const
strForResult(TSCacheLookupResult const result)
{
  switch (result) {
  case TS_CACHE_LOOKUP_MISS:
    return RESULT_MISS;
    break;
  case TS_CACHE_LOOKUP_HIT_STALE:
    return RESULT_STALE;
    break;
  default:
    return RESULT_UNKNOWN;
    break;
  }
}

typedef struct invalidate_t {
  const char *regex_text;
  pcre *regex;
  pcre_extra *regex_extra;
  time_t epoch;
  time_t expiry;
  TSCacheLookupResult new_result;
  struct invalidate_t *next;
} invalidate_t;

typedef struct {
  invalidate_t *invalidate_list;
  char *config_path;
  time_t last_load;
  TSTextLogObject log;
  char *state_path;
} plugin_state_t;

static invalidate_t *
init_invalidate_t(invalidate_t *i)
{
  i->regex_text  = NULL;
  i->regex       = NULL;
  i->regex_extra = NULL;
  i->epoch       = 0;
  i->expiry      = 0;
  i->new_result  = TS_CACHE_LOOKUP_HIT_STALE;
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
free_invalidate_t_list(invalidate_t *iptr)
{
  while (NULL != iptr) {
    invalidate_t *const next = iptr->next;
    free_invalidate_t(iptr);
    iptr = next;
  }
}

static plugin_state_t *
init_plugin_state_t(plugin_state_t *pstate)
{
  pstate->invalidate_list = NULL;
  pstate->config_path     = NULL;
  pstate->last_load       = 0;
  pstate->log             = NULL;
  pstate->state_path      = NULL;
  return pstate;
}

static void
free_plugin_state_t(plugin_state_t *pstate)
{
  if (pstate->invalidate_list) {
    free_invalidate_t_list(pstate->invalidate_list);
  }
  if (pstate->config_path) {
    TSfree(pstate->config_path);
  }
  if (pstate->log) {
    TSTextLogObjectDestroy(pstate->log);
  }
  if (pstate->state_path) {
    TSfree(pstate->state_path);
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
  iptr->new_result  = i->new_result;
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
        TSDebug(PLUGIN_NAME, "Removing %s expiry: %d type: %s now: %d", iptr->regex_text, (int)iptr->expiry,
                strForResult(iptr->new_result), (int)now);
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
load_state(plugin_state_t *pstate, invalidate_t **ilist)
{
  FILE *fs = NULL;
  struct stat s;
  char line[LINE_MAX];
  time_t now;
  pcre *config_re = NULL;
  const char *errptr;
  int erroffset, ovector[OVECTOR_SIZE], rc;
  int ln = 0;

  if (!*ilist) {
    return true;
  }

  if (stat(pstate->state_path, &s) < 0) {
    TSDebug(PLUGIN_NAME, "Could not stat state %s", pstate->state_path);
    return false;
  }

  if (!(fs = fopen(pstate->state_path, "r"))) {
    TSDebug(PLUGIN_NAME, "Could not open state %s for reading", pstate->state_path);
    return false;
  }

  now = time(NULL);

  config_re = pcre_compile("^([^#].+?)\\s+(\\d+)\\s+(\\d+)\\s+(\\w+)\\s*$", 0, &errptr, &erroffset, NULL);
  TSAssert(NULL != config_re);

  while (fgets(line, LINE_MAX, fs) != NULL) {
    TSDebug(PLUGIN_NAME, "state: processing: %d %s", ln, line);
    ++ln;
    rc = pcre_exec(config_re, NULL, line, strlen(line), 0, 0, ovector, OVECTOR_SIZE);

    if (5 == rc) {
      invalidate_t *const inv = (invalidate_t *)TSmalloc(sizeof(invalidate_t));
      init_invalidate_t(inv);

      pcre_get_substring(line, ovector, rc, 1, &(inv->regex_text));
      inv->epoch  = atoi(line + ovector[4]);
      inv->expiry = atoi(line + ovector[6]);

      if (inv->expiry < now) {
        TSDebug(PLUGIN_NAME, "state: skipping expired : '%s'", inv->regex_text);
        free_invalidate_t(inv);
        continue;
      }

      int const len          = ovector[9] - ovector[8];
      char const *const type = line + ovector[8];

      if (0 == strncasecmp(type, RESULT_STALE, len)) {
        TSDebug(PLUGIN_NAME, "state: regex line set to result type %s: '%s'", RESULT_STALE, inv->regex_text);
      } else if (0 == strncasecmp(type, RESULT_MISS, len)) {
        TSDebug(PLUGIN_NAME, "state: regex line set to result type %s: '%s'", RESULT_MISS, inv->regex_text);
        inv->new_result = TS_CACHE_LOOKUP_MISS;
      } else {
        TSDebug(PLUGIN_NAME, "state: unknown regex line result type '%.*s', skipping '%s'", len, type, inv->regex_text);
      }

      // iterate through the loaded config and try to merge
      invalidate_t *iptr = *ilist;
      do {
        if (0 == strcmp(inv->regex_text, iptr->regex_text)) {
          if (iptr->expiry == inv->expiry && iptr->new_result == inv->new_result) {
            TSDebug(PLUGIN_NAME, "state: restoring epoch for %s", iptr->regex_text);
            iptr->epoch = inv->epoch;
          }
          break;
        }

        if (NULL == iptr->next) {
          break;
        }
        iptr = iptr->next;
      } while (NULL != iptr);

      free_invalidate_t(inv);
    } else {
      TSDebug(PLUGIN_NAME, "state: invalid line '%s'", line);
    }
  }

  pcre_free(config_re);
  fclose(fs);
  return true;
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

  if (pstate->config_path[0] != '/') {
    path_len = strlen(TSConfigDirGet()) + strlen(pstate->config_path) + 2;
    path     = alloca(path_len);
    snprintf(path, path_len, "%s/%s", TSConfigDirGet(), pstate->config_path);
  } else {
    path = pstate->config_path;
  }
  if (stat(path, &s) < 0) {
    TSDebug(PLUGIN_NAME, "Could not stat %s", path);
    return false;
  }
  if (pstate->last_load < s.st_mtime) {
    now = time(NULL);
    if (!(fs = fopen(path, "r"))) {
      TSDebug(PLUGIN_NAME, "Could not open %s for reading", path);
      return false;
    }
    config_re = pcre_compile("^([^#].+?)\\s+(\\d+)(\\s+(\\w+))?\\s*$", 0, &errptr, &erroffset, NULL);
    TSAssert(NULL != config_re);

    while (fgets(line, LINE_MAX, fs) != NULL) {
      TSDebug(PLUGIN_NAME, "Processing: %d %s", ln, line);
      ++ln;
      rc = pcre_exec(config_re, NULL, line, strlen(line), 0, 0, ovector, OVECTOR_SIZE);

      if (3 <= rc) {
        i = (invalidate_t *)TSmalloc(sizeof(invalidate_t));
        init_invalidate_t(i);
        pcre_get_substring(line, ovector, rc, 1, &i->regex_text);

        i->regex  = pcre_compile(i->regex_text, 0, &errptr, &erroffset, NULL);
        i->epoch  = now;
        i->expiry = atoi(line + ovector[4]);

        if (5 == rc) {
          int const len          = ovector[9] - ovector[8];
          char const *const type = line + ovector[8];
          if (0 == strncasecmp(type, RESULT_MISS, len)) {
            TSDebug(PLUGIN_NAME, "Regex line set to result type %s: '%s'", RESULT_MISS, i->regex_text);
            i->new_result = TS_CACHE_LOOKUP_MISS;
          } else if (0 != strncasecmp(type, RESULT_STALE, len)) {
            TSDebug(PLUGIN_NAME, "Unknown regex line result type '%s', using default '%s' '%s'", type, RESULT_STALE, i->regex_text);
          }
        }

        if (i->expiry <= i->epoch) {
          TSDebug(PLUGIN_NAME, "Rule is already expired!");
          free_invalidate_t(i);
          i = NULL;
        } else if (i->regex == NULL) {
          TSDebug(PLUGIN_NAME, "%s did not compile", i->regex_text);
          free_invalidate_t(i);
          i = NULL;
        } else {
          i->regex_extra = pcre_study(i->regex, 0, &errptr);
          if (!*ilist) {
            *ilist = i;
            TSDebug(PLUGIN_NAME, "Created new list and Loaded %s %d %d %s", i->regex_text, (int)i->epoch, (int)i->expiry,
                    strForResult(i->new_result));
          } else {
            iptr = *ilist;
            while (1) {
              if (strcmp(i->regex_text, iptr->regex_text) == 0) {
                if (iptr->expiry != i->expiry) {
                  TSDebug(PLUGIN_NAME, "Updating duplicate %s", i->regex_text);
                  iptr->epoch  = i->epoch;
                  iptr->expiry = i->expiry;
                }
                if (iptr->new_result != i->new_result) {
                  TSDebug(PLUGIN_NAME, "Resetting duplicate due to type change %s", i->regex_text);
                  iptr->new_result = i->new_result;
                  iptr->epoch      = now;
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
              TSDebug(PLUGIN_NAME, "Loaded %s %d %d %s", i->regex_text, (int)i->epoch, (int)i->expiry, strForResult(i->new_result));
            }
          }
        }
      } else {
        TSDebug(PLUGIN_NAME, "Skipping line %d", ln);
      }
    }
    pcre_free(config_re);
    fclose(fs);
    pstate->last_load = s.st_mtime;
    return true;
  } else {
    TSDebug(PLUGIN_NAME, "File mod time is not newer: %d >= %d", (int)pstate->last_load, (int)s.st_mtime);
  }
  return false;
}

static void
list_config(plugin_state_t *pstate, invalidate_t *i)
{
  invalidate_t *iptr;

  TSDebug(PLUGIN_NAME, "Current config:");
  if (pstate->log) {
    TSTextLogObjectWrite(pstate->log, "Current config:");
  }

  FILE *state_file = NULL;
  if (pstate->state_path) {
    state_file = fopen(pstate->state_path, "w");
    if (NULL == state_file) {
      TSDebug(PLUGIN_NAME, "Unable to open state file %s\n", pstate->state_path);
    }
  }

  if (i) {
    iptr = i;
    while (iptr) {
      char const *const typestr = strForResult(iptr->new_result);
      TSDebug(PLUGIN_NAME, "%s epoch: %d expiry: %d result: %s", iptr->regex_text, (int)iptr->epoch, (int)iptr->expiry, typestr);
      if (pstate->log) {
        TSTextLogObjectWrite(pstate->log, "%s epoch: %d expiry: %d result: %s", iptr->regex_text, (int)iptr->epoch,
                             (int)iptr->expiry, typestr);
      }
      if (state_file) {
        fprintf(state_file, "%s %d %d %s\n", iptr->regex_text, (int)iptr->epoch, (int)iptr->expiry, typestr);
      }
      iptr = iptr->next;
    }

  } else {
    TSDebug(PLUGIN_NAME, "EMPTY");
    if (pstate->log) {
      TSTextLogObjectWrite(pstate->log, "EMPTY");
    }
  }

  if (NULL != state_file) {
    fclose(state_file);
  }
}

static int
free_handler(TSCont cont, TSEvent event, void *edata)
{
  invalidate_t *iptr;

  TSDebug(PLUGIN_NAME, "Freeing old config");
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

  TSDebug(PLUGIN_NAME, "In config Handler");
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
    TSDebug(PLUGIN_NAME, "No Changes");
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
              TSHttpTxnCacheLookupStatusSet(txn, iptr->new_result);
              increment_stat(iptr->new_result);
              TSDebug(PLUGIN_NAME, "Forced revalidate - %.*s %s", url_len, url, strForResult(iptr->new_result));
              iptr = NULL;
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

static char *
make_state_path(const char *filename)
{
  if ('/' == *filename) {
    return TSstrdup(filename);
  } else {
    char buf[8192];
    const char *dir = TSInstallDirGet();
    snprintf(buf, sizeof(buf), "%s/%s/%s", dir, DEFAULT_DIR, filename);
    return TSstrdup(buf);
  }

  return NULL;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont main_cont, config_cont;
  plugin_state_t *pstate;
  invalidate_t *iptr        = NULL;
  bool disable_timed_reload = false;

  TSDebug(PLUGIN_NAME, "Starting plugin init");

  pstate = (plugin_state_t *)TSmalloc(sizeof(plugin_state_t));
  init_plugin_state_t(pstate);

  int c;
  static const struct option longopts[] = {{"config", required_argument, NULL, 'c'},
                                           {"log", required_argument, NULL, 'l'},
                                           {"disable-timed-reload", no_argument, NULL, 'd'},
                                           {"state-file", required_argument, NULL, 'f'},
                                           {NULL, 0, NULL, 0}};

  while ((c = getopt_long(argc, (char *const *)argv, "c:l:f:", longopts, NULL)) != -1) {
    switch (c) {
    case 'c':
      pstate->config_path = TSstrdup(optarg);
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
    case 'f':
      pstate->state_path = make_state_path(optarg);
    default:
      break;
    }
  }

  if (NULL == pstate->config_path) {
    TSError("[regex_revalidate] Plugin requires a --config option along with a config file name");
    free_plugin_state_t(pstate);
    return;
  }

  if (!load_config(pstate, &iptr)) {
    TSDebug(PLUGIN_NAME, "Problem loading config from file %s", pstate->config_path);
  } else {
    pstate->invalidate_list = iptr;

    /* Load and merge previous state if provided */
    if (NULL != pstate->state_path) {
      if (!load_state(pstate, &iptr)) {
        TSDebug(PLUGIN_NAME, "Problem loading state from file %s", pstate->state_path);
      } else {
        TSDebug(PLUGIN_NAME, "Loaded state from file %s", pstate->state_path);
      }
    }

    list_config(pstate, iptr);
  }

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[regex_revalidate] Plugin registration failed");

    free_plugin_state_t(pstate);
    return;
  } else {
    TSDebug(PLUGIN_NAME, "Plugin registration succeeded");
  }

  create_stats();

  main_cont = TSContCreate(main_handler, NULL);
  TSContDataSet(main_cont, (void *)pstate);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, main_cont);

  config_cont = TSContCreate(config_handler, TSMutexCreate());
  TSContDataSet(config_cont, (void *)pstate);

  TSMgmtUpdateRegister(config_cont, PLUGIN_NAME);

  if (!disable_timed_reload) {
    TSContScheduleOnPool(config_cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);
  }

  TSDebug(PLUGIN_NAME, "Plugin Init Complete");
}
