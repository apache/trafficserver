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

#include "sni_limiter.h"
#include "sni_selector.h"

std::atomic<SniSelector *> SniSelector::_instance = nullptr;

///////////////////////////////////////////////////////////////////////////////
// YAML parser for the global YAML configuration (via plugin.config)
//
bool
SniSelector::yamlParser(std::string yaml_file)
{
  YAML::Node config;

  try {
    config = YAML::LoadFile(yaml_file);
  } catch (YAML::BadFile &e) {
    TSError("[%s] Cannot load configuration file: %s.", PLUGIN_NAME, e.what());
    return false;
  } catch (std::exception &e) {
    TSError("[%s] Unknown error while loading configuration file: %s.", PLUGIN_NAME, e.what());
    return false;
  }

  _yaml_file       = yaml_file;
  _yaml_last_write = std::filesystem::last_write_time(_yaml_file);

  const YAML::Node &ipreps = config["ip-rep"];

  if (ipreps && ipreps.IsSequence()) {
    for (size_t i = 0; i < ipreps.size(); ++i) {
      const YAML::Node &ipr = ipreps[i];

      if (ipr.IsMap() && ipr["name"]) {
        std::string name = ipr["name"].as<std::string>();

        if (nullptr != findIpRep(name)) {
          TSError("[%s] Duplicate IP-Reputation names being added (%s)", PLUGIN_NAME, name.c_str());
          return false;
        }

        auto iprep = new IpReputation::SieveLru(name);

        if (iprep->parseYaml(ipr)) {
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

  // ToDo: Add the IP list YAML parsing

  // Parse all the SNI selectors
  const YAML::Node &sel = config["selector"];

  if (sel && sel.IsSequence()) {
    for (size_t i = 0; i < sel.size(); ++i) {
      const YAML::Node &sni = sel[i];

      if (sni.IsMap()) {
        // ToDo: Allow a sequence of names here
        std::string name = sni["sni"].as<std::string>();

        if (nullptr != findSNI(name)) {
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

          if (aliases && aliases.IsSequence()) {
            for (size_t j = 0; j < aliases.size(); ++j) {
              std::string alias = aliases[j].as<std::string>();

              if (nullptr != findSNI(alias)) {
                TSError("[%s] Duplicate SNIs being added (%s)", PLUGIN_NAME, alias.c_str());
                return false;
              }
              Dbg(dbg_ctl, "Adding alias: %s -> %s", alias.c_str(), name.c_str());
              addAlias(alias, limiter);
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
  auto current  = std::filesystem::last_write_time(selector->yamlFile());
  auto old_sel  = static_cast<SniSelector *>(TSContDataGet(cont));

  // Delete the previous selector
  if (old_sel) {
    old_sel->release();
    TSContDataSet(cont, nullptr);
  }

  if (current > selector->yamlLastWrite()) {
    auto new_sel = new SniSelector();

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
  } else {
    Dbg(dbg_ctl, "No change in YAML file: %s", selector->yamlFile().c_str());
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
  SniSelector *selector = static_cast<SniSelector *>(TSContDataGet(cont));

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

IpReputation::SieveLru *
SniSelector::findIpRep(const std::string &name)
{
  auto it = std::find_if(_reputations.begin(), _reputations.end(),
                         [&name](const IpReputation::SieveLru *iprep) { return iprep->name() == name; });

  if (it != _reputations.end()) {
    return *it;
  }

  return nullptr;
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
// Startup of the SNI selector hooks and config reload continuation and instance
//
void
SniSelector::startup()
{
  TSCont sni_cont = TSContCreate(sni_limit_cont, nullptr);

  TSReleaseAssert(sni_cont);

  _instance.store(new SniSelector());
  TSHttpHookAdd(TS_SSL_CLIENT_HELLO_HOOK, sni_cont);
  TSHttpHookAdd(TS_VCONN_CLOSE_HOOK, sni_cont);

  auto config_cont = TSContCreate(sni_config_cont, TSMutexCreate());

  TSReleaseAssert(config_cont);
  TSContScheduleEveryOnPool(config_cont, std::chrono::milliseconds{10000}.count(), TS_THREAD_POOL_TASK);
}
