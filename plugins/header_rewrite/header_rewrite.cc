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
#include <boost/algorithm/string.hpp>

#include "ts/ts.h"
#include "ts/remap.h"

#include "parser.h"
#include "ruleset.h"
#include "resources.h"

// Debugs
const char* PLUGIN_NAME = "header_rewrite";
const char* PLUGIN_NAME_DBG = "header_rewrite_dbg";

static const char* DEFAULT_CONF_PATH = "/usr/local/etc/header_rewrite/";

// Simple wrapper around a configuration file / set. This is useful such that
// we can reuse most of the code for both global and per-remap rule sets.
struct RulesConfig
{
  RulesConfig()
  {
    memset(rules, 0, sizeof(rules));
    memset(resids, 0, sizeof(resids));
  }

  ~RulesConfig()
  {
    for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i)
      delete rules[i];
  }

  RuleSet* rules[TS_HTTP_LAST_HOOK+1];
  ResourceIDs resids[TS_HTTP_LAST_HOOK+1];
};

// Helper function to add a rule to the rulesets
static bool
add_rule(RuleSet* rule, RulesConfig *conf) {
  if (rule && rule->has_operator()) {
    TSDebug(PLUGIN_NAME, "Adding rule to hook=%d\n", rule->get_hook());
    if (NULL == conf->rules[rule->get_hook()]) {
      conf->rules[rule->get_hook()] = rule;
    } else {
      conf->rules[rule->get_hook()]->append(rule);
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
parse_config(const std::string fname, TSHttpHookID default_hook, RulesConfig *conf)
{
  RuleSet* rule = NULL;
  std::string filename = fname;
  std::ifstream f;
  int lineno = 0;

  // Try appending the default conf path if the fname doesn't exist.
  if (0 != access(filename.c_str(), R_OK)) {
    filename = DEFAULT_CONF_PATH;
    filename += fname;
  }

  f.open(filename.c_str(), std::ios::in);
  if (!f.is_open()) {
    TSError("%s: unable to open %s", PLUGIN_NAME, filename.c_str());
    return false;
  }

  TSDebug(PLUGIN_NAME, "Loading header_rewrite config from %s", filename.c_str());

  while (!f.eof()) {
    std::string line;

    getline(f, line);
    ++lineno; // ToDo: we should probably use this for error messages ...
    TSDebug(PLUGIN_NAME_DBG, "Reading line: %d: %s", lineno, line.c_str());

    boost::trim(line);
    if (line.empty() || (line[0] == '#'))
      continue;

    Parser p(line);  // Tokenize and parse this line
    if (p.empty())
      continue;

    // If we are at the beginning of a new condition, save away the previous rule (but only if it has operators).
    if (p.is_cond() && add_rule(rule, conf))
      rule = NULL;

    if (NULL == rule) {
      rule = new RuleSet();
      rule->set_hook(default_hook);

      // Special case for specifying the HOOK this rule applies to.
      // These can only be at the beginning of a rule, and have an implicit [AND].
      if (p.cond_op_is("READ_RESPONSE_HDR_HOOK")) {
        rule->set_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
        continue;
      } else if (p.cond_op_is("READ_REQUEST_HDR_HOOK")) {
        rule->set_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
        continue;
      } else if (p.cond_op_is("READ_REQUEST_PRE_REMAP_HOOK")) {
        rule->set_hook(TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK);
        continue;
      } else if (p.cond_op_is("SEND_REQUEST_HDR_HOOK")) {
        rule->set_hook(TS_HTTP_SEND_REQUEST_HDR_HOOK);
        continue;
      } else if (p.cond_op_is("SEND_RESPONSE_HDR_HOOK")) {
        rule->set_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
        continue;
      } else if (p.cond_op_is("REMAP_PSEUDO_HOOK")) {
        rule->set_hook(TS_REMAP_PSEUDO_HOOK);
        continue;
      }
    }

    if (p.is_cond()) {
      rule->add_condition(p);
    } else {
      rule->add_operator(p);
    }
  }

  // Add the last rule (possibly the only rule)
  add_rule(rule, conf);

  // Collect all resource IDs that we need
  for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i) {
    if (conf->rules[i]) {
      conf->resids[i] = conf->rules[i]->get_all_resource_ids();
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
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSHttpHookID hook = TS_HTTP_LAST_HOOK;
  RulesConfig* conf = static_cast<RulesConfig*>(TSContDataGet(contp));

  // Get the resources necessary to process this event
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    hook = TS_HTTP_READ_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_READ_REQUEST_PRE_REMAP:
    hook = TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    break;
  default:
    TSError("%s: unknown event for this plugin", PLUGIN_NAME);
    TSDebug(PLUGIN_NAME, "unknown event for this plugin");
    break;
  }

  if (hook != TS_HTTP_LAST_HOOK) {
    const RuleSet* rule = conf->rules[hook];
    Resources res(txnp, contp);

    res.gather(conf->resids[hook], hook);

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

  info.plugin_name = (char*)PLUGIN_NAME;
  info.vendor_name = (char*)"Apache Software Foundation";
  info.support_email = (char*)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_3_0 , &info)) {
    TSError("%s: plugin registration failed.\n", PLUGIN_NAME);
  }
  TSDebug(PLUGIN_NAME, "number of arguments: %d", argc);

  // Parse the global config file(s). All rules are just appended
  // to the "global" Rules configuration.
  RulesConfig* conf = new RulesConfig;
  bool got_config = false;

  for (int i=1; i < argc; ++i) {
    // Parse the config file(s). Note that multiple config files are
    // just appended to the configurations.
    if (!parse_config(argv[i], TS_HTTP_READ_RESPONSE_HDR_HOOK, conf)) {
      TSError("header_rewrite: failed to parse configuration file %s", argv[argc]);
    } else {
      got_config = true;
    }
  }

  if (got_config) {
    TSCont contp = TSContCreate(cont_rewrite_headers, NULL);
    TSContDataSet(contp, conf);

    for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i) {
      if (conf->rules[i]) {
        TSDebug(PLUGIN_NAME, "adding hook: %d", i);
        TSHttpHookAdd(static_cast<TSHttpHookID>(i), contp);
      }
    }
  } else {
    // Didn't get anything, nuke it.
    TSError("%s: failed to parse configuration file", PLUGIN_NAME);
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
    snprintf(errbuf, errbuf_size - 1, "[TSRemapInit] - Incorrect API version %ld.%ld",
             api_info->tsremap_version >> 16, (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS;
}


TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  TSDebug(PLUGIN_NAME, "initializing the remap plugin header_rewrite");

  if (argc < 3) {
    TSError("%s: Unable to create remap instance, need config file", PLUGIN_NAME);
    return TS_ERROR;
  }

  RulesConfig* conf = new RulesConfig;

  for (int i=2; i < argc; ++i) {
    if (!parse_config(argv[i], TS_REMAP_PSEUDO_HOOK, conf)) {
      TSError("%s: Unable to create remap instance", PLUGIN_NAME);
      return TS_ERROR;
    }
  }

  *ih = conf;
  TSDebug(PLUGIN_NAME, "added header_rewrite remap rule set");

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  RulesConfig* conf = static_cast<RulesConfig*>(ih);

  delete conf;
}


///////////////////////////////////////////////////////////////////////////////
// This is the main "entry" point for the plugin, called for every request.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  TSRemapStatus rval = TSREMAP_NO_REMAP;

  if (NULL == ih) {
    TSDebug(PLUGIN_NAME, "No Rules configured, falling back to default");
    return rval;
  } else {
    RulesConfig* conf = static_cast<RulesConfig*>(ih);

    // ToDo: Would it be faster / better to register the global hook for all
    // hooks, as we do in header_filter, regardless if there's a known config
    // for that hook? For now, we assume it's cheaper / faster to create a
    // TXN hook when necessary.

    // Go through all hooks we support, and setup the txn hook(s) as necessary
    for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i) {
      TSCont contp = NULL;

      if (conf->rules[i]) {
        if (NULL == contp) {
          contp = TSContCreate(cont_rewrite_headers, NULL);
          TSContDataSet(contp, conf);
        }
        TSHttpTxnHookAdd(rh, static_cast<TSHttpHookID>(i), contp);
        TSDebug(PLUGIN_NAME, "activated transaction hook via remap.config: new hook=%d", i);
      }
    }

    // Now handle the remap specific rules for the "remap hook" (which is not a real hook).
    // This avoids scheduling an additional continuation for a very common case.
    RuleSet* rule = conf->rules[TS_REMAP_PSEUDO_HOOK];
    Resources res(rh, rri);

    // res.gather(conf->resids[TS_REMAP_PSEUDO_HOOK], TS_REMAP_PSEUDO_HOOK);
    res.gather(RSRC_CLIENT_REQUEST_HEADERS, TS_REMAP_PSEUDO_HOOK);

    // Evaluation. This code is duplicated sort of, should we merge with the continuation evaluator ?
    while (rule) {
      if (rule->eval(res)) {
        OperModifiers rt = rule->exec(res);

        if (res.changed_url == true)
          rval = TSREMAP_DID_REMAP;

        if (rule->last() || (rt & OPER_LAST)) {
          break; // Conditional break, force a break with [L]
        }
      }
      rule = rule->next;
    }
  }

  TSDebug(PLUGIN_NAME, "returing with status: %d", rval);
  return rval;
}

