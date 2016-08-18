
/** @file

    A brief file description

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
*/

#include "ats_config.h"

#include <ts/ts.h>
#include <fstream>

#include "net/instaweb/util/public/string_util.h"

#include "ats_message_handler.h"
#include "ats_rewrite_options.h"

namespace net_instaweb
{
using namespace std;

void
ltrim_if(string &s, int (*fp)(int))
{
  for (size_t i = 0; i < s.size();) {
    if (fp(s[i])) {
      s.erase(i, 1);
    } else {
      break;
    }
  }
}

void
rtrim_if(string &s, int (*fp)(int))
{
  for (ssize_t i = (ssize_t)s.size() - 1; i >= 0; i--) {
    if (fp(s[i])) {
      s.erase(i, 1);
    } else {
      break;
    }
  }
}

void
trim_if(string &s, int (*fp)(int))
{
  ltrim_if(s, fp);
  rtrim_if(s, fp);
}

vector<string>
tokenize(const string &s, int (*fp)(int))
{
  vector<string> r;
  string tmp;

  for (size_t i = 0; i < s.size(); i++) {
    if (fp(s[i])) {
      if (tmp.size()) {
        r.push_back(tmp);
        tmp = "";
      }
    } else {
      tmp += s[i];
    }
  }

  if (tmp.size()) {
    r.push_back(tmp);
  }

  return r;
}

AtsConfig::AtsConfig(AtsThreadSystem *thread_system) : thread_system_(thread_system)
{
  AddHostConfig(new AtsHostConfig(GoogleString("(XXXXXX)"), new AtsRewriteOptions(thread_system_)));
}

AtsConfig::~AtsConfig()
{
  for (size_t i = 0; i < host_configurations_.size(); i++) {
    delete host_configurations_[i];
    host_configurations_.clear();
  }
}

void
AtsConfig::AddHostConfig(AtsHostConfig *hc)
{
  host_configurations_.push_back(hc);
}

AtsHostConfig::~AtsHostConfig()
{
  if (options_ != NULL) {
    delete options_;
    options_ = NULL;
  }
}

AtsHostConfig *
AtsConfig::Find(const char *host, int host_length)
{
  AtsHostConfig *host_configuration = host_configurations_[0];

  std::string shost(host, host_length);

  for (size_t i = 1; i < host_configurations_.size(); i++) {
    if (host_configurations_[i]->host() == shost) {
      host_configuration = host_configurations_[i];
      break;
    }
  }

  return host_configuration;
}

bool
AtsConfig::Parse(const char *path)
{
  string pathstring(path);

  // If we have a path and it's not an absolute path, make it relative to the
  // configuration directory.
  if (!pathstring.empty() && pathstring[0] != '/') {
    pathstring.assign(TSConfigDirGet());
    pathstring.append("/");
    pathstring.append(path);
  }

  trim_if(pathstring, isspace);

  AtsHostConfig *current_host_configuration = host_configurations_[0];

  if (pathstring.empty()) {
    TSError("[ats_config] Empty path passed in AtsConfig::Parse");
    return false;
  }

  path = pathstring.c_str();
  std::ifstream f;

  size_t lineno = 0;

  f.open(path, std::ios::in);

  if (!f.is_open()) {
    TSError("[ats_config] Could not open file [%s], skip", path);
    return false;
  }

  while (!f.eof()) {
    std::string line;
    getline(f, line);
    ++lineno;

    trim_if(line, isspace);
    if (line.size() == 0) {
      continue;
    }
    if (line[0] == '#') {
      continue;
    }

    vector<string> v = tokenize(line, isspace);
    if (v.size() == 0)
      continue;
    GoogleString msg;
    AtsMessageHandler handler(thread_system_->NewMutex());
    if (v.size() == 1) {
      string token = v[0];
      if ((token[0] == '[') && (token[token.size() - 1] == ']')) {
        GoogleString current_host  = token.substr(1, token.size() - 2);
        current_host_configuration = new AtsHostConfig(current_host, new AtsRewriteOptions(thread_system_));
        AddHostConfig(current_host_configuration);
      } else if (StringCaseEqual(token, "override_expiry")) {
        current_host_configuration->set_override_expiry(true);
      } else {
        msg = "unknown single token on a line";
      }
    } else {
      global_settings settings;
      v.erase(v.begin());
      const char *err = current_host_configuration->options()->ParseAndSetOptions(v, &handler, settings);
      if (err) {
        msg.append(err);
      }
    }
    if (msg.size() > 0) {
      TSDebug("ats-speed", "Error parsing line [%s]: [%s]", line.c_str(), msg.c_str());
    }
  }

  return true;
}

} //  namespace net_instaweb
