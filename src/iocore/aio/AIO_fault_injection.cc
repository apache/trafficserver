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

#include "AIO_fault_injection.h"
#include "Lock.h"
#include <mutex>

AIOFaultInjection aioFaultInjection;

void
AIOFaultInjection::_decrement_op_count(int fd)
{
  auto it = _state_by_fd.find(fd);
  if (it != _state_by_fd.end()) {
    it->second.op_count--;
  }
}

AIOFaultInjection::IOFault
AIOFaultInjection::_op_result(int fd)
{
  auto it = _faults_by_fd.find(fd);
  if (it != _faults_by_fd.end()) {
    std::size_t op_count = _state_by_fd[fd].op_count++;
    auto results         = it->second;
    if (results.find(op_count) != results.end()) {
      return results[op_count];
    }
  }

  return IOFault{0, false};
}

void
AIOFaultInjection::inject_fault(const char *path_regex, int op_index, IOFault fault)
{
  std::lock_guard<std::mutex> lock{_mutex};
  _faults_by_regex[path_regex][op_index] = fault;
}

int
AIOFaultInjection::open(const char *pathname, int flags, mode_t mode)
{
  std::lock_guard<std::mutex> lock{_mutex};
  std::filesystem::path abspath = std::filesystem::absolute(pathname);
  int fd                        = ::open(pathname, flags, mode);
  if (fd >= 0) {
    for (auto &[re_str, faults] : _faults_by_regex) {
      std::regex re{re_str};
      std::cmatch m;
      if (std::regex_match(abspath.c_str(), m, re)) {
        _faults_by_fd.insert_or_assign(fd, faults);
      }
    }
  }

  return fd;
}

ssize_t
AIOFaultInjection::pread(int fd, void *buf, size_t nbytes, off_t offset)
{
  std::lock_guard<std::mutex> lock{_mutex};
  IOFault result = _op_result(fd);
  ssize_t ret    = 0;
  if (result.skip_io) {
    ink_release_assert(result.err_no != 0);
  } else {
    ret = ::pread(fd, buf, nbytes, offset);
    if (ret < 0 && (errno == EINTR || errno == ENOBUFS || errno == ENOMEM)) {
      // caller will retry
      _decrement_op_count(fd);
    }
  }
  if (result.err_no != 0) {
    errno = result.err_no;
    ret   = -1;
  }
  return ret;
}

ssize_t
AIOFaultInjection::pwrite(int fd, const void *buf, size_t n, off_t offset)
{
  std::lock_guard<std::mutex> lock{_mutex};
  IOFault result = _op_result(fd);
  ssize_t ret    = 0;
  if (result.skip_io) {
    ink_release_assert(result.err_no != 0);
  } else {
    ret = ::pwrite(fd, buf, n, offset);
    if (ret < 0 && (errno == EINTR || errno == ENOBUFS || errno == ENOMEM)) {
      // caller will retry
      _decrement_op_count(fd);
    }
  }
  if (result.err_no != 0) {
    errno = result.err_no;
    ret   = -1;
  }
  return ret;
}
