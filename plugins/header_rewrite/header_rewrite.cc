/*
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

#include <fstream>
#include <mutex>
#include <string>
#include <stack>
#include <stdexcept>
#include <array>
#include <getopt.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/remap_version.h"

#include "proxy/http/remap/PluginFactory.h"
#include "records/RecCore.h"

#include "parser.h"
#include "ruleset.h"
#include "resources.h"
#include "conditions.h"
#include "conditions_geo.h"

// Debugs
namespace header_rewrite_ns
{
const char PLUGIN_NAME[]     = "header_rewrite";
const char PLUGIN_NAME_DBG[] = "dbg_header_rewrite";

DbgCtl dbg_ctl{PLUGIN_NAME_DBG};
DbgCtl pi_dbg_ctl{PLUGIN_NAME};

std::once_flag initHRWLibs;
PluginFactory  plugin_factory;

int timezone        = 0;
int inboundIpSource = 0;

} // namespace header_rewrite_ns

static void
initHRWLibraries(const std::string &dbPath)
{
  header_rewrite_ns::plugin_factory.setRuntimeDir(RecConfigReadRuntimeDir()).addSearchDir(RecConfigReadPluginDir());

  if (dbPath.empty()) {
    return;
  }

  Dbg(pi_dbg_ctl, "Loading geo db %s", dbPath.c_str());

#if TS_USE_HRW_GEOIP
  GeoIPConditionGeo::initLibrary(dbPath);
#elif TS_USE_HRW_MAXMINDDB
  MMConditionGeo::initLibrary(dbPath);
#endif
}

// Forward declaration for the main continuation.
static int cont_rewrite_headers(TSCont, TSEvent, void *);

// Simple wrapper around a configuration file / set. This is useful such that
// we can reuse most of the code for both global and per-remap rule sets.
class RulesConfig
{
public:
  RulesConfig()
  {
    Dbg(dbg_ctl, "RulesConfig CTOR");
    _cont = TSContCreate(cont_rewrite_headers, nullptr);
    TSContDataSet(_cont, static_cast<void *>(this));
  }

  ~RulesConfig()
  {
    Dbg(dbg_ctl, "RulesConfig DTOR");
    TSContDestroy(_cont);
  }

  [[nodiscard]] TSCont
  continuation() const
  {
    return _cont;
  }

  [[nodiscard]] ResourceIDs
  resid(int hook) const
  {
    return _resids[hook];
  }

  [[nodiscard]] RuleSet *
  rule(int hook) const
  {
    return _rules[hook].get();
  }

  bool parse_config(const std::string &fname, TSHttpHookID default_hook, char *from_url = nullptr, char *to_url = nullptr);

private:
  void add_rule(std::unique_ptr<RuleSet> rule);

  TSCont                                                      _cont;
  std::array<std::unique_ptr<RuleSet>, TS_HTTP_LAST_HOOK + 1> _rules{};
  std::array<ResourceIDs, TS_HTTP_LAST_HOOK + 1>              _resids{};
};

void
RulesConfig::add_rule(std::unique_ptr<RuleSet> rule)
{
  int hook = rule->get_hook();
  if (!_rules[hook]) {
    _rules[hook] = std::move(rule);
  } else {
    _rules[hook]->append(std::move(rule));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Config parser, use to parse both the global, and per-remap, configurations.
//
// Note that this isn't particularly efficient, but it's a startup time cost
// anyways (or reload for remap.config), so not really in the critical path.
//
bool
RulesConfig::parse_config(const std::string &fname, TSHttpHookID default_hook, char *from_url, char *to_url)
{
  std::unique_ptr<RuleSet>     rule(nullptr);
  std::string                  filename;
  int                          lineno = 0;
  std::stack<ConditionGroup *> group_stack;
  ConditionGroup              *group = nullptr;

  if (0 == fname.size()) {
    TSError("[%s] no config filename provided", PLUGIN_NAME);
    return false;
  }

  if (fname[0] != '/') {
    filename  = TSConfigDirGet();
    filename += "/" + fname;
  } else {
    filename = fname;
  }

  auto reader = openConfig(filename);
  if (!reader || !reader->stream) {
    TSError("[%s] unable to open %s", PLUGIN_NAME, filename.c_str());
    return false;
  }

  Dbg(dbg_ctl, "Parsing started on file: %s", filename.c_str());

  while (!reader->stream->eof()) {
    std::string line;

    getline(*reader->stream, line);
    ++lineno;
    Dbg(dbg_ctl, "Reading line: %d: %s", lineno, line.c_str());

    while (std::isspace(line[0])) {
      line.erase(0, 1);
    }

    while (line.length() > 0 && std::isspace(line[line.length() - 1])) {
      line.erase(line.length() - 1, 1);
    }

    if (line.empty() || (line[0] == '#')) {
      continue;
    }

    Parser p(from_url, to_url);

    // Tokenize and parse this line
    if (!p.parse_line(line)) {
      TSError("[%s] Error parsing file '%s', line '%s', lineno: %d", PLUGIN_NAME, filename.c_str(), line.c_str(), lineno);
      Dbg(dbg_ctl, "Error parsing line '%s', lineno: %d", line.c_str(), lineno);
      continue;
    }

    if (p.empty()) {
      continue;
    }

    TSHttpHookID hook    = default_hook;
    bool         is_hook = p.cond_is_hook(hook);

    // Deal with the elif / else special keywords, these are neither conditions nor operators.
    if (p.is_else() || p.is_elif()) {
      Dbg(pi_dbg_ctl, "Entering elif/else, CondClause=%d", static_cast<int>(p.get_clause()));
      if (rule) {
        group = rule->new_section(p.get_clause());
        continue;
      } else {
        TSError("[%s] ELSE/ELIF clause without preceding conditions in file: %s, lineno: %d", PLUGIN_NAME, fname.c_str(), lineno);
        return false;
      }
    }

    // If we are at the beginning of a new condition, save away the previous rule (but only if it has operators).
    if (p.is_cond() && rule) {
      bool transfer    = rule->cur_section()->has_operator();
      auto rule_clause = rule->get_clause();

      if (rule_clause == Parser::CondClause::ELIF) {
        if (is_hook) {
          TSError("[%s] ELIF without operators are not allowed in file: %s, lineno: %d", PLUGIN_NAME, fname.c_str(), lineno);
          return false;
        }
      } else if (rule_clause == Parser::CondClause::ELSE) {
        if (!transfer) {
          TSError("[%s] conditions not allowed in ELSE clause in file: %s, lineno: %d", PLUGIN_NAME, fname.c_str(), lineno);
          return false;
        }
      }
      if (transfer) {
        add_rule(std::move(rule));
      }
    }

    if (!rule) {
      if (!group_stack.empty()) {
        TSError("[%s] mismatched %%{GROUP} conditions in file: %s, lineno: %d", PLUGIN_NAME, fname.c_str(), lineno);
        return false;
      }

      rule = std::make_unique<RuleSet>();
      rule->set_hook(hook);
      group = rule->get_group(); // This the implicit rule group to begin with

      if (is_hook) {
        // Check if the hooks are not available for the remap mode
        if ((default_hook == TS_REMAP_PSEUDO_HOOK) &&
            ((TS_HTTP_READ_REQUEST_HDR_HOOK == hook) || (TS_HTTP_PRE_REMAP_HOOK == hook))) {
          TSError("[%s] you can not use cond %%{%s} in a remap rule, lineno: %d", PLUGIN_NAME, p.get_op().c_str(), lineno);
          return false;
        }
        continue;
      }
    } else {
      if (is_hook) {
        TSError("[%s] cond %%{%s} at %s:%d should be the first hook condition in the rule set and each rule set should contain "
                "only one hook condition",
                PLUGIN_NAME, p.get_op().c_str(), fname.c_str(), lineno);
        return false;
      }
    }

    // This is pretty ugly, but it turns out, some conditions / operators can fail (regexes), which didn't use to be the case.
    // Long term, maybe we need to percolate all this up through add_condition() / add_operator() rather than this big ugly try.
    try {
      if (p.is_cond()) {
        Condition *cond = rule->make_condition(p, filename.c_str(), lineno);

        if (!cond) {
          throw std::runtime_error("add_condition() failed");
        } else {
          auto *ngrp = dynamic_cast<ConditionGroup *>(cond);

          if (ngrp) {
            if (ngrp->closes()) {
              // Closing a group, pop the stack
              if (group_stack.empty()) {
                throw std::runtime_error("unmatched %{GROUP}");
              } else {
                delete cond; // We don't care about the closing group condition, it's a no-op
                ngrp  = group;
                group = group_stack.top();
                group_stack.pop();
                group->add_condition(ngrp); // Add the previous group to the current group's conditions
              }
            } else {
              // New group
              group_stack.push(group);
              group = ngrp;
            }
          } else {
            group->add_condition(cond);
          }
        }
      } else { // Operator
        if (!rule->add_operator(p, filename.c_str(), lineno)) {
          throw std::runtime_error("add_operator() failed");
        }
      }
    } catch (std::runtime_error &e) {
      TSError("[%s] header_rewrite configuration exception: %s in file: %s, lineno: %d", PLUGIN_NAME, e.what(), fname.c_str(),
              lineno);
      return false;
    }
  }

  if (reader->pipebuf) {
    reader->pipebuf->close();
    if (reader->pipebuf->exit_status() != 0) {
      TSError("[%s] hrw4u preprocessor exited with non-zero status (%d): %s", PLUGIN_NAME, reader->pipebuf->exit_status(),
              fname.c_str());
      return false;
    }
  }

  if (!group_stack.empty()) {
    TSError("[%s] missing final %%{GROUP:END} condition in file: %s, lineno: %d", PLUGIN_NAME, fname.c_str(), lineno);
    return false;
  }

  // Add the last rule (possibly the only rule)
  if (rule && rule->has_operator()) {
    add_rule(std::move(rule));
  }

  // Collect all resource IDs that we need
  for (size_t i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
    if (_rules[i]) {
      _resids[i] = _rules[i]->get_all_resource_ids();
    }
  }

  return true;
}

static void
setPluginControlValues(TSHttpTxn txnp)
{
  if (header_rewrite_ns::timezone != 0 || header_rewrite_ns::inboundIpSource != 0) {
    ConditionNow temporal_statement; // This could be any statement that use the private slot.
    int          slot = temporal_statement.get_txn_private_slot();

    PrivateSlotData private_data;
    private_data.raw       = reinterpret_cast<uint64_t>(TSUserArgGet(txnp, slot));
    private_data.timezone  = header_rewrite_ns::timezone;
    private_data.ip_source = header_rewrite_ns::inboundIpSource;
    TSUserArgSet(txnp, slot, reinterpret_cast<void *>(private_data.raw));
  }
}

///////////////////////////////////////////////////////////////////////////////
// Continuation
//
static int
cont_rewrite_headers(TSCont contp, TSEvent event, void *edata)
{
  auto         txnp = static_cast<TSHttpTxn>(edata);
  TSHttpHookID hook = TS_HTTP_LAST_HOOK;
  auto        *conf = static_cast<RulesConfig *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_READ_REQUEST_PRE_REMAP:
    hook = TS_HTTP_PRE_REMAP_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_TXN_START:
    hook = TS_HTTP_TXN_START_HOOK;
    setPluginControlValues(txnp);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    hook = TS_HTTP_TXN_CLOSE_HOOK;
    break;
  default:
    TSError("[%s] unknown event for this plugin", PLUGIN_NAME);
    Dbg(pi_dbg_ctl, "unknown event for this plugin");
    break;
  }

  bool reenable{true};

  if (hook != TS_HTTP_LAST_HOOK) {
    RuleSet  *rule = conf->rule(hook);
    Resources res(txnp, contp);

    // Get the resources necessary to process this event
    res.gather(conf->resid(hook), hook);

    // Evaluation of all rules. This code is sort of duplicate in DoRemap as well.
    while (rule) {
      const RuleSet::OperatorAndMods &ops = rule->eval(res);
      const OperModifiers             rt  = rule->exec(ops, res);

      if (rt & OPER_NO_REENABLE) {
        reenable = false;
      }

      if (rule->last() || (rt & OPER_LAST)) {
        break; // Conditional break, force a break with [L]
      }
      rule = rule->next.get();
    }
  }

  if (reenable) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  return 0;
}

static const struct option longopt[] = {
  {.name = "geo-db-path",       .has_arg = required_argument, .flag = nullptr, .val = 'm' },
  {.name = "timezone",          .has_arg = required_argument, .flag = nullptr, .val = 't' },
  {.name = "inbound-ip-source", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
  {.name = nullptr,             .has_arg = no_argument,       .flag = nullptr, .val = '\0'}
};

///////////////////////////////////////////////////////////////////////////////
// Initialize the InkAPI plugin for the global hooks we support.
//
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  std::string geoDBpath;
  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "m:t:i:", longopt, nullptr);

    switch (opt) {
    case 'm':
      geoDBpath = optarg;
      break;
    case 't':
      Dbg(pi_dbg_ctl, "Global timezone %s", optarg);
      if (strcmp(optarg, "LOCAL") == 0) {
        header_rewrite_ns::timezone = TIMEZONE_LOCAL;
      } else if (strcmp(optarg, "GMT") == 0) {
        header_rewrite_ns::timezone = TIMEZONE_GMT;
      } else {
        TSError("[%s] Unknown value for timezone parameter: %s", PLUGIN_NAME, optarg);
      }
      break;
    case 'i':
      Dbg(pi_dbg_ctl, "Global inbound IP source %s", optarg);
      if (strcmp(optarg, "PEER") == 0) {
        header_rewrite_ns::inboundIpSource = IP_SRC_PEER;
      } else if (strcmp(optarg, "PROXY") == 0) {
        header_rewrite_ns::inboundIpSource = IP_SRC_PROXY;
      } else {
        TSError("[%s] Unknown value for inbound-ip-source parameter: %s", PLUGIN_NAME, optarg);
      }
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (!geoDBpath.empty() && !geoDBpath.starts_with('/')) {
    geoDBpath = std::string(TSConfigDirGet()) + '/' + geoDBpath;
  }

  Dbg(pi_dbg_ctl, "Global geo db %s", geoDBpath.c_str());

  std::call_once(initHRWLibs, [&geoDBpath]() { initHRWLibraries(geoDBpath); });

  // Parse the global config file(s). All rules are just appended
  // to the "global" Rules configuration.
  auto *conf       = new RulesConfig;
  bool  got_config = false;

  for (int i = optind; i < argc; ++i) {
    // Parse the config file(s). Note that multiple config files are
    // just appended to the configurations.
    Dbg(pi_dbg_ctl, "Loading global configuration file %s", argv[i]);
    if (conf->parse_config(argv[i], TS_HTTP_READ_RESPONSE_HDR_HOOK)) {
      Dbg(pi_dbg_ctl, "Successfully loaded global config file %s", argv[i]);
      got_config = true;
    } else {
      TSError("[%s] failed to parse configuration file %s", PLUGIN_NAME, argv[i]);
    }
  }

  if (got_config) {
    TSCont contp = TSContCreate(cont_rewrite_headers, nullptr);
    TSContDataSet(contp, conf);

    // We always need to hook TXN_START to call setPluginControlValues at the beginning.
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp);

    for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
      if (conf->rule(i)) {
        Dbg(pi_dbg_ctl, "Adding global ruleset to hook=%s", TSHttpHookNameLookup(static_cast<TSHttpHookID>(i)));
        TSHttpHookAdd(static_cast<TSHttpHookID>(i), contp);
      }
    }
  } else {
    // Didn't get anything, nuke it.
    TSError("[%s] failed to parse any configuration file", PLUGIN_NAME);
    delete conf;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  Dbg(pi_dbg_ctl, "Remap plugin is successfully initialized");

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  Dbg(pi_dbg_ctl, "Instantiating a new remap.config plugin rule");

  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, need config file", PLUGIN_NAME);
    return TS_ERROR;
  }

  char *from_url = argv[0];
  char *to_url   = argv[1];

  // argv contains the "to" and "from" URLs. Skip the first so that the
  // second one poses as the program name.
  --argc;
  ++argv;

  std::string geoDBpath;
  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "m:t:i:", longopt, nullptr);

    switch (opt) {
    case 'm':
      geoDBpath = optarg;
      break;
    case 't':
      Dbg(pi_dbg_ctl, "Global timezone %s", optarg);
      if (strcmp(optarg, "LOCAL") == 0) {
        header_rewrite_ns::timezone = TIMEZONE_LOCAL;
      } else if (strcmp(optarg, "GMT") == 0) {
        header_rewrite_ns::timezone = TIMEZONE_GMT;
      } else {
        TSError("[%s] Unknown value for timezone parameter: %s", PLUGIN_NAME, optarg);
      }
      break;
    case 'i':
      Dbg(pi_dbg_ctl, "Global inbound IP source %s", optarg);
      if (strcmp(optarg, "PEER") == 0) {
        header_rewrite_ns::inboundIpSource = IP_SRC_PEER;
      } else if (strcmp(optarg, "PROXY") == 0) {
        header_rewrite_ns::inboundIpSource = IP_SRC_PROXY;
      } else {
        TSError("[%s] Unknown value for inbound-ip-source parameter: %s", PLUGIN_NAME, optarg);
      }
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if (!geoDBpath.empty()) {
    if (!geoDBpath.starts_with('/')) {
      geoDBpath = std::string(TSConfigDirGet()) + '/' + geoDBpath;
    }

    Dbg(pi_dbg_ctl, "Remap geo db %s", geoDBpath.c_str());
    std::call_once(initHRWLibs, [&geoDBpath]() { initHRWLibraries(geoDBpath); });
  }

  auto *conf = new RulesConfig;

  for (int i = optind; i < argc; ++i) {
    Dbg(pi_dbg_ctl, "Loading remap configuration file %s", argv[i]);
    if (!conf->parse_config(argv[i], TS_REMAP_PSEUDO_HOOK, from_url, to_url)) {
      TSError("[%s] Unable to create remap instance", PLUGIN_NAME);
      delete conf;
      return TS_ERROR;
    } else {
      Dbg(pi_dbg_ctl, "Successfully loaded remap config file %s", argv[i]);
    }
  }

  *ih = static_cast<void *>(conf);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  Dbg(pi_dbg_ctl, "Deleting RulesConfig");
  delete static_cast<RulesConfig *>(ih);
}

///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  // Make sure things are properly setup (this should never happen)
  if (nullptr == ih) {
    Dbg(pi_dbg_ctl, "No Rules configured, falling back to default");
    return TSREMAP_NO_REMAP;
  }

  setPluginControlValues(rh);

  TSRemapStatus rval = TSREMAP_NO_REMAP;
  auto         *conf = static_cast<RulesConfig *>(ih);

  // Go through all hooks we support, and setup the txn hook(s) as necessary
  for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
    if (conf->rule(i)) {
      TSHttpTxnHookAdd(rh, static_cast<TSHttpHookID>(i), conf->continuation());
      Dbg(pi_dbg_ctl, "Added remapped TXN hook=%s", TSHttpHookNameLookup(static_cast<TSHttpHookID>(i)));
    }
  }

  // Now handle the remap specific rules for the "remap hook" (which is not a real hook).
  // This is sufficiently different than the normal cont_rewrite_headers() callback, and
  // we can't (shouldn't) schedule this as a TXN hook.
  RuleSet  *rule = conf->rule(TS_REMAP_PSEUDO_HOOK);
  Resources res(rh, rri);

  res.gather(RSRC_CLIENT_REQUEST_HEADERS, TS_REMAP_PSEUDO_HOOK);
  while (rule) {
    const RuleSet::OperatorAndMods &ops = rule->eval(res);
    const OperModifiers             rt  = rule->exec(ops, res);

    ink_assert((rt & OPER_NO_REENABLE) == 0);

    if (res.changed_url == true) {
      rval = TSREMAP_DID_REMAP;
    }

    if (rule->last() || (rt & OPER_LAST)) {
      break; // Conditional break, force a break with [L]
    }

    rule = rule->next.get();
  }

  Dbg(dbg_ctl, "Returning from TSRemapDoRemap with status: %d", rval);
  return rval;
}
