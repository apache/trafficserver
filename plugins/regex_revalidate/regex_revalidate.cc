/** @file

  ATS plugin to do (simple) regular expression remap rules

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
#include "ts/experimental.h"
#include "regex.h"

#include <cinttypes>
#include <cstring>
#include <ctime>
#include <getopt.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

namespace
{
constexpr char const *const PLUGIN_NAME = "regex_revalidate";
constexpr char const *const RELOAD_TAG  = "config_reload";
constexpr char const *const PRINT_TAG   = "config_print";

constexpr char const *const RESULT_MISS  = "MISS";
constexpr char const *const RESULT_STALE = "STALE";

#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define ERROR_LOG(fmt, ...)                         \
  TSError("(%s) " fmt, PLUGIN_NAME, ##__VA_ARGS__); \
  DEBUG_LOG(fmt, ##__VA_ARGS__)

constexpr TSHRTime const CONFIG_TMOUT = 60000; // ms, 60s
constexpr int const OVECTOR_SIZE      = 30;
constexpr int const LOG_ROLL_INTERVAL = 86400;
constexpr int const LOG_ROLL_OFFSET   = 0;

TSCont config_cont{nullptr};

struct Invalidate {
  static Regex config_re;

  std::string regex_text{};
  Regex regex{};
  time_t epoch{0};
  time_t expiry{0};
  TSCacheLookupResult new_result{TS_CACHE_LOOKUP_HIT_STALE};

  Invalidate() = default;

  Invalidate(Invalidate const &orig)
  {
    regex_text = orig.regex_text;
    regex.compile(regex_text.c_str());
    epoch      = orig.epoch;
    expiry     = orig.expiry;
    new_result = orig.new_result;
  }

  Invalidate(Invalidate &&orig)
  {
    std::swap(regex_text, orig.regex_text);
    std::swap(regex, orig.regex);
    std::swap(epoch, orig.epoch);
    std::swap(expiry, orig.expiry);
    std::swap(new_result, orig.new_result);
  }

  Invalidate &
  operator=(Invalidate &&rhs)
  {
    if (&rhs != this) {
      this->~Invalidate();
      new (this) Invalidate(std::move(rhs));
    }
    return *this;
  }

  Invalidate &
  operator=(Invalidate const &rhs)
  {
    if (&rhs != this) {
      this->~Invalidate();
      new (this) Invalidate(rhs);
    }
    return *this;
  }

  ~Invalidate() = default;

  inline bool
  is_valid() const
  {
    return regex.is_valid();
  }

  inline bool
  matches(char const *const url, int const url_len) const
  {
    return regex.matches(std::string_view{url, (unsigned)url_len});
  }

  static Invalidate fromLine(time_t const epoch, char *const line);
};

Regex Invalidate::config_re;

struct PluginState {
  std::string config_file{};
  std::shared_ptr<std::vector<Invalidate>> invalidate_vec; // sorted by regex_text
  bool timed_reload{true};
  time_t config_file_mtime{0};
  time_t min_expiry{0};
  TSTextLogObject log{nullptr};

  ~PluginState()
  {
    if (nullptr != log) {
      TSTextLogObjectDestroy(log);
    }
  }

  bool fromArgs(int argc, char const **argv);
  bool loadConfig(time_t const timenow, std::vector<Invalidate> *const newrules) const;

  void printConfig() const;
  void logConfig() const;
};

Invalidate
Invalidate::fromLine(time_t const epoch, char *const line)
{
  Invalidate rule;

  int ovector[OVECTOR_SIZE];
  DEBUG_LOG("'%s'", line);
  int const rc = config_re.exec(line, ovector, OVECTOR_SIZE);

  if (3 <= rc) {
    int const regbeg = ovector[2];
    int const regend = ovector[3];

    char const *const regstr = line + regbeg;
    char const oldch         = line[regend];
    line[regend]             = '\0'; // temporarily inject null termination

    if (rule.regex.compile(regstr)) {
      rule.regex_text = regstr;
      rule.expiry     = (time_t)atoll(line + ovector[4]);
      rule.epoch      = epoch;
    } else {
      DEBUG_LOG("Invalid regex in line: '%s'", regstr);
    }

    if (5 == rc) {
      char const *const type = line + ovector[8];
      if (0 == strcasecmp(type, RESULT_MISS)) {
        DEBUG_LOG("Regex line set to result type %s: '%s'", RESULT_MISS, regstr);
        rule.new_result = TS_CACHE_LOOKUP_MISS;
      } else if (0 != strcasecmp(type, RESULT_STALE)) {
        DEBUG_LOG("Unknown regex line result type '%s', using %s '%s'", type, RESULT_STALE, regstr);
      }
    }

    // restore the line char*
    line[regend] = oldch;
  }

  return rule;
}

void
get_time_now_str(char *const buf, size_t const buflen)
{
  TSHRTime const timenowusec = TShrtime();
  int64_t const timemsec     = static_cast<int64_t>(timenowusec / 1000000);
  time_t const timesec       = static_cast<time_t>(timemsec / 1000);
  int const ms               = static_cast<int>(timemsec % 1000);

  struct tm tm;
  gmtime_r(&timesec, &tm);
  size_t const dtlen = strftime(buf, buflen, "%b %e %H:%M:%S", &tm);

  // tack on the ms
  snprintf(buf + dtlen, buflen - dtlen, ".%03d", ms);
}

void
PluginState::printConfig() const
{
  char timebuf[64] = "";
  get_time_now_str(timebuf, sizeof(timebuf));

  fprintf(stderr, "[%s] %s config file: %s\n", timebuf, PLUGIN_NAME, config_file.c_str());

  if (invalidate_vec) {
    for (Invalidate const &iv : *invalidate_vec) {
      char const *const typestr = (iv.new_result == TS_CACHE_LOOKUP_MISS ? RESULT_MISS : RESULT_STALE);
      fprintf(stderr, "[%s] %s line: '%s' epoch: %ju expiry: %ju result: '%s'\n", timebuf, PLUGIN_NAME, iv.regex_text.c_str(),
              (uintmax_t)iv.epoch, (uintmax_t)iv.expiry, typestr);
    }
  } else {
    fprintf(stderr, "[%s] %s config: EMPTY\n", timebuf, PLUGIN_NAME);
  }

  fflush(stderr);
}

void
PluginState::logConfig() const
{
  TSDebug(PLUGIN_NAME, "Current config: %s", config_file.c_str());
  if (nullptr != log) {
    TSTextLogObjectWrite(log, "Current config: %s", config_file.c_str());
  }

  if (invalidate_vec) {
    for (Invalidate const &iv : *invalidate_vec) {
      char const *const typestr = (iv.new_result == TS_CACHE_LOOKUP_MISS ? RESULT_MISS : RESULT_STALE);
      TSDebug(PLUGIN_NAME, "line: '%s' epoch: %ju expiry: %ju result: '%s'", iv.regex_text.c_str(), (uintmax_t)iv.epoch,
              (uintmax_t)iv.expiry, typestr);
      if (nullptr != log) {
        TSTextLogObjectWrite(log, "line: '%s' epoch: %ju expiry: %ju result: '%s'", iv.regex_text.c_str(), (uintmax_t)iv.epoch,
                             (uintmax_t)iv.expiry, typestr);
      }
    }
  } else {
    TSDebug(PLUGIN_NAME, "Configuration EMPTY");
    if (nullptr != log) {
      TSTextLogObjectWrite(log, "EMPTY");
    }
  }
}

bool
PluginState::fromArgs(int argc, char const **argv)
{
  int c;
  constexpr option const longopts[] = {
    {"config", required_argument, nullptr, 'c'},
    {"disable-timed-reload", no_argument, nullptr, 'd'},
    {"log", required_argument, nullptr, 'l'},
    {nullptr, 0, nullptr, 0},
  };

  while ((c = getopt_long(argc, (char *const *)argv, "c:l:d", longopts, nullptr)) != -1) {
    switch (c) {
    case 'c':
      config_file = optarg;

      // path is relative to config dir
      if ('/' != config_file[0]) {
        config_file = std::string(TSConfigDirGet()) + "/" + config_file;
      }

      DEBUG_LOG("Config File: %s", config_file.c_str());
      break;
    case 'l':
      if (TS_SUCCESS == TSTextLogObjectCreate(optarg, TS_LOG_MODE_ADD_TIMESTAMP, &log)) {
        TSTextLogObjectRollingEnabledSet(log, 1);
        TSTextLogObjectRollingIntervalSecSet(log, LOG_ROLL_INTERVAL);
        TSTextLogObjectRollingOffsetHrSet(log, LOG_ROLL_OFFSET);
        DEBUG_LOG("Logging Mode enabled");
      }
      break;
    case 'd':
      timed_reload = false;
      DEBUG_LOG("Timed reload disabled (disable-timed-reload)");
      break;
    default:
      break;
    }
  }

  if (config_file.empty()) {
    ERROR_LOG("Plugin requires a --config option along with a config file name");
    return false;
  }

  return true;
}

time_t
timeForFile(std::string const &filepath)
{
  time_t mtime{0};
  struct stat fstat;
  if (0 == stat(filepath.c_str(), &fstat)) {
    mtime = fstat.st_mtime;
  } else {
    DEBUG_LOG("Could not stat %s", filepath.c_str());
  }
  return mtime;
}

// load config, true if rules changed
bool
PluginState::loadConfig(time_t const timenow, std::vector<Invalidate> *const rules) const
{
  TSAssert(nullptr != rules);

  FILE *const fs = fopen(config_file.c_str(), "r");
  if (nullptr == fs) {
    DEBUG_LOG("Could not open %s for reading", config_file.c_str());
    return false;
  }

  // load from file
  std::vector<Invalidate> loaded;
  int lineno = 0;
  char line[LINE_MAX];
  while (nullptr != fgets(line, LINE_MAX, fs)) {
    ++lineno;
    line[strcspn(line, "\r\n")] = '\0';
    if (0 < strlen(line) && '#' != line[0]) {
      Invalidate rnew = Invalidate::fromLine(timenow, line);
      if (rnew.is_valid()) {
        loaded.push_back(std::move(rnew));
      } else {
        DEBUG_LOG("Invalid rule '%s' from line: '%d'", line, lineno);
      }
    }
  }

  fclose(fs);

  if (loaded.empty()) {
    DEBUG_LOG("No rules loaded from file '%s'", config_file.c_str());
    return false;
  }

  // stable sort to make clearing duplicates easy
  std::stable_sort(loaded.begin(), loaded.end(),
                   [](Invalidate const &lhs, Invalidate const &rhs) { return lhs.regex_text < rhs.regex_text; });

  // sweep to clear duplicates, last one wins
  for (size_t index = 0; index < (loaded.size() - 1); ++index) {
    if (loaded[index].regex_text == loaded[index + 1].regex_text) {
      loaded[index] = Invalidate{};
    }
  }

  if (!this->invalidate_vec || this->invalidate_vec->empty()) {
    DEBUG_LOG("Installing fresh rules");
    *rules = std::move(loaded);
    return true;
  }

  DEBUG_LOG("Merging new config");

  // merge loaded and current rule set
  auto const &cur = *(this->invalidate_vec);
  auto itload     = loaded.begin();
  auto itcur      = cur.cbegin();

  bool changed = false;

  // reimplementation of std::set_union
  while (cur.cend() != itcur) {
    if (loaded.cend() == itload) {
      std::copy(itcur, cur.cend(), rules->end());
      break;
    }

    // fast forward over cleared items
    while (!itload->is_valid()) {
      DEBUG_LOG("Skipping cleared duplicate rule");
      ++itload;
      TSAssert(loaded.end() != itload); // last item will always be valid.
    }

    int const cmp = itload->regex_text.compare(itcur->regex_text);
    if (cmp < 0) {
      if (timenow < itload->expiry) {
        DEBUG_LOG("Adding new rule: '%s'", itload->regex_text.c_str());
        rules->push_back(std::move(*itload));
        changed = true;
      } else {
        DEBUG_LOG("Not adding new expired rule: '%s'", itload->regex_text.c_str());
      }
      ++itload;
    } else if (0 < cmp) {
      DEBUG_LOG("Retaining old rule: '%s'", itcur->regex_text.c_str());
      rules->push_back(*itcur);
      ++itcur;
    } else {
      if (itload->expiry != itcur->expiry || itload->new_result != itcur->new_result) {
        DEBUG_LOG("Updating rule: '%s'", itload->regex_text.c_str());
        rules->push_back(std::move(*itload));
        changed = true;
      } else {
        DEBUG_LOG("Using old rule: '%s'", itcur->regex_text.c_str());
        rules->push_back(*itcur);
      }
      ++itcur;
      ++itload;
    }
  }

  // any leftover loaded rules get tacked on
  while (loaded.end() != itload) {
    DEBUG_LOG("Adding new rule: '%s'", itload->regex_text.c_str());
    rules->push_back(std::move(*itload));
    ++itload;
    changed = true;
  }

  DEBUG_LOG("Rules have been changed: '%s'", changed ? "true" : "false");

  return changed;
}

// remove expired rules
bool
pruneConfig(time_t const timenow, std::vector<Invalidate> *const rules, time_t *const min_expiry)
{
  TSAssert(nullptr != rules);
  TSAssert(nullptr != min_expiry);

  bool pruned = false;
  if (timenow < *min_expiry || nullptr == rules) {
    return pruned;
  }

  // recalculate min_expiry
  *min_expiry = std::numeric_limits<time_t>::max();

  auto iter = rules->begin();
  while (rules->end() != iter) {
    Invalidate const &inv = *iter;

    if (inv.expiry < timenow) {
      DEBUG_LOG("Removing rule: %s", inv.regex_text.c_str());
      iter   = rules->erase(iter); // retain sort order
      pruned = true;
    } else {
      *min_expiry = std::min(*min_expiry, inv.expiry);
      ++iter;
    }
  }

  std::vector<Invalidate>::iterator itvec(rules->begin());

  while (rules->end() != itvec) {
    Invalidate const &inv = *itvec;

    if (inv.expiry < timenow) {
      DEBUG_LOG("Removing rule: %s", inv.regex_text.c_str());
      itvec  = rules->erase(itvec); // retain sort order
      pruned = true;
    } else {
      *min_expiry = std::min(*min_expiry, inv.expiry);
      ++itvec;
    }
  }

  if (pruned && rules->empty()) {
    DEBUG_LOG("All rules pruned");
  }

  return pruned;
}

int
config_handler(TSCont config_cont, TSEvent event, void *edata)
{
  DEBUG_LOG("config_handler, event: %d", event);

  // config_cont's mutex is already locked
  PluginState *const pstate = static_cast<PluginState *>(TSContDataGet(config_cont));

  bool should_reload = false;

  switch (event) {
  case TS_EVENT_TIMEOUT: {
    should_reload = pstate->timed_reload;
  } break;
  case TS_EVENT_MGMT_UPDATE: {
    should_reload = true;
  } break;
  case TS_EVENT_LIFECYCLE_MSG: {
    // ensure the message is for regex_revalidate
    TSPluginMsg *const msgp = (TSPluginMsg *)edata;
    if (0 == strncasecmp(msgp->tag, PLUGIN_NAME, strlen(msgp->tag))) {
      char const *const msgstr = static_cast<char const *>(msgp->data);
      int const msglen         = static_cast<int>(msgp->data_size);
      DEBUG_LOG("Lifecycle plugin message received: %.*s", msglen, msgstr);
      if (0 == strncasecmp(msgstr, RELOAD_TAG, msglen)) {
        should_reload = true;
      } else if (0 == strncasecmp(msgstr, PRINT_TAG, msglen)) {
        pstate->printConfig();
      } else {
        ERROR_LOG("Unrecognized lifecycle message %.*s", msglen, msgstr);
      }
    }
  } break;
  default: // unknown message
    return 0;
    break;
  }

  time_t const timenow = time(nullptr);

  bool rules_changed = false;
  std::vector<Invalidate> newrules;

  if (should_reload) {
    time_t const mtime = timeForFile(pstate->config_file);
    if (mtime != pstate->config_file_mtime) {
      rules_changed             = pstate->loadConfig(timenow, &newrules);
      pstate->config_file_mtime = mtime;

      // this sets off a new min expiry scan
      if (rules_changed) {
        pstate->min_expiry = 0;
      }
    }
  }

  // prune
  if (pstate->min_expiry < timenow) {
    if (newrules.empty()) {
      newrules = *(pstate->invalidate_vec);
    }

    time_t new_expiry  = 0;
    bool const pruned  = pruneConfig(timenow, &newrules, &new_expiry);
    pstate->min_expiry = new_expiry;

    if (pruned) {
      rules_changed = true;
    }
  }

  if (rules_changed) {
    auto vecnew = std::make_shared<std::vector<Invalidate>>(std::move(newrules));

    std::atomic_store(&(pstate->invalidate_vec), vecnew);
    DEBUG_LOG("New configuation installed");
    pstate->logConfig();
  }

  // Reschedule the continuous pruning/reload job
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
                TSHttpTxnCacheLookupStatusSet(txn, inv.new_result);
                DEBUG_LOG("Forced revalidate - %.*s", url_len, url);
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
  default:
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

namespace
{
void
setup_config_cont(PluginState *const pstate)
{
  if (nullptr == config_cont) {
    DEBUG_LOG("creating config continuation");

    // create reusable configuration file regex
    constexpr char const *const expr = "^([^#].+?)\\s+(\\d+)(\\s+(\\w+))?\\s*$";
    bool const cstat                 = Invalidate::config_re.compile(expr);

    if (!cstat) {
      ERROR_LOG("setup_config_cont: Unable to compile config_re, disabling plugin");
      return;
    }

    // set up the config continuation
    config_cont = TSContCreate(config_handler, TSMutexCreate());
    TSContDataSet(config_cont, static_cast<void *>(pstate));

    // constantly run this for rule grooming
    TSContScheduleOnPool(config_cont, CONFIG_TMOUT, TS_THREAD_POOL_TASK);

    // also register this continuation with any config reloads
    TSMgmtUpdateRegister(config_cont, PLUGIN_NAME);

    // and also register as a lifecycle hook
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, config_cont);
  }
}

} // namespace

void
TSPluginInit(int argc, char const *argv[])
{
  DEBUG_LOG("Starting plugin init");

  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    ERROR_LOG("Global plugin registration failed");
    return;
  } else {
    DEBUG_LOG("Global plugin registration succeeded");
  }

  if (TS_VERSION_MAJOR != TSTrafficServerVersionGetMajor()) {
    ERROR_LOG("Plugin requires Traffic Server %d", TS_VERSION_MAJOR);
    return;
  }

  PluginState *const pstate = new PluginState;
  if (!pstate->fromArgs(argc, argv)) {
    ERROR_LOG("Remap plugin registration failed");
    delete pstate;
    return;
  }

  setup_config_cont(pstate);

  TSCont const main_cont = TSContCreate(main_handler, nullptr);
  TSContDataSet(main_cont, static_cast<void *>(pstate));
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, main_cont);

  TSAssert(nullptr != config_cont);

  time_t const timenow = time(nullptr);

  std::vector<Invalidate> newrules;
  pstate->loadConfig(timenow, &newrules);
  pstate->config_file_mtime = timeForFile(pstate->config_file);

  time_t new_expiry = 0;
  pruneConfig(timenow, &newrules, &new_expiry);
  pstate->min_expiry = new_expiry;

  auto vecnew            = std::make_shared<std::vector<Invalidate>>(std::move(newrules));
  pstate->invalidate_vec = vecnew;

  DEBUG_LOG("Configuation installed");

  pstate->logConfig();

  DEBUG_LOG("Global Plugin Init Complete");
}
