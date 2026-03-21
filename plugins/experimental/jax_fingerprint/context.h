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

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <limits.h>

#include <string>

class JAxContext
{
public:
  JAxContext(const char *name, sockaddr const *s_sockaddr);
  ~JAxContext();

  const std::string &get_fingerprint() const;
  void               set_fingerprint(const std::string &fingerprint);

  const char *get_addr() const;
  const char *get_method_name() const;

private:
  std::string _fingerprint;
  char        _addr[PATH_MAX + 1];
  const char *_method_name;
};
