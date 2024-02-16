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
#include <shared_mutex>

#include <swoc/TextView.h>
#include <swoc/bwf_std.h>
#include <swoc/bwf_ex.h>

#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */

Global G;
extern std::string glob_to_rxp(TextView glob);

const std::string Config::GLOBAL_ROOT_KEY{"txn_box"};
const std::string Config::REMAP_ROOT_KEY{"."};

Hook
Convert_TS_Event_To_TxB_Hook(TSEvent ev)
{
  static const std::map<TSEvent, Hook> table{
    {TS_EVENT_HTTP_TXN_START,         Hook::TXN_START },
    {TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ      },
    {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ      },
    {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP      },
    {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP      },
    {TS_EVENT_HTTP_PRE_REMAP,         Hook::PRE_REMAP },
    {TS_EVENT_HTTP_POST_REMAP,        Hook::POST_REMAP},
    {TS_EVENT_HTTP_TXN_CLOSE,         Hook::TXN_CLOSE }
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

namespace
{
Config::Handle Plugin_Config;
std::shared_mutex Plugin_Config_Mutex; // safe updating of the shared ptr.
/// Start time of the currently active reload. If the default value then no reload is active.
/// @note A time instead of a @c bool for better diagnostics.
/// @internal Older gcc versions don't like the default constructor when used with @c atomic.
static constexpr std::chrono::system_clock::time_point SYSTEM_CLOCK_NULL_TIME;
std::atomic<std::chrono::system_clock::time_point> Plugin_Reloading{SYSTEM_CLOCK_NULL_TIME};

// Get a shared pointer to the configuration safely against updates.
Config::Handle
scoped_plugin_config()
{
  std::shared_lock lock(Plugin_Config_Mutex);
  auto zret = Plugin_Config; // Be *completely* sure this is done under lock.
  return zret;
}

} // namespace
/* ------------------------------------------------------------------------------------ */
void
Global::reserve_txn_arg()
{
  if (G.TxnArgIdx < 0) {
    auto &&[idx, errata]{ts::HttpTxn::reserve_arg(Config::GLOBAL_ROOT_KEY, "Transaction Box")};
    if (!errata.is_ok()) {
      _preload_errata.note(errata);
    } else {
      TxnArgIdx = idx;
    }
  }
}
/* ------------------------------------------------------------------------------------ */
// Global callback, thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int
CB_Txn_Start(TSCont, TSEvent, void *payload)
{
  auto txn{reinterpret_cast<TSHttpTxn>(payload)};
  if (auto cfg = scoped_plugin_config(); cfg) {
    Context *ctx = new Context(cfg);
    ctx->enable_hooks(txn);
  }
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

void
Task_ConfigReload()
{
  auto t_null = SYSTEM_CLOCK_NULL_TIME;
  auto t0     = std::chrono::system_clock::now();
  if (Plugin_Reloading.compare_exchange_strong(t_null, t0)) {
    std::shared_ptr cfg = std::make_shared<Config>();
    auto errata         = cfg->load_cli_args(cfg, G._args, 1);
    if (errata.is_ok()) {
      std::unique_lock lock(Plugin_Config_Mutex);
      Plugin_Config = cfg;
    } else {
      std::string err_str;
      swoc::bwprint(err_str, "{}: Failed to reload configuration.\n{}", Config::PLUGIN_NAME, errata);
      TSError("%s", err_str.c_str());
    }
    Plugin_Reloading = t_null;
    auto delta       = std::chrono::system_clock::now() - t0;
    std::string text;
    TS_DBG("%s", swoc::bwprint(text, "{} files loaded in {} ms.", Plugin_Config->file_count(),
                               std::chrono::duration_cast<std::chrono::milliseconds>(delta).count())
                   .c_str());
  } else { // because the exchange failed, @a t_null is the value that was in @a Plugin_Loading
    std::string err_str;
    swoc::bwprint(err_str, "{}: Reload requested while previous reload at {} still active", Config::PLUGIN_NAME,
                  swoc::bwf::Date(std::chrono::system_clock::to_time_t(t_null)));
    TSError("%s", err_str.c_str());
  }
}

int
CB_TxnBoxMsg(TSCont, TSEvent, void *data)
{
  static constexpr TextView TAG{"txn_box."};
  static constexpr TextView RELOAD("reload");
  auto msg = static_cast<TSPluginMsg *>(data);
  if (TextView tag{msg->tag, strlen(msg->tag)}; tag.starts_with_nocase(TAG)) {
    tag.remove_prefix(TAG.size());
    if (0 == strcasecmp(tag, RELOAD)) {
      ts::PerformAsTask(&Task_ConfigReload);
    }
  }
  return TS_SUCCESS;
}

int
CB_TxnBoxShutdown(TSCont, TSEvent, void *)
{
  TS_DBG("Global shut down");
  std::unique_lock lock(Plugin_Config_Mutex);
  Plugin_Config.reset();
  return TS_SUCCESS;
}

Errata
TxnBoxInit()
{
  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media", "solidwallofcode@verizonmedia.com"};

  Plugin_Config = std::make_shared<Config>();
  auto t0       = std::chrono::system_clock::now();
  auto errata   = Plugin_Config->load_cli_args(Plugin_Config, G._args, 1);
  if (!errata.is_ok()) {
    return errata;
  }
  auto delta = std::chrono::system_clock::now() - t0;
  std::string text;
  TS_DBG("%s", swoc::bwprint(text, "{} files loaded in {} ms.", Plugin_Config->file_count(),
                             std::chrono::duration_cast<std::chrono::milliseconds>(delta).count())
                 .c_str());

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
    G.reserve_txn_arg();
  } else {
    errata.note(R"({}: plugin registration failed.)", Config::PLUGIN_TAG);
    return errata;
  }
  return {};
}

void
TSPluginInit(int argc, char const *argv[])
{
  for (int idx = 0; idx < argc; ++idx) {
    G._args.emplace_back(argv[idx]);
  }
  std::string err_str;
  if (!G._preload_errata.is_ok()) {
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
  }
  auto errata{TxnBoxInit()};
  if (!errata.is_ok()) {
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(&CB_TxnBoxMsg, nullptr));
  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(&CB_TxnBoxShutdown, nullptr));
#if TS_VERSION_MAJOR >= 9
  TSPluginDSOReloadEnable(false);
#endif
};
/* ------------------------------------------------------------------------ */
