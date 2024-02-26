/**
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

  Copyright 2019, Oath Inc.
*/

#include <string>
#include <map>
#include <numeric>
#include <getopt.h>

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <yaml-cpp/yaml.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include <ts/remap.h>
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
Config::YamlCache Remap_Cfg_Cache;
/// Static configuration for use in remap invocation when there is no global configuration.
std::shared_ptr Remap_Static_Config = std::make_shared<Config>();
/* ------------------------------------------------------------------------------------ */
class RemapContext
{
  using self_type = RemapContext; ///< Self reference type.
public:
  std::shared_ptr<Config> rule_cfg; ///< Configuration for a specific rule in @a r_cfg;
};
/* ------------------------------------------------------------------------------------ */
TSReturnCode
TSRemapInit(TSRemapInterface *, char *errbuff, int errbuff_size)
{
  G.reserve_txn_arg();
  if (!G._preload_errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
    swoc::FixedBufferWriter w{errbuff, size_t(errbuff_size)};
    w.print("{}: startup issues, see error log for details.\0", Config::PLUGIN_NAME);
  }
  return TS_SUCCESS;
};

#if TS_VERSION_MAJOR >= 8
void
TSRemapPostConfigReload(TSRemapReloadStatus)
{
  Remap_Cfg_Cache.clear();
}
#endif

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuff, int errbuff_size)
{
  swoc::FixedBufferWriter w(errbuff, errbuff_size);

  if (argc < 3) {
    w.print("{} plugin requires at least one configuration file parameter.\0", Config::PLUGIN_NAME);
    return TS_ERROR;
  }

  auto cfg = std::make_shared<Config>();
  swoc::MemSpan<char const *> rule_args{swoc::MemSpan<char *>(argv, argc).rebind<char const *>()};
  cfg->mark_as_remap();
  Errata errata = cfg->load_cli_args(cfg, rule_args,
                                     2
#if TS_VERSION_MAJOR >= 8
                                     // pre ATS 8 doesn't support remap reload callbacks, so the config cache can't be used.
                                     ,
                                     &Remap_Cfg_Cache
#endif
  );

  if (!errata.is_ok()) {
    std::string text;
    TSError("%s", swoc::bwprint(text, "{}", errata).c_str());
    w.print("Error while parsing configuration for {} - see diagnostic log for more detail.\0", Config::PLUGIN_TAG);
    return TS_ERROR;
  }

  G._remap_ctx_storage_required += cfg->reserved_ctx_storage_size();
  *ih                            = new RemapContext{cfg};
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txn, TSRemapRequestInfo *rri)
{
  auto r_ctx = static_cast<RemapContext *>(ih);
  // This is a hack because errors reported during TSRemapNewInstance are ignored
  // leaving broken instances around. Gah. Need to fix remap loading to actually
  // check for new instance errors.
  if (nullptr == r_ctx) {
    return TSREMAP_NO_REMAP;
  }

  Context *ctx = static_cast<Context *>(ts::HttpTxn(txn).arg(G.TxnArgIdx));
  if (nullptr == ctx) {
    ctx = new Context(Remap_Static_Config);
    ctx->enable_hooks(txn); // This sets G.TxnArgIdx
  }
  ctx->invoke_for_remap(*(r_ctx->rule_cfg), rri);

  return ctx->_remap_status;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto r_ctx = static_cast<RemapContext *>(ih);
  if (r_ctx) {
    G._remap_ctx_storage_required -= r_ctx->rule_cfg->reserved_ctx_storage_size();
    delete r_ctx;
  }
}
