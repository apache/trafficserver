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
#include <string>

#include "ts/ts.h"
#include "ts/remap.h"

#include "tscore/ink_atomic.h"

#include "parser.h"
#include "ruleset.h"
#include "resources.h"

// Debugs
const char PLUGIN_NAME[]     = "header_rewrite";
const char PLUGIN_NAME_DBG[] = "dbg_header_rewrite";

// Geo information, currently only Maxmind. These have to be initialized when the plugin loads.
#if HAVE_GEOIP_H
#include <GeoIP.h>

GeoIP *gGeoIP[NUM_DB_TYPES];

static void
initGeoIP()
{
  GeoIPDBTypes dbs[] = {GEOIP_COUNTRY_EDITION, GEOIP_COUNTRY_EDITION_V6, GEOIP_ASNUM_EDITION, GEOIP_ASNUM_EDITION_V6};

  for (unsigned i = 0; i < sizeof(dbs) / sizeof(dbs[0]); ++i) {
    if (!gGeoIP[dbs[i]] && GeoIP_db_avail(dbs[i])) {
      // GEOIP_STANDARD seems to break threaded apps...
      gGeoIP[dbs[i]] = GeoIP_open_type(dbs[i], GEOIP_MMAP_CACHE);

      char *db_info = GeoIP_database_info(gGeoIP[dbs[i]]);
      TSDebug(PLUGIN_NAME, "initialized GeoIP-DB[%d] %s", dbs[i], db_info);
      free(db_info);
    }
  }
}

#else

static void
initGeoIP()
{
}
#endif

// Forward declaration for the main continuation.
static int cont_rewrite_headers(TSCont, TSEvent, void *);

// Simple wrapper around a configuration file / set. This is useful such that
// we can reuse most of the code for both global and per-remap rule sets.
class RulesConfig
{
public:
  RulesConfig()
  {
    TSDebug(PLUGIN_NAME_DBG, "RulesConfig CTOR");
    memset(_rules, 0, sizeof(_rules));
    memset(_resids, 0, sizeof(_resids));

    _cont = TSContCreate(cont_rewrite_headers, nullptr);
    TSContDataSet(_cont, static_cast<void *>(this));
  }

  ~RulesConfig()
  {
    TSDebug(PLUGIN_NAME_DBG, "RulesConfig DTOR");
    for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i <= TS_HTTP_LAST_HOOK; ++i) {
      delete _rules[i];
    }
    TSContDestroy(_cont);
  }

  TSCont
  continuation() const
  {
    return _cont;
  }

  ResourceIDs
  resid(int hook) const
  {
    return _resids[hook];
  }
  RuleSet *
  rule(int hook) const
  {
    return _rules[hook];
  }

  bool parse_config(const std::string &fname, TSHttpHookID default_hook);

private:
  bool add_rule(RuleSet *rule);

  TSCont _cont;
  RuleSet *_rules[TS_HTTP_LAST_HOOK + 1];
  ResourceIDs _resids[TS_HTTP_LAST_HOOK + 1];
};

// Helper function to add a rule to the rulesets
bool
RulesConfig::add_rule(RuleSet *rule)
{
  if (rule && rule->has_operator()) {
    TSDebug(PLUGIN_NAME_DBG, "   Adding rule to hook=%s", TSHttpHookNameLookup(rule->get_hook()));
    if (nullptr == _rules[rule->get_hook()]) {
      _rules[rule->get_hook()] = rule;
    } else {
      _rules[rule->get_hook()]->append(rule);
    }
    return true;
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Config parser, use to parse both the global, and per-remap, configurations.
//
// Note that this isn't particularly efficient, but it's a startup time cost
// anyways (or reload for remap.config), so not really in the critical path.
//
bool
RulesConfig::parse_config(const std::string &fname, TSHttpHookID default_hook)
{
  RuleSet *rule = nullptr;
  std::string filename;
  std::ifstream f;
  int lineno = 0;

  if (0 == fname.size()) {
    TSError("[%s] no config filename provided", PLUGIN_NAME);
    return false;
  }

  if (fname[0] != '/') {
    filename = TSConfigDirGet();
    filename += "/" + fname;
  } else {
    filename = fname;
  }

  f.open(filename.c_str(), std::ios::in);
  if (!f.is_open()) {
    TSError("[%s] unable to open %s", PLUGIN_NAME, filename.c_str());
    return false;
  }

  while (!f.eof()) {
    std::string line;

    getline(f, line);
    ++lineno; // ToDo: we should probably use this for error messages ...
    TSDebug(PLUGIN_NAME_DBG, "Reading line: %d: %s", lineno, line.c_str());

    while (std::isspace(line[0])) {
      line.erase(0, 1);
    }

    while (line.length() > 0 && std::isspace(line[line.length() - 1])) {
      line.erase(line.length() - 1, 1);
    }

    if (line.empty() || (line[0] == '#')) {
      continue;
    }

    Parser p(line); // Tokenize and parse this line
    if (p.empty()) {
      continue;
    }

    // If we are at the beginning of a new condition, save away the previous rule (but only if it has operators).
    if (p.is_cond() && add_rule(rule)) {
      rule = nullptr;
    }

    TSHttpHookID hook = default_hook;
    bool is_hook      = p.cond_is_hook(hook); // This updates the hook if explicitly set, if not leaves at default

    if (nullptr == rule) {
      rule = new RuleSet();
      rule->set_hook(hook);

      if (is_hook) {
        // Check if the hooks are not available for the remap mode
        if ((default_hook == TS_REMAP_PSEUDO_HOOK) &&
            ((TS_HTTP_READ_REQUEST_HDR_HOOK == hook) || (TS_HTTP_PRE_REMAP_HOOK == hook))) {
          TSError("[%s] you can not use cond %%{%s} in a remap rule", PLUGIN_NAME, p.get_op().c_str());
          delete rule;
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

    if (p.is_cond()) {
      if (!rule->add_condition(p, filename.c_str(), lineno)) {
        delete rule;
        return false;
      }
    } else {
      if (!rule->add_operator(p, filename.c_str(), lineno)) {
        delete rule;
        return false;
      }
    }
  }

  // Add the last rule (possibly the only rule)
  add_rule(rule);

  // Collect all resource IDs that we need
  for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
    if (_rules[i]) {
      _resids[i] = _rules[i]->get_all_resource_ids();
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Continuation
//
static int
cont_rewrite_headers(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp    = static_cast<TSHttpTxn>(edata);
  TSHttpHookID hook = TS_HTTP_LAST_HOOK;
  RulesConfig *conf = static_cast<RulesConfig *>(TSContDataGet(contp));

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
  default:
    TSError("[%s] unknown event for this plugin", PLUGIN_NAME);
    TSDebug(PLUGIN_NAME, "unknown event for this plugin");
    break;
  }

  if (hook != TS_HTTP_LAST_HOOK) {
    const RuleSet *rule = conf->rule(hook);
    Resources res(txnp, contp);

    // Get the resources necessary to process this event
    res.gather(conf->resid(hook), hook);

    // Evaluation of all rules. This code is sort of duplicate in DoRemap as well.
    while (rule) {
      if (rule->eval(res)) {
        OperModifiers rt = rule->exec(res);

        if (rule->last() || (rt & OPER_LAST)) {
          break; // Conditional break, force a break with [L]
        }
      }
      rule = rule->next;
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

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
  }

  // Parse the global config file(s). All rules are just appended
  // to the "global" Rules configuration.
  RulesConfig *conf = new RulesConfig;
  bool got_config   = false;

  initGeoIP();

  for (int i = 1; i < argc; ++i) {
    // Parse the config file(s). Note that multiple config files are
    // just appended to the configurations.
    TSDebug(PLUGIN_NAME, "Loading global configuration file %s", argv[i]);
    if (conf->parse_config(argv[i], TS_HTTP_READ_RESPONSE_HDR_HOOK)) {
      TSDebug(PLUGIN_NAME, "Successfully loaded global config file %s", argv[i]);
      got_config = true;
    } else {
      TSError("[header_rewrite] failed to parse configuration file %s", argv[i]);
    }
  }

  if (got_config) {
    TSCont contp = TSContCreate(cont_rewrite_headers, nullptr);
    TSContDataSet(contp, conf);

    for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
      if (conf->rule(i)) {
        TSDebug(PLUGIN_NAME, "Adding global ruleset to hook=%s", TSHttpHookNameLookup((TSHttpHookID)i));
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
  if (!api_info) {
    strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  initGeoIP();
  TSDebug(PLUGIN_NAME, "Remap plugin is successfully initialized");

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  TSDebug(PLUGIN_NAME, "Instantiating a new remap.config plugin rule");

  if (argc < 3) {
    TSError("[%s] Unable to create remap instance, need config file", PLUGIN_NAME);
    return TS_ERROR;
  }

  RulesConfig *conf = new RulesConfig;

  for (int i = 2; i < argc; ++i) {
    TSDebug(PLUGIN_NAME, "Loading remap configuration file %s", argv[i]);
    if (!conf->parse_config(argv[i], TS_REMAP_PSEUDO_HOOK)) {
      TSError("[%s] Unable to create remap instance", PLUGIN_NAME);
      delete conf;
      return TS_ERROR;
    } else {
      TSDebug(PLUGIN_NAME, "Successfully loaded remap config file %s", argv[i]);
    }
  }

  // For debugging only
  if (TSIsDebugTagSet(PLUGIN_NAME)) {
    for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
      if (conf->rule(i)) {
        TSDebug(PLUGIN_NAME, "Adding remap ruleset to hook=%s", TSHttpHookNameLookup((TSHttpHookID)i));
      }
    }
  }

  *ih = static_cast<void *>(conf);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
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
    TSDebug(PLUGIN_NAME, "No Rules configured, falling back to default");
    return TSREMAP_NO_REMAP;
  }

  TSRemapStatus rval = TSREMAP_NO_REMAP;
  RulesConfig *conf  = static_cast<RulesConfig *>(ih);

  // Go through all hooks we support, and setup the txn hook(s) as necessary
  for (int i = TS_HTTP_READ_REQUEST_HDR_HOOK; i < TS_HTTP_LAST_HOOK; ++i) {
    if (conf->rule(i)) {
      TSHttpTxnHookAdd(rh, static_cast<TSHttpHookID>(i), conf->continuation());
      TSDebug(PLUGIN_NAME, "Added remapped TXN hook=%s", TSHttpHookNameLookup((TSHttpHookID)i));
    }
  }

  // Now handle the remap specific rules for the "remap hook" (which is not a real hook).
  // This is sufficiently differen than the normal cont_rewrite_headers() callback, and
  // we can't (shouldn't) schedule this as a TXN hook.
  RuleSet *rule = conf->rule(TS_REMAP_PSEUDO_HOOK);
  Resources res(rh, rri);

  res.gather(RSRC_CLIENT_REQUEST_HEADERS, TS_REMAP_PSEUDO_HOOK);
  while (rule) {
    if (rule->eval(res)) {
      OperModifiers rt = rule->exec(res);

      if (res.changed_url == true) {
        rval = TSREMAP_DID_REMAP;
      }

      if (rule->last() || (rt & OPER_LAST)) {
        break; // Conditional break, force a break with [L]
      }
    }
    rule = rule->next;
  }

  TSDebug(PLUGIN_NAME_DBG, "Returning from TSRemapDoRemap with status: %d", rval);
  return rval;
}
