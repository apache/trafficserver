/** @file

  ssl_init.cc - set up things

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
#include <string.h>
#include "ssl_utils.h"
#include "Config.h"
#include "common.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

ssl_session_param ssl_param; // <- global containing all operational info
std::string conf_file;

int
init_subscriber()
{
  ssl_param.sub = new RedisSubscriber(conf_file);
  if ((!ssl_param.sub) || (!ssl_param.sub->is_good())) {
    TSError("Construct RedisSubscriber error.");
    return -1;
  }
  return 0;
}

int
init_ssl_params(const std::string &conf)
{
  conf_file = conf;
  if (Config::getSingleton().loadConfig(conf)) {
    Config::getSingleton().getValue("ssl_session", "ClusterName", ssl_param.cluster_name);
    Config::getSingleton().getValue("ssl_session", "KeyUpdateInterval", ssl_param.key_update_interval);
    Config::getSingleton().getValue("ssl_session", "STEKMaster", ssl_param.stek_master);
    Config::getSingleton().getValue("ssl_session", "redis_auth_key_file", ssl_param.redis_auth_key_file);
  } else {
    return -1;
  }

  if (ssl_param.key_update_interval > STEK_MAX_LIFETIME) {
    ssl_param.key_update_interval = STEK_MAX_LIFETIME;
    TSDebug(PLUGIN, "KeyUpdateInterval too high, resetting session ticket key rotation to %d seconds",
            (int)ssl_param.key_update_interval);
  }

  TSDebug(PLUGIN, "init_ssl_params: I %s been configured to initially be stek_master",
          ((ssl_param.stek_master) ? "HAVE" : "HAVE NOT"));
  TSDebug(PLUGIN, "init_ssl_params: Rotation interval (ssl_param.key_update_interval)set to %d\n", ssl_param.key_update_interval);
  TSDebug(PLUGIN, "init_ssl_params: cluster_name set to %s", ssl_param.cluster_name.c_str());

  int ret = STEK_init_keys();
  if (ret < 0) {
    TSError("init keys failure. %s", conf.c_str());
    return -1;
  }

  ssl_param.pub = new RedisPublisher(conf);
  if ((!ssl_param.pub) || (!ssl_param.pub->is_good())) {
    TSError("Construct RedisPublisher error.");
    return -1;
  }
  return 0;
}

ssl_session_param::ssl_session_param() : pub(nullptr) {}

ssl_session_param::~ssl_session_param()
{
  // Let the publish object leak for now, we are shutting down anyway
  /*if (pub)
      delete pub; */
}

/*
 Read the redis auth key from file ssl_param.redis_auth_key_file in retKeyBuff

 */
int
get_redis_auth_key(char *retKeyBuff, int buffSize)
{
  // Get the Key
  if (ssl_param.redis_auth_key_file.length()) {
    int fd = open(ssl_param.redis_auth_key_file.c_str(), O_RDONLY);
    struct stat info;
    if (0 == fstat(fd, &info)) {
      size_t n = info.st_size;
      std::string key_data;
      key_data.resize(n);
      auto read_len = read(fd, const_cast<char *>(key_data.data()), n);
      // Strip any trailing newlines
      while (read_len > 1 && key_data[read_len - 1] == '\n') {
        --read_len;
      }
      strncpy(retKeyBuff, key_data.c_str(), read_len);
    }
  } else {
    TSError("can not get redis auth key");
    return 0; /* error */
  }

  return 1; /* ok */
}
