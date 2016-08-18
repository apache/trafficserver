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
#include "ts/ink_atomic.h"

namespace Gzip
{
typedef std::vector<std::string> StringContainer;

class HostConfiguration
{
public:
  explicit HostConfiguration(const std::string &host)
    : host_(host), enabled_(true), cache_(true), remove_accept_encoding_(false), flush_(false), ref_count_(0)
  {
  }

  bool
  enabled()
  {
    return enabled_;
  }
  void
  set_enabled(bool x)
  {
    enabled_ = x;
  }
  bool
  cache()
  {
    return cache_;
  }
  void
  set_cache(bool x)
  {
    cache_ = x;
  }
  bool
  flush()
  {
    return flush_;
  }
  void
  set_flush(bool x)
  {
    flush_ = x;
  }
  bool
  remove_accept_encoding()
  {
    return remove_accept_encoding_;
  }
  void
  set_remove_accept_encoding(bool x)
  {
    remove_accept_encoding_ = x;
  }
  std::string
  host()
  {
    return host_;
  }
  bool
  has_disallows() const
  {
    return !disallows_.empty();
  }

  void add_disallow(const std::string &disallow);
  void add_compressible_content_type(const std::string &content_type);
  bool is_url_allowed(const char *url, int url_len);
  bool is_content_type_compressible(const char *content_type, int content_type_length);

  // Ref-counting these host configuration objects
  void
  hold()
  {
    ink_atomic_increment(&ref_count_, 1);
  }
  void
  release()
  {
    if (1 >= ink_atomic_decrement(&ref_count_, 1)) {
      debug("released and deleting HostConfiguration for %s settings", host_.size() > 0 ? host_.c_str() : "global");
      delete this;
    }
  }

private:
  std::string host_;
  bool enabled_;
  bool cache_;
  bool remove_accept_encoding_;
  bool flush_;
  volatile int ref_count_;

  StringContainer compressible_content_types_;
  StringContainer disallows_;

  DISALLOW_COPY_AND_ASSIGN(HostConfiguration);
};

typedef std::vector<HostConfiguration *> HostContainer;

class Configuration
{
  friend class HostConfiguration;

public:
  static Configuration *Parse(const char *path);
  HostConfiguration *find(const char *host, int host_length);
  void release_all();

private:
  explicit Configuration() {}
  void add_host_configuration(HostConfiguration *hc);

  HostContainer host_configurations_;

  DISALLOW_COPY_AND_ASSIGN(Configuration);
}; // class Configuration

} // namespace

#endif
