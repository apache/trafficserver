/** @file

  HostFile class for processing a hosts file

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

#include "swoc/swoc_file.h"

#include "HostDBProcessor.h"
#include "swoc/TextView.h"

#include <memory>
#include <unordered_map>

struct HostFileRecord {
  HostDBRecord::Handle record_4;
  HostDBRecord::Handle record_6;
};

struct HostFile {
  using HostFileForwardMap = std::unordered_map<swoc::TextView, HostFileRecord, std::hash<std::string_view>>;
  using HostFileReverseMap = std::unordered_map<IpAddr, HostDBRecord::Handle, IpAddr::Hasher>;

  HostFile(ts_seconds ttl) : ttl(ttl) {}

  HostDBRecord::Handle lookup(const HostDBHash &hash);

  ts_seconds ttl;
  HostFileForwardMap forward;
  HostFileReverseMap reverse;
};

std::shared_ptr<HostFile> ParseHostFile(swoc::file::path const &path, ts_seconds interval);
