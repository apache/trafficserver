/** @file

  SSL Preaccept test plugin
  Implements blind tunneling based on the client IP address
  The client ip addresses are specified in the plugin's
  config file as an array of IP addresses or IP address ranges under the
  key "client-blind-tunnel"

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

#include <ts/ts.h>
#include <openssl/ssl.h>
#include <strings.h>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <unordered_map>
#include <vector>

#define PN "ssl_secret_load_test"
#define PCP "[" PN " Plugin] "

// Map of secret name to last modified time
std::unordered_map<std::string, time_t> secret_versions;

void
update_file_name(const std::string_view &path, std::string &newname)
{
  // insert the "ssl" directory into the path
  auto offset = path.find_last_of("/");
  if (offset == std::string::npos) {
    newname = "ssl/";
    newname.append(path);
  } else {
    newname = path.substr(0, offset + 1);
    newname.append("ssl/");
    newname.append(path.substr(offset + 1));
  }
}

bool
load_file(const std::string &newname, struct stat *statdata, std::string &data_item)
{
  if (stat(newname.c_str(), statdata) < 0) {
    return false;
  }

  int fd = open(newname.c_str(), O_RDONLY);
  if (fd < 0) {
    TSDebug(PN, "Failed to load %s", newname.c_str());
    return false;
  }
  size_t total_size = statdata->st_size;
  data_item.resize(total_size);
  size_t offset = 0;
  char *data    = data_item.data();
  while (offset < total_size) {
    int num_read = read(fd, data + offset, total_size - offset);
    if (num_read < 0) {
      close(fd);
      return false;
    }
    offset += num_read;
  }
  close(fd);
  return true;
}

int
CB_Load_Secret(TSCont cont, TSEvent event, void *edata)
{
  TSSecretID *id = reinterpret_cast<TSSecretID *>(edata);

  TSDebug(PN, "Load secret for %*.s", static_cast<int>(id->cert_name_len), id->cert_name);

  std::string newname;
  std::string data_item;
  struct stat statdata;

  update_file_name(std::string_view{id->cert_name, id->cert_name_len}, newname);

  TSDebug(PN, "Really load secret for %s", newname.c_str());

  // Load the secret and add it to the map
  if (!load_file(newname, &statdata, data_item)) {
    return TS_ERROR;
  }
  secret_versions.insert(std::make_pair(std::string{id->cert_name, id->cert_name_len}, statdata.st_mtime));

  TSSslSecretSet(id->cert_name, id->cert_name_len, data_item.data(), data_item.size());

  if (id->key_name_len > 0) {
    TSDebug(PN, "Load secret for %*.s", static_cast<int>(id->key_name_len), id->key_name);
    update_file_name(std::string_view{id->key_name, id->key_name_len}, newname);

    TSDebug(PN, "Really load secret for %s", newname.c_str());

    // Load the secret and add it to the map
    if (!load_file(newname, &statdata, data_item)) {
      return TS_ERROR;
    }
    secret_versions.insert(std::make_pair(std::string{id->key_name, id->key_name_len}, statdata.st_mtime));

    TSSslSecretSet(id->key_name, id->key_name_len, data_item.data(), data_item.size());
  }

  return TS_SUCCESS;
}

int
CB_Update_Secret(TSCont cont, TSEvent event, void *edata)
{
  std::vector<std::string> updates;
  for (auto iter = secret_versions.begin(); iter != secret_versions.end(); ++iter) {
    std::string newname;
    std::string data_item;
    struct stat statdata;

    update_file_name(iter->first, newname);
    TSDebug(PN, "check secret for %s, really %s", iter->first.c_str(), newname.c_str());

    if (stat(newname.c_str(), &statdata) < 0) {
      continue;
    }

    if (statdata.st_mtime > iter->second) {
      TSDebug(PN, "check secret %s has been updated", newname.c_str());
      if (!load_file(newname, &statdata, data_item)) {
        continue;
      }
      TSSslSecretSet(iter->first.c_str(), iter->first.length(), data_item.data(), data_item.size());
      updates.push_back(iter->first);
      iter->second = statdata.st_mtime;
    }
  }
  for (auto name : updates) {
    TSDebug(PN, "update cert for secret %s", name.c_str());
    TSSslSecretUpdate(name.c_str(), name.length());
  }
  TSContScheduleOnPool(cont, 3000, TS_THREAD_POOL_TASK);
  return TS_SUCCESS;
}

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>("SSL secret load test");
  info.vendor_name   = const_cast<char *>("apache");
  info.support_email = const_cast<char *>("shinrich@apache.org");
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PN);
  }

  TSCont cb = TSContCreate(&CB_Load_Secret, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_SSL_SECRET_HOOK, cb);

  // Scheduled a call back to trigger every 3 seconds to look for changes to the files
  TSCont cb_update = TSContCreate(&CB_Update_Secret, TSMutexCreate());
  TSContScheduleOnPool(cb_update, 3000, TS_THREAD_POOL_TASK);

  return;
}
