/** @file

  Transforms content using gzip or deflate

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

#ifndef GZIP_CONFIGURATION_H_
#define GZIP_CONFIGURATION_H_

#include <string>
#include <vector>
#include "debug_macros.h"

namespace Gzip
{
class HostConfiguration
{
public: // todo -> only configuration should be able to construct hostconfig
  explicit HostConfiguration(const std::string &host) : host_(host), enabled_(true), cache_(true), remove_accept_encoding_(false) {}
  inline bool
  enabled()
  {
    return enabled_;
  }
  inline void
  set_enabled(bool x)
  {
    enabled_ = x;
  }
  inline bool
  cache()
  {
    return cache_;
  }
  inline void
  set_cache(bool x)
  {
    cache_ = x;
  }
  inline bool
  remove_accept_encoding()
  {
    return remove_accept_encoding_;
  }
  inline void
  set_remove_accept_encoding(bool x)
  {
    remove_accept_encoding_ = x;
  }
  inline std::string
  host()
  {
    return host_;
  }
  void add_disallow(const std::string &disallow);
  void add_compressible_content_type(const std::string &content_type);
  bool IsUrlAllowed(const char *url, int url_len);
  bool ContentTypeIsCompressible(const char *content_type, int content_type_length);

private:
  std::string host_;
  bool enabled_;
  bool cache_;
  bool remove_accept_encoding_;
  std::vector<std::string> compressible_content_types_;
  std::vector<std::string> disallows_;
  DISALLOW_COPY_AND_ASSIGN(HostConfiguration);
}; // class HostConfiguration

class Configuration
{
  friend class HostConfiguration;

public:
  static Configuration *Parse(const char *path);
  HostConfiguration *Find(const char *host, int host_length);
  inline HostConfiguration *
  GlobalConfiguration()
  {
    return host_configurations_[0];
  }

private:
  explicit Configuration() {}
  void AddHostConfiguration(HostConfiguration *hc);

  std::vector<HostConfiguration *> host_configurations_;
  // todo: destructor. delete owned host configurations
  DISALLOW_COPY_AND_ASSIGN(Configuration);
}; // class Configuration

} // namespace

#endif
