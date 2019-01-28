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

#include "ts/ts.h"
#include "ts/remap.h"

#include <cinttypes>
#include <ctime>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <string_view>
#include <sys/types.h>
#include <sys/stat.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

namespace
{
constexpr char const *const LOG_PREFIX = "regex_revalidate";
constexpr TSHRTime const CONFIG_TMOUT  = 60000; // ms, 60s
// constexpr TSHRTime const CONFIG_TMOUT = 500; // ms, 60s
constexpr int const OVECTOR_SIZE      = 30;
constexpr int const LOG_ROLL_INTERVAL = 86400;
constexpr int const LOG_ROLL_OFFSET   = 0;

TSCont config_cont{nullptr};
pcre *config_re{nullptr};

// protected by config_cont's mutex
struct PluginState;
std::vector<PluginState *> plugin_states;

void *
ts_malloc(size_t s)
{
  return TSmalloc(s);
}

void
ts_free(void *s)
{
  return TSfree(s);
}

struct Invalidate {
  char const *regex_text{nullptr};
  pcre *regex{nullptr};
  pcre_extra *regex_extra{nullptr};
  time_t epoch{0};
  time_t expiry{0};

  Invalidate(char const *regextext = nullptr) : regex_text(regextext), regex(nullptr), regex_extra(nullptr)
  {
    if (nullptr != regex_text) {
      char const *errptr = nullptr;
      int erroffset      = 0;
      regex              = pcre_compile(regex_text, 0, &errptr, &erroffset, nullptr);
      if (nullptr != regex) {
        regex_extra = pcre_study(regex, 0, &errptr);
      }
    }
  }

  Invalidate(Invalidate const &orig) : regex_text(nullptr), regex(nullptr), regex_extra(nullptr)
  {
    if (nullptr != orig.regex_text) {
      regex_text         = TSstrdup(orig.regex_text);
      char const *errptr = nullptr;
      int erroffset      = 0;
      regex              = pcre_compile(regex_text, 0, &errptr, &erroffset, nullptr);
      if (nullptr != regex) {
        regex_extra = pcre_study(regex, 0, &errptr);
      }

      epoch  = orig.epoch;
      expiry = orig.expiry;
    }
  }

  Invalidate(Invalidate &&orig) : regex_text(nullptr), regex(nullptr), regex_extra(nullptr)
  {
    std::swap(orig.regex_text, regex_text);
    std::swap(orig.regex, regex);
    std::swap(orig.regex_extra, regex_extra);
    std::swap(orig.epoch, epoch);
    std::swap(orig.expiry, expiry);
  }

  Invalidate &
  operator=(Invalidate &&rhs)
  {
    if (&rhs != this) {
      std::swap(rhs.regex_text, regex_text);
      std::swap(rhs.regex, regex);
      std::swap(rhs.regex_extra, regex_extra);
      std::swap(rhs.epoch, epoch);
      std::swap(rhs.expiry, expiry);
    }
    return *this;
  }

  ~Invalidate()
  {
    if (nullptr != regex_extra) {
#ifndef PCRE_STUDY_JIT_COMPILE
      pcre_free(regex_extra);
#else
      pcre_free_study(regex_extra);
#endif
    }
    if (nullptr != regex) {
      pcre_free(regex);
    }
    if (nullptr != regex_text) {
      pcre_free_substring(regex_text);
    }
  }

  bool
  isValid() const
  {
    return nullptr != regex;
  }

  bool
  matches(char const *const url, int const url_len) const
  {
    return 0 <= pcre_exec(regex, regex_extra, url, url_len, 0, 0, nullptr, 0);
  }

  static Invalidate fromLine(pcre const *const config_re, char const *const line);
};

struct PluginState {
  std::string remap_from{};
  std::string remap_to{};
  std::string config_file{};
  std::shared_ptr<std::vector<Invalidate>> invalidate_vec;
  bool disable_timed_reload{false};
  time_t time_last_load{0};
  time_t min_expiry{0};
  TSTextLogObject log{nullptr};

  ~PluginState()
  {
    if (nullptr != log) {
      TSTextLogObjectDestroy(log);
    }
  }

  bool
  isSameRemap(PluginState const &other) const
  {
    return other.remap_from == remap_from && other.remap_to == remap_to;
  }

  void logConfig(std::shared_ptr<std::vector<Invalidate>> const &vecinv) const;

  bool fromArgs(int argc, char const **argv);

  bool loadConfig(time_t const timenow, std::shared_ptr<std::vector<Invalidate>> &vecinv);

  bool pruneConfig(time_t const timenow, std::shared_ptr<std::vector<Invalidate>> &vecinv);
};

Invalidate
Invalidate::fromLine(pcre const *const config_re, char const *const line)
{
  int ovector[OVECTOR_SIZE];
  TSDebug(LOG_PREFIX, "Processing: '%s'", line);
  int const rc = pcre_exec(config_re, nullptr, line, strlen(line), 0, 0, ovector, OVECTOR_SIZE);
  if (3 == rc) {
    char const *regex_text = nullptr;
    pcre_get_substring(line, ovector, rc, 1, &regex_text);

    TSAssert(nullptr != regex_text);
    Invalidate inv(regex_text); // take ownership of pointer
    if (inv.isValid()) {
      inv.expiry = (time_t)atoll(line + ovector[4]);
      // inv.epoch is still set to '0'
      return inv;
    } else {
      TSDebug(LOG_PREFIX, "Invalid regex in line: '%s'", regex_text);
    }
  }

  return Invalidate{};
}

void
PluginState::logConfig(std::shared_ptr<std::vector<Invalidate>> const &invvec) const
{
  TSDebug(LOG_PREFIX, "Current config: %s %s %s", remap_from.c_str(), remap_to.c_str(), config_file.c_str());
  if (nullptr != log) {
    TSTextLogObjectWrite(log, "Current config: %s %s %s", remap_from.c_str(), remap_to.c_str(), config_file.c_str());
  }

  if (invvec) {
    for (Invalidate const &iv : *invvec) {
      TSDebug(LOG_PREFIX, "%s epoch: %ju expiry: %ju", iv.regex_text, (uintmax_t)iv.epoch, (uintmax_t)iv.expiry);
      if (nullptr != log) {
        TSTextLogObjectWrite(log, "%s epoch: %ju expiry: %ju", iv.regex_text, (uintmax_t)iv.epoch, (uintmax_t)iv.expiry);
      }
    }
  } else {
    TSDebug(LOG_PREFIX, "Configuration EMPTY");
    if (nullptr != log) {
      TSTextLogObjectWrite(log, "EMPTY");
    }
  }
}

bool
PluginState::fromArgs(int argc, char const **argv)
{
  int c;
  constexpr option const longopts[] = {{"config", required_argument, nullptr, 'c'},
                                       {"log", required_argument, nullptr, 'l'},
                                       {"disable-timed-reload", no_argument, nullptr, 'd'},
                                       {nullptr, 0, nullptr, 0}};

  while ((c = getopt_long(argc, (char *const *)argv, "c:l:d", longopts, nullptr)) != -1) {
    switch (c) {
    case 'c':
      config_file = optarg;
      TSDebug(LOG_PREFIX, "Config File: %s", config_file.c_str());
      break;
    case 'l':
      if (TS_SUCCESS == TSTextLogObjectCreate(optarg, TS_LOG_MODE_ADD_TIMESTAMP, &log)) {
        TSTextLogObjectRollingEnabledSet(log, 1);
        TSTextLogObjectRollingIntervalSecSet(log, LOG_ROLL_INTERVAL);
        TSTextLogObjectRollingOffsetHrSet(log, LOG_ROLL_OFFSET);
        TSDebug(LOG_PREFIX, "Logging Mode enabled");
      }
      break;
    case 'd':
      disable_timed_reload = true;
      TSDebug(LOG_PREFIX, "Timed reload disabled (disable-timed-reload)");
      break;
    default:
      break;
    }
  }

  if (config_file.empty()) {
    TSError("[regex_revalidate] Plugin requires a --config option along with a config file name");
    return false;
  }

  return true;
}

bool
PluginState::loadConfig(time_t const timenow, std::shared_ptr<std::vector<Invalidate>> &vecinv)
{
  std::string configfile = config_file;

  if (configfile.empty()) {
    TSError("[regex_revalidate] No configfile");
    return false;
  }

  // path is relative to config dir
  if ('/' != configfile[0]) {
    configfile = std::string(TSConfigDirGet()) + "/" + configfile;
  }
  struct stat fstat;
  if (0 != stat(configfile.c_str(), &fstat)) {
    TSDebug(LOG_PREFIX, "Could not stat %s", configfile.c_str());
    return false;
  }

  // check if file has been modified since last load
  if (fstat.st_mtime < time_last_load) {
    TSDebug(LOG_PREFIX, "File mod time is not newer: %ju < %ju", (uintmax_t)fstat.st_mtime, (uintmax_t)time_last_load);
    return false;
  }

  FILE *const fs = fopen(configfile.c_str(), "r");
  if (nullptr == fs) {
    TSDebug(LOG_PREFIX, "Could not open %s for reading", configfile.c_str());
    return false;
  }

  time_last_load = timenow;

  bool updated = false;

  // create an index into the existing check for fast updates
  using TextIndex = std::map<std::string, size_t, std::less<>>;
  TextIndex textind;

  if (vecinv) {
    for (size_t index = 0; index < vecinv->size(); ++index) {
      Invalidate const &inv                = (*vecinv)[index];
      textind[std::string(inv.regex_text)] = index;
    }
  }

  // load and merge file into the vector
  int lineno = 0;
  char line[LINE_MAX];
  while (nullptr != fgets(line, LINE_MAX, fs)) {
    line[strcspn(line, "\r\n")] = '\0';
    ++lineno;
    Invalidate inv = Invalidate::fromLine(config_re, line);
    if (inv.isValid()) {
      auto const itfind = textind.find(std::string_view(inv.regex_text));

      if (textind.end() != itfind) { // merge with previous rule
        size_t const index  = itfind->second;
        Invalidate &invprev = (*vecinv)[index];
        TSDebug(LOG_PREFIX, "Merging with previous rule: '%s'", line);
        if (inv.expiry != invprev.expiry) {
          updated        = true;
          invprev.expiry = inv.expiry;
          TSDebug(LOG_PREFIX, "Expiration updated with rule '%s'", line);
        }
      } else { // new rule, update index
        if (!vecinv) {
          vecinv = std::make_shared<std::vector<Invalidate>>();
        }
        TSDebug(LOG_PREFIX, "Loading new rule: '%s'", line);
        updated                              = true;
        inv.epoch                            = timenow;
        size_t const newindex                = vecinv->size();
        textind[std::string(inv.regex_text)] = newindex;
        vecinv->push_back(std::move(inv));
      }
    } else {
      TSDebug(LOG_PREFIX, "Invalid line: '%s'", line);
    }
  }

  fclose(fs);

  if (updated) {
    min_expiry = 0; // next prune will recalculate this
  }

  return updated;
}

bool
PluginState::pruneConfig(time_t const timenow, std::shared_ptr<std::vector<Invalidate>> &vecinv)
{
  bool pruned = false;
  if (timenow < min_expiry || nullptr == vecinv.get()) {
    return pruned;
  }

  // recalculate min_expiry
  min_expiry = std::numeric_limits<time_t>::max();

  if (vecinv) {
    std::vector<Invalidate>::iterator itvec(vecinv->begin());

    while (vecinv->end() != itvec) {
      Invalidate const &inv = *itvec;

      if (inv.expiry < timenow) {
        TSDebug(LOG_PREFIX, "Removing rule: %s", inv.regex_text);
        std::swap(*itvec, vecinv->back());
        vecinv->pop_back();
        pruned = true;
      } else {
        min_expiry = std::min(min_expiry, inv.expiry);
        ++itvec;
      }
    }
  }

  if (pruned && vecinv->empty()) {
    vecinv.reset();
    TSDebug(LOG_PREFIX, "All rules pruned");
  }

  return pruned;
}

int
config_handler(TSCont config_cont, TSEvent event, void * /* edata */)
{
  TSDebug(LOG_PREFIX, "config_handler, event: %d", event);

  // config_cont's mutex is already locked

  switch (event) {
  case TS_EVENT_TIMEOUT:
  case TS_EVENT_MGMT_UPDATE: {
    time_t const timenow = time(nullptr);
    for (PluginState *const pstate : plugin_states) {
      using InvHandle  = std::shared_ptr<std::vector<Invalidate>>;
      InvHandle vecold = pstate->invalidate_vec;

      // create a copy of the currently active vector
      InvHandle vecnew;

      if (vecold) {
        vecnew = std::make_shared<std::vector<Invalidate>>(*vecold);
      } else {
        vecnew = std::make_shared<std::vector<Invalidate>>();
      }

      bool reloaded = false;
      if (event == TS_EVENT_MGMT_UPDATE || !pstate->disable_timed_reload) {
        reloaded = pstate->loadConfig(timenow, vecnew);
      }
      bool const pruned = pstate->pruneConfig(timenow, vecnew);

      if (pruned || reloaded) {
        std::atomic_store(&(pstate->invalidate_vec), vecnew);
        TSDebug(LOG_PREFIX, "New configuation installed");
        pstate->logConfig(vecnew);
      }
    }
  } break;
  default:
    break;
  }

  // Don't reschedule on a config reload
  if (TS_EVENT_TIMEOUT == event) {
    TSContScheduleOnPool(config_cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);
  }

  return 0;
}

time_t
get_date_from_cached_hdr(TSHttpTxn txn)
{
  TSMBuffer buf;
  TSMLoc hdr_loc, date_loc;
  time_t date = 0;

  if (TSHttpTxnCachedRespGet(txn, &buf, &hdr_loc) == TS_SUCCESS) {
    date_loc = TSMimeHdrFieldFind(buf, hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (nullptr != date_loc) {
      date = TSMimeHdrFieldValueDateGet(buf, hdr_loc, date_loc);
      TSHandleMLocRelease(buf, hdr_loc, date_loc);
    }
    TSHandleMLocRelease(buf, nullptr, hdr_loc);
  }

  return date;
}

int
main_handler(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;
  int status    = TS_ERROR;

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_SUCCESS) {
      if (status == TS_CACHE_LOOKUP_HIT_FRESH) {
        PluginState *const pstate = static_cast<PluginState *>(TSContDataGet(cont));
        // atomcally grab a handle
        using InvHandle        = std::shared_ptr<std::vector<Invalidate>>;
        InvHandle const vecinv = std::atomic_load(&(pstate->invalidate_vec));

        if (vecinv) {
          time_t const date    = get_date_from_cached_hdr(txn);
          time_t const timenow = time(nullptr);
          char *url            = nullptr;
          int url_len          = 0;

          for (Invalidate const &inv : *vecinv) {
            if (date <= inv.epoch && timenow <= inv.expiry) {
              if (nullptr == url) {
                url = TSHttpTxnEffectiveUrlStringGet(txn, &url_len);
              }

              if (nullptr == url || 0 == url_len) {
                break;
              } else if (inv.matches(url, url_len)) {
                TSHttpTxnCacheLookupStatusSet(txn, TS_CACHE_LOOKUP_HIT_STALE);
                TSDebug(LOG_PREFIX, "Forced revalidate - %.*s", url_len, url);
                break;
              }
            }
          }

          if (nullptr != url) {
            TSfree(url);
          }
        }
      }
    }
    break;
  case TS_EVENT_HTTP_TXN_CLOSE: // never called by global
    TSContDestroy(cont);
    break;
  default:
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  TSCont const main_cont = TSContCreate(main_handler, nullptr);

  TSContDataSet(main_cont, ih);
  TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, main_cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, main_cont);

  return TSREMAP_NO_REMAP;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  TSDebug(LOG_PREFIX, "Starting remap init");
  PluginState *const pstatenew = new PluginState;

  if (!pstatenew->fromArgs(argc - 1, (char const **)(argv + 1))) {
    TSError("[regex_revalidate] Remap plugin registration failed");
    delete pstatenew;
    return TS_ERROR;
  }

  pstatenew->remap_from = argv[0];
  pstatenew->remap_to   = argv[1];

  *ih = static_cast<void *>(pstatenew);

  TSAssert(nullptr != config_cont);

  // put this plugin on the config scanner
  TSMutex const config_mutex = TSContMutexGet(config_cont);
  TSMutexLock(config_mutex);

  // If the remap rule already exists, use its invalidate list
  for (PluginState *const pstate : plugin_states) {
    if (pstatenew->isSameRemap(*pstate)) {
      TSDebug(LOG_PREFIX, "Transferring active rules across reload");
      pstatenew->invalidate_vec = std::atomic_load(&(pstate->invalidate_vec));
      break;
    }
  }

  using InvHandle  = std::shared_ptr<std::vector<Invalidate>>;
  InvHandle vecold = pstatenew->invalidate_vec;

  InvHandle vecnew;
  if (vecold) {
    vecnew = std::make_shared<std::vector<Invalidate>>(*vecold);
  } else {
    vecnew = std::make_shared<std::vector<Invalidate>>();
  }

  // Load the config.
  time_t const timenow = time(nullptr);
  bool const loaded    = pstatenew->loadConfig(timenow, vecnew);
  bool const pruned    = pstatenew->pruneConfig(timenow, vecnew);

  if (loaded || pruned) {
    pstatenew->invalidate_vec = vecnew;
  }

  pstatenew->logConfig(vecnew);

  plugin_states.push_back(pstatenew);

  TSMutexUnlock(config_mutex);

  TSDebug(LOG_PREFIX, "Remap plugin registration succeeded");

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(LOG_PREFIX, "Deleting remap plugin instance");

  TSAssert(nullptr != ih);

  if (nullptr != ih) {
    PluginState *const pstate = static_cast<PluginState *>(ih);

    TSAssert(nullptr != config_cont);

    // remove the pstate from the plugin states
    TSMutex const config_mutex = TSContMutexGet(config_cont);
    TSMutexLock(config_mutex);

    std::vector<PluginState *>::iterator const itf = std::find(plugin_states.begin(), plugin_states.end(), pstate);

    if (plugin_states.end() != itf) {
      std::swap(*itf, plugin_states.back());
      plugin_states.pop_back();
    } else {
      TSError("Unable to find registered plugin config state");
    }

    TSMutexUnlock(config_mutex);
    delete pstate;
  }
}

namespace
{
bool
check_ts_version()
{
  return TS_VERSION_MAJOR == TSTrafficServerVersionGetMajor();
}

void
setup_memory_allocation()
{
  if (&ts_malloc != pcre_malloc) {
    pcre_malloc = &ts_malloc;
    pcre_free   = &ts_free;
  }
}

void
setup_config_cont()
{
  if (nullptr == config_cont) {
    TSDebug(LOG_PREFIX, "creating config continuation");
    config_cont = TSContCreate(config_handler, TSMutexCreate());

    // create reusable configuration file regex
    constexpr char const *const expr = "^([^#].+?)\\s+(\\d+)\\s*$";
    char const *errptr               = nullptr;
    int erroffset                    = 0;
    config_re                        = pcre_compile(expr, 0, &errptr, &erroffset, nullptr);
    TSAssert(nullptr != config_re);

    // constantly run this for rule grooming
    TSContScheduleOnPool(config_cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);

    // also register this continuation with any config reloads
    TSMgmtUpdateRegister(config_cont, LOG_PREFIX);
  }
}

} // namespace

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbug, int errbuf_size)
{
  TSDebug(LOG_PREFIX, "Starting TSRemapInit");
  if (!check_ts_version()) {
    TSError("[regex_revalidate] Plugin requires Traffic Server %d", TS_VERSION_MAJOR);
    return TS_ERROR;
  }

  setup_memory_allocation();
  setup_config_cont();

  TSDebug(LOG_PREFIX, "TSRemapInit done");

  return TS_SUCCESS;
}

void
TSPluginInit(int argc, char const *argv[])
{
  TSDebug(LOG_PREFIX, "Starting plugin init");

  TSPluginRegistrationInfo info;
  info.plugin_name   = LOG_PREFIX;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[regex_revalidate] Global plugin registration failed");
    return;
  } else {
    TSDebug(LOG_PREFIX, "Global plugin registration succeeded");
  }

  if (!check_ts_version()) {
    TSError("[regex_revalidate] Plugin requires Traffic Server %d", TS_VERSION_MAJOR);
    return;
  }

  setup_memory_allocation();
  setup_config_cont();

  PluginState *const pstatenew = new PluginState;
  if (!pstatenew->fromArgs(argc, argv)) {
    TSError("[regex_revalidate] Remap plugin registration failed");
    delete pstatenew;
    return;
  }

  TSCont const main_cont = TSContCreate(main_handler, nullptr);
  TSContDataSet(main_cont, static_cast<void *>(pstatenew));
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, main_cont);

  TSAssert(nullptr != config_cont);

  TSMutex const config_mutex = TSContMutexGet(config_cont);
  TSMutexLock(config_mutex);

  plugin_states.push_back(pstatenew);

  time_t const timenow = time(nullptr);

  if (pstatenew->loadConfig(timenow, pstatenew->invalidate_vec)) {
    pstatenew->logConfig(pstatenew->invalidate_vec);
  }

  TSMutexUnlock(config_mutex);

  TSDebug(LOG_PREFIX, "Global Plugin Init Complete");
}
