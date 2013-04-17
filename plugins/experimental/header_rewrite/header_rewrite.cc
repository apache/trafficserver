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
//////////////////////////////////////////////////////////////////////////////////////////////
// header_rewrite: YTS plugin to do header rewrites
// --------------
//
//
#include <fstream>
#include <string>
#include <boost/algorithm/string.hpp>

#include <ts/ts.h>
#include <ts/remap.h>

#include "parser.h"
#include "ruleset.h"
#include "resources.h"

// "Defines"
const char* PLUGIN_NAME = "header_rewrite";
const char* PLUGIN_NAME_DBG = "header_rewrite_dbg";

static const char* DEFAULT_CONF_PATH = "/usr/local/etc/header_rewrite/";


// Global holding the rulesets and resource IDs
static RuleSet* all_rules[TS_HTTP_LAST_HOOK+1];
static ResourceIDs all_resids[TS_HTTP_LAST_HOOK+1];

// Helper function to add a rule to the rulesets
static bool
add_rule(RuleSet* rule) {
  if (rule && rule->has_operator()) {
    TSDebug(PLUGIN_NAME, "Adding rule to hook=%d\n", rule->get_hook());
    if (NULL == all_rules[rule->get_hook()]) {
      all_rules[rule->get_hook()] = rule;
    } else {
      all_rules[rule->get_hook()]->append(rule);
    }
    return true;
  }

  return false;
}


///////////////////////////////////////////////////////////////////////////////
// Simple "config" parser, this modifies the global above. ToDo: What happens
// on a "config" reload?
//
// Note that this isn't particularly efficient, but it's a startup time cost
// anyways (or reload for remap.config), so not really in the critical path.
//
bool
parse_config(const std::string fname, TSHttpHookID default_hook)
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
    TSError("header_rewrite: unable to open %s", filename.c_str());
    return false;
  }

  TSDebug(PLUGIN_NAME, "Loading header_rewrite config from %s", filename.c_str());

  while (!f.eof()) {
    std::string line;

    getline(f, line);
    ++lineno; // ToDo: we should probably use this for error messages ...
    TSDebug(PLUGIN_NAME, "Reading line: %d: %s", lineno, line.c_str());

    boost::trim(line);
    if (line.empty() || (line[0] == '#'))
      continue;

    Parser p(line);  // Tokenize and parse this line
    if (p.empty())
      continue;

    // If we are at the beginning of a new condition, save away the previous rule (but only if it has operators).
    if (p.is_cond() && add_rule(rule))
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
      }
    }

    if (p.is_cond()) {
      rule->add_condition(p);
    } else {
      rule->add_operator(p);
    }
  }

  // Add the last rule (possibly the only rule)
  add_rule(rule);

  // Collect all resource IDs that we need
  for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i)
    if (all_rules[i])
      all_resids[i] = all_rules[i]->get_all_resource_ids();

  return true;
}


///////////////////////////////////////////////////////////////////////////////
// Continuation
//
static int
cont_rewrite_headers(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "plugin: %d", event);

  TSHttpTxn txnp = (TSHttpTxn) edata;
  Resources res(txnp, contp);
  const RuleSet* rule = NULL;
  TSHttpHookID hook = TS_HTTP_LAST_HOOK;

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
    TSError("header_rewrite: unknown event for this plugin");
    TSDebug(PLUGIN_NAME, "unknown event for this plugin");
    break;
  }

  if (hook != TS_HTTP_LAST_HOOK) {
    res.gather(all_resids[hook], hook);
    rule = all_rules[hook];

    // Evaluation
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
  info.vendor_name = (char*)"";
  info.support_email = (char*)"";

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_3_0 , &info)) {
    TSError("header_rewrite: plugin registration failed.\n"); 
  }

  TSDebug(PLUGIN_NAME, "number of arguments: %d", argc);
  if (argc != 2) {
    TSError("usage: %s <config-file>\n", argv[0] );
    assert(argc == 2);
  }

  // Initialize the globals
  for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i) {
    all_rules[i] = NULL;
    all_resids[i] = RSRC_NONE;
  }

  // Parse the config file
  if (parse_config(argv[1], TS_HTTP_READ_RESPONSE_HDR_HOOK)) {
    for (int i=TS_HTTP_READ_REQUEST_HDR_HOOK; i<TS_HTTP_LAST_HOOK; ++i) {
      if (all_rules[i]) {
        TSDebug(PLUGIN_NAME, "adding hook: %d", i);
        TSHttpHookAdd(static_cast<TSHttpHookID>(i), TSContCreate(cont_rewrite_headers, NULL));
      }
    }
  } else {
    TSError("header_rewrite: failed to parse configuration file");
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
TSRemapNewInstance(int argc, char *argv[], void **ih, char *, int) // UNUSED:  char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "initializing the remap plugin header_rewrite");

  if (argc < 3) {
    TSError("Unable to create remap instance, need config file");
    return TS_ERROR;
  }

  // TODO: this is a big ugly, we use a pseudo hook for parsing the config for a 
  // remap instantiation.
  all_rules[TS_REMAP_PSEUDO_HOOK] = NULL;
  if (!parse_config(argv[2], TS_REMAP_PSEUDO_HOOK)) {
    TSError("Unable to create remap instance");
    return TS_ERROR;
  }

  *ih = all_rules[TS_REMAP_PSEUDO_HOOK];
  all_rules[TS_REMAP_PSEUDO_HOOK] = NULL;

  TSDebug(PLUGIN_NAME, "successfully initialize the header_rewrite plugin");
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  RuleSet* rule = static_cast<RuleSet*>(ih);

  delete rule;
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
    RuleSet* rule = (RuleSet*)ih;
    Resources res((TSHttpTxn)rh, rri);

    // TODO: This might be suboptimal, do we always need the client request
    // headers in a remap plugin?
    res.gather(RSRC_CLIENT_REQUEST_HEADERS, TS_REMAP_PSEUDO_HOOK);

    // Evaluation
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

