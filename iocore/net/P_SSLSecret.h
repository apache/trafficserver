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
#include <string_view>
#include <mutex>
#include <unordered_map>

class SSLSecret
{
public:
  SSLSecret() {}
  bool getSecret(const std::string &name, std::string_view &data) const;
  bool setSecret(const std::string &name, const char *data, int data_len);
  bool getOrLoadSecret(const std::string &name, const std::string &name2, std::string_view &data, std::string_view &data2);

private:
  const std::string *getSecretItem(const std::string &name) const;
  bool loadSecret(const std::string &name, const std::string &name2, std::string &data_item, std::string &data_item2);
  bool loadFile(const std::string &name, std::string &data_item);

  std::unordered_map<std::string, std::string> secret_map;
  mutable std::recursive_mutex secret_map_mutex;
};
