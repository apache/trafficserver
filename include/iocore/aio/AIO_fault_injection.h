/** @file

  A mechanism to simulate disk failure by injecting faults in userspace.

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

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/Lock.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_mutex.h"
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <regex>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// We need a way to simulate failures determininstically to test disk
// initialization

static constexpr auto TAG{"fault"};

class AIOFaultInjection
{
  struct IOFault {
    int  err_no;
    bool skip_io;
  };
  using IOFaults = std::unordered_map<int, IOFault>;

  struct IOFaultState {
    std::size_t op_count = 0;
  };

  std::unordered_map<std::string, IOFaults> _faults_by_regex;
  std::unordered_map<int, IOFaults &>       _faults_by_fd;
  std::unordered_map<int, IOFaultState>     _state_by_fd;

  void    _decrement_op_count(int fd);
  IOFault _op_result(int fd);

  std::mutex _mutex;

public:
  void inject_fault(const char *path_regex, int op_index, IOFault fault);

  int     open(const char *pathname, int flags, mode_t mode);
  ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset);
  ssize_t pwrite(int fd, const void *buf, size_t n, off_t offset);
};

extern AIOFaultInjection aioFaultInjection;
