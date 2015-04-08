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
//
// Main entry points for the plugin hooks etc.
//
#include <ts/ts.h>
#include <ts/remap.h>
#include <stdio.h>
#include <string.h>

#include "lulu.h"


// Virtual base class for all policies
class PromotionPolicy
{
public:
  virtual ~PromotionPolicy(){};
  virtual bool parseArguments(int argc, char *argv[]) = 0;
  virtual bool doPromote(TSHttpTxn txnp) const = 0;

  const char *
  getPolicyName() const
  {
    return _policy_name;
  }

  void
  usage()
  {
    TSDebug(PLUGIN_NAME, "Usage: @plugin=%s.so @pparam=%s @pparam=<n>%%", PLUGIN_NAME, getPolicyName());
    TSError("Usage: @plugin=%s.so @pparam=%s @pparam=<n>%%", PLUGIN_NAME, getPolicyName());
  }

private:
  const char *_policy_name = "virtual";
};


// This is the simplest of all policies, just give each request a (small)
// percentage chance to be promoted to cache. Usage:
//
//   @plugin=cache_promot.so @pparam=chance @pparam=10%
class ChancePolicy : public PromotionPolicy
{
public:
  ChancePolicy() : _percent(0.0)
  {
    // This doesn't have to be perfect, since this is just statistical sampling.
    // coverity[dont_call]
    srand48((long)time(NULL));
  }

  bool
  parseArguments(int argc, char *argv[])
  {
    if (4 != argc) {
      usage();
      return false;
    }

    _percent = strtof(argv[3], NULL) / 100.0;
    if (_percent < 0 || _percent > 1) {
      usage();
      return false;
    }

    TSDebug(PLUGIN_NAME, "created remap rule, with chance policy of %s (%f)", argv[3], _percent);
    return true;
  }


  bool doPromote(TSHttpTxn /* txnp ATS_UNUSED */) const
  {
    double r = drand48();

    TSDebug(PLUGIN_NAME, "evaluating ChancePolicy::doPromote(), %f > %f ?", _percent, r);
    // coverity[dont_call]
    return _percent > r;
  }

private:
  float _percent;
  const char *_policy_name = "chance";
};


// This holds the configuration for a remap rule
class PromotionConfig
{
public:
  PromotionConfig() : _policy(NULL){};

  ~PromotionConfig() { delete _policy; }

  const PromotionPolicy *
  getPolicy() const
  {
    return _policy;
  }

  // Parse the command line arguments to the plugin, and instantiate the appropriate policy
  bool
  factory(int argc, char *argv[])
  {
    if (argc < 3) {
      TSError("Unable to create remap instance, need a percentage parameter");
      return false;
    }

    if (0 == strncasecmp(argv[2], "chance", 6)) {
      _policy = new ChancePolicy();
    } else {
      TSError("Unknown policy: %s", argv[2]);
      return false;
    }

    if (!_policy->parseArguments(argc, argv)) {
      delete _policy;
      _policy = NULL;
      return false;
    }

    return true;
  }

private:
  PromotionPolicy *_policy;
};


//////////////////////////////////////////////////////////////////////////////
// Main "plugin", a TXN hook in the TS_HTTP_READ_CACHE_HDR_HOOK. Unless the
// policy allows caching, we will turn off the cache from here on for the TXN.
//
static int
cont_handle_policy(TSCont contp, TSEvent event, void *edata)
{
  // ToDo: If we want to support per-remap configurations, we have to pass along the data here
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  PromotionConfig *config = static_cast<PromotionConfig *>(TSContDataGet(contp));
  int obj_status;

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    if (TS_ERROR != TSHttpTxnCacheLookupStatusGet(txnp, &obj_status)) {
      switch (obj_status) {
      case TS_CACHE_LOOKUP_MISS:
      case TS_CACHE_LOOKUP_SKIPPED:
        if (!config->getPolicy()->doPromote(txnp)) {
          TSDebug(PLUGIN_NAME, "cache-status is %d, and turning off the cache (not promoted)", obj_status);
          TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
        } else {
          TSDebug(PLUGIN_NAME, "cache-status is %d, and leaving cache on (promoted)", obj_status);
        }
        break;
      default:
        // Do nothing, just let it handle the lookup.
        TSDebug(PLUGIN_NAME, "cache hit, so leaving it alone");
        break;
      }
    }
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    TSHttpTxnServerRespNoStoreSet(txnp, 1);
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unhandled event %d", (int)event);
    break;
  }

  // Reenable and continue with the state machine.
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[tsremap_init] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size - 1, "[tsremap_init] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "remap plugin is successfully initialized");
  return TS_SUCCESS; /* success */
}


TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf */, int /* errbuf_size */)
{
  PromotionConfig *config = new PromotionConfig;
  TSCont contp = TSContCreate(cont_handle_policy, TSMutexCreate());

  config->factory(argc, argv);
  TSContDataSet(contp, static_cast<void *>(config));
  *ih = static_cast<void *>(contp);

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSCont contp = static_cast<TSCont>(ih);
  PromotionConfig *config = static_cast<PromotionConfig *>(TSContDataGet(contp));

  delete config;
  TSContDestroy(contp);
}


///////////////////////////////////////////////////////////////////////////////
// Schedule the cache-read continuation for this remap rule.
//
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo * /* ATS_UNUSED rri */)
{
  if (NULL == ih) {
    TSDebug(PLUGIN_NAME, "No ACLs configured, this is probably a plugin bug");
  } else {
    TSCont contp = static_cast<TSCont>(ih);

    TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
  }

  return TSREMAP_NO_REMAP;
}
