/** @file

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
#include <string>
#include <map>
#include "InkAPIInternal.h" // Added to include the ssl_hook and lifestyle_hook definitions
#include "tscore/ts_file.h"
#include "P_SSLConfig.h"

bool
SSLSecret::loadSecret(const std::string &name1, const std::string &name2, std::string &data_item1, std::string &data_item2)
{
  // Call the load secret hooks
  //
  class APIHook *curHook = lifecycle_hooks->get(TS_LIFECYCLE_SSL_SECRET_HOOK);
  TSSecretID secret_name;
  secret_name.cert_name     = name1.data();
  secret_name.cert_name_len = name1.size();
  secret_name.key_name      = name2.data();
  secret_name.key_name_len  = name2.size();
  while (curHook) {
    curHook->invoke(TS_EVENT_SSL_SECRET, &secret_name);
    curHook = curHook->next();
  }

  const std::string *data1 = this->getSecretItem(name1);
  const std::string *data2 = this->getSecretItem(name2);
  if ((nullptr == data1 || data1->length() == 0) || (!name2.empty() && (nullptr == data2 || data2->length() == 0))) {
    // If none of them loaded it, assume it is a file
    return loadFile(name1, data_item1) && (name2.empty() || loadFile(name2, data_item2));
  }
  return true;
}

bool
SSLSecret::loadFile(const std::string &name, std::string &data_item)
{
  struct stat statdata;
  // Load the secret and add it to the map
  if (stat(name.c_str(), &statdata) < 0) {
    Debug("ssl_secret", "File: %s received error: %s", name.c_str(), strerror(errno));
    return false;
  }
  std::error_code error;
  data_item = ts::file::load(ts::file::path(name), error);
  if (error) {
    // Loading file failed
    Debug("ssl_secret", "Loading file: %s failed ", name.c_str());
    return false;
  }
  if (SSLConfigParams::load_ssl_file_cb) {
    SSLConfigParams::load_ssl_file_cb(name.c_str());
  }
  return true;
}

bool
SSLSecret::setSecret(const std::string &name, const char *data, int data_len)
{
  std::scoped_lock lock(secret_map_mutex);
  auto iter = secret_map.find(name);
  if (iter == secret_map.end()) {
    secret_map[name] = "";
    iter             = secret_map.find(name);
  }
  if (iter == secret_map.end()) {
    return false;
  }
  iter->second.assign(data, data_len);
  // The full secret data can be sensitive. Print only the first 50 bytes.
  Debug("ssl_secret", "Set secret for %s to %.50s", name.c_str(), iter->second.c_str());
  return true;
}

const std::string *
SSLSecret::getSecretItem(const std::string &name) const
{
  std::scoped_lock lock(secret_map_mutex);
  auto iter = secret_map.find(name);
  if (iter == secret_map.end()) {
    return nullptr;
  }
  return &iter->second;
}

bool
SSLSecret::getSecret(const std::string &name, std::string_view &data) const
{
  const std::string *data_item = this->getSecretItem(name);
  if (data_item) {
    // The full secret data can be sensitive. Print only the first 50 bytes.
    Debug("ssl_secret", "Get secret for %s: %.50s", name.c_str(), data_item->c_str());
    data = *data_item;
  } else {
    Debug("ssl_secret", "Get secret for %s: not found", name.c_str());
    data = std::string_view{};
  }
  return data_item != nullptr;
}

bool
SSLSecret::getOrLoadSecret(const std::string &name1, const std::string &name2, std::string_view &data1, std::string_view &data2)
{
  Debug("ssl_secret", "lookup up secrets for %s and %s", name1.c_str(), name2.empty() ? "[empty]" : name2.c_str());
  std::scoped_lock lock(secret_map_mutex);
  bool found_secret1 = this->getSecret(name1, data1);
  bool found_secret2 = name2.empty() || this->getSecret(name2, data2);

  // If we can't find either secret, load them both again
  if (!found_secret1 || !found_secret2) {
    // Make sure each name has an entry
    if (!found_secret1) {
      secret_map[name1] = "";
    }
    if (!found_secret2) {
      secret_map[name2] = "";
    }
    auto iter1 = secret_map.find(name1);
    auto iter2 = name2.empty() ? iter1 : secret_map.find(name2);
    if (this->loadSecret(name1, name2, iter1->second, iter2->second)) {
      data1 = iter1->second;
      if (!name2.empty()) {
        data2 = iter2->second;
      }
      return true;
    }
  } else {
    return true;
  }
  return false;
}
