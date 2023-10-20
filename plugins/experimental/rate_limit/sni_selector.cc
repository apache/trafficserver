/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "tscore/ink_config.h"
#include <yaml-cpp/yaml.h>

#include "sni_selector.h"

std::atomic<SniSelector *> SniSelector::_instance = nullptr;

///////////////////////////////////////////////////////////////////////////////
// YAML parser for the global YAML configuration (via plugin.config)
//
bool
SniSelector::yamlParser(const std::string &yaml_file)
{
  YAML::Node config;

  try {
    config = YAML::LoadFile(yaml_file);
  } catch (YAML::BadFile const &e) {
    TSError("[%s] Cannot load configuration file: %s.", PLUGIN_NAME, e.what());
    return false;
  } catch (std::exception const &e) {
    TSError("[%s] Unknown error while loading configuration file: %s.", PLUGIN_NAME, e.what());
    return false;
  }

  _yaml_file = yaml_file;

  // First build the Lists, if any
  const YAML::Node &lists = config["lists"];

  if (lists && lists.IsSequence()) {
    for (const auto &i : lists) {
      const YAML::Node &list = i;

      if (list.IsMap() && list["name"]) {
        auto name = list["name"].as<std::string>();

        if (nullptr != findList(name)) {
          TSError("[%s] Duplicate List names being added (%s)", PLUGIN_NAME, name.c_str());
          return false;
        }

        auto ipl = new List::IP(name);

        if (ipl->parseYaml(list)) {
          Dbg(dbg_ctl, "Loaded List rule: %s", name.c_str());
          addList(ipl);
        } else {
          TSError("[%s] Failed to parse the List YAML node", PLUGIN_NAME);
          delete ipl;
          return false;
        }
      } else {
        TSError("[%s] List node is not a map or without a name", PLUGIN_NAME);
        return false;
      }
    }
  }

  // Next, build the IP reputation (if any)
  const YAML::Node &ipreps = config["ip-rep"];

  if (ipreps && ipreps.IsSequence()) {
    for (const auto &i : ipreps) {
      const YAML::Node &ipr = i;

      if (ipr.IsMap() && ipr["name"]) {
        auto name = ipr["name"].as<std::string>();

        if (nullptr != findIpRep(name)) {
          TSError("[%s] Duplicate IP-Reputation names being added (%s)", PLUGIN_NAME, name.c_str());
          return false;
        }

        auto iprep = new IpReputation::SieveLru(name);

        if (iprep->parseYaml(ipr)) {
          Dbg(dbg_ctl, "Loaded IP Reputation rule: %s", name.c_str());
          addIPReputation(iprep);
        } else {
          TSError("[%s] Failed to parse the ip-rep YAML node", PLUGIN_NAME);
          delete iprep;
          return false;
        }
      } else {
        TSError("[%s] ip-rep node is not a map or without a name", PLUGIN_NAME);
        return false;
      }
    }
  }

  // Finally, parse all the SNI selectors (if any)
  const YAML::Node &sel = config["selector"];

  if (sel && sel.IsSequence()) {
    for (const auto &i : sel) {
      const YAML::Node &sni = i;

      if (sni.IsMap() && !sni["sni"].IsSequence()) {
        auto name = sni["sni"].as<std::string>();

        if (nullptr != findLimiter(name)) {
          TSError("[%s] Duplicate SNIs being added (%s)", PLUGIN_NAME, name.c_str());
          return false;
        }

        auto limiter = new SniRateLimiter(name, this);

        if (limiter->parseYaml(sni)) {
          if (name == "*" || name == "default") {
            _default = limiter;
          } else {
            addLimiter(limiter);
          }

          // Add aliases, if any
          const YAML::Node &aliases = sni["aliases"];

          if (aliases) {
            if (aliases.IsSequence()) {
              for (const auto &aliase : aliases) {
                auto alias = aliase.as<std::string>();

                if (nullptr != findLimiter(alias)) {
                  TSError("[%s] Duplicate SNIs being added (%s)", PLUGIN_NAME, alias.c_str());
                  return false;
                }
                Dbg(dbg_ctl, "Adding alias: %s -> %s", alias.c_str(), name.c_str());
                addAlias(alias, limiter);
              }
            } else {
              TSError("[%s] aliases node is not a sequence", PLUGIN_NAME);
              return false;
            }
          }
        } else {
          TSError("[%s] Failed to parse the selector YAML node", PLUGIN_NAME);
          delete limiter;
          return false;
        }
      } else {
        TSError("[%s] selector node is not a map or without a name", PLUGIN_NAME);
        return false;
      }
    }
  }

  Dbg(dbg_ctl, "Succesfully loaded YAML file: %s", yaml_file.c_str());

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// This is the queue management continuation, which gets called periodically
//
static int
sni_config_cont(TSCont cont, TSEvent event, void *edata)
{
  auto selector = SniSelector::instance(); // Also leases the instance
  auto old_sel  = static_cast<SniSelector *>(TSContDataGet(cont));
  auto new_sel  = new SniSelector();

  // Delete the previous selector, which releases the lease we got at setup / reload
  if (old_sel) {
    old_sel->release();
    TSContDataSet(cont, nullptr);
  }

  if (new_sel->yamlParser(selector->yamlFile())) {
    new_sel->acquire();
    new_sel->setupQueueCont(); // Start the queue processing continuation if needed
    SniSelector::swap(new_sel);
    // Now, save the old selector in the cont data here, such that we do the final release next time
    TSContDataSet(cont, selector);
    Dbg(dbg_ctl, "Reloading YAML file: %s", new_sel->yamlFile().c_str());
  } else {
    delete new_sel;
    TSError("[%s] Failed to reload YAML file: %s", PLUGIN_NAME, selector->yamlFile().c_str());
  }

  selector->release();

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// This is the queue management continuation, which gets called periodically
//
static int
sni_queue_cont(TSCont cont, TSEvent event, void *edata)
{
  auto *selector = static_cast<SniSelector *>(TSContDataGet(cont));

  for (const auto &[key, entry] : selector->limiters()) {
    auto [owner, limiter] = entry;
    QueueTime now         = std::chrono::system_clock::now(); // Only do this once per limiter

    if (owner) { // Don't operate on the aliases
      // Try to enable some queued VCs (if any) if there are slots available
      while (limiter->size() > 0 && limiter->reserve()) {
        auto [vc, contp, start_time]    = limiter->pop();
        std::chrono::milliseconds delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

        (void)contp; // Ugly, but silences some compilers.
        Dbg(dbg_ctl, "SNI=%s: Enabling queued VC after %ldms", key.data(), static_cast<long>(delay.count()));
        TSVConnReenable(vc);
        limiter->incrementMetric(RATE_LIMITER_METRIC_RESUMED);
      }

      // Kill any queued VCs if they are too old
      if (limiter->size() > 0 && limiter->max_age() > std::chrono::milliseconds::zero()) {
        now = std::chrono::system_clock::now(); // Update the "now", for some extra accuracy

        while (limiter->size() > 0 && limiter->hasOldEntity(now)) {
          // The oldest object on the queue is too old on the queue, so "kill" it.
          auto [vc, contp, start_time]  = limiter->pop();
          std::chrono::milliseconds age = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);

          (void)contp;
          Dbg(dbg_ctl, "Queued VC is too old (%ldms), erroring out", static_cast<long>(age.count()));
          TSVConnReenableEx(vc, TS_EVENT_ERROR);
          limiter->incrementMetric(RATE_LIMITER_METRIC_EXPIRED);
        }
      }
    }
  }

  return TS_EVENT_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// If needed, create the queue continuation that needs to run for this selector.
//
void
SniSelector::setupQueueCont()
{
  if (_needs_queue_cont && !_queue_cont) {
    _queue_cont = TSContCreate(sni_queue_cont, TSMutexCreate());
    TSReleaseAssert(_queue_cont);
    TSContDataSet(_queue_cont, this);
    _queue_action = TSContScheduleEveryOnPool(_queue_cont, QUEUE_DELAY_TIME.count(), TS_THREAD_POOL_TASK);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Startup of the SNI selector hooks and config reload continuation and
// instance. This should only be called once, after which the configuration
// continuation takes over any reloads.
//
void
SniSelector::startup(const std::string &yaml_file)
{
  auto sni_cont    = TSContCreate(sni_limit_cont, nullptr);
  auto config_cont = TSContCreate(sni_config_cont, TSMutexCreate());

  TSReleaseAssert(sni_cont);
  TSReleaseAssert(config_cont);

  _instance.store(new SniSelector());
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, sni_cont);
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, sni_cont);

  auto selector = SniSelector::instance(); // Assure that we don't delete this until next config reload

  if (selector->yamlParser(yaml_file)) {
    selector->setupQueueCont(); // Start the queue processing continuation if needed
    TSMgmtUpdateRegister(config_cont, PLUGIN_NAME, yaml_file.c_str());
  } else {
    selector->release();
    TSFatal("[%s] Failed to parse YAML file '%s'", PLUGIN_NAME, yaml_file.c_str());
  }
}
