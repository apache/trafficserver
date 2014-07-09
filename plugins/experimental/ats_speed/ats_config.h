// Copyright 2013 We-Amp B.V.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: oschaaf@we-amp.com (Otto van der Schaaf)
#ifndef ATS_CONFIG_H_
#define ATS_CONFIG_H_

#include <string>
#include <vector>

#include <ts/ts.h>

#include "ats_thread_system.h"

#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_util.h"


namespace net_instaweb {

class AtsRewriteOptions;

class AtsHostConfig {
public:
  explicit AtsHostConfig(const GoogleString & host, AtsRewriteOptions* options)
      : host_(host)
      , options_(options)
  {
  }
  virtual ~AtsHostConfig();

  inline GoogleString host() { return host_; }
  inline AtsRewriteOptions* options() { return options_; }
  inline bool override_expiry() { return override_expiry_; }
  inline void set_override_expiry(bool x) { override_expiry_ = x; }
private:
  GoogleString host_;
  AtsRewriteOptions* options_;
  bool override_expiry_;
  DISALLOW_COPY_AND_ASSIGN(AtsHostConfig);
}; // class AtsHostConfig

class AtsConfig {
  friend class AtsHostConfig;
public:
  explicit AtsConfig(AtsThreadSystem* thread_system);
  virtual ~AtsConfig();
  
  // TODO(oschaaf): destructor??
  bool Parse(const char * path);
  AtsHostConfig * Find(const char * host, int host_length);
  inline AtsHostConfig * GlobalConfiguration() {
    return host_configurations_[0];
  }
  AtsThreadSystem* thread_system() {
    return thread_system_;
  }

private:
  void AddHostConfig(AtsHostConfig* hc);

  std::vector<AtsHostConfig *> host_configurations_;
  AtsThreadSystem* thread_system_;
  //todo: destructor. delete owned host configurations
  DISALLOW_COPY_AND_ASSIGN(AtsConfig);
}; // class Configuration


}  // namespace net_instaweb

#endif  // ATS_CONFIG_H
