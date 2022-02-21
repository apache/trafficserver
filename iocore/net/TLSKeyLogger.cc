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

#include "P_TLSKeyLogger.h"
#include "tscore/Diags.h"

#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

// The caller of this function is responsible to acquire a unique_lock for
// _mutex.
void
TLSKeyLogger::close_keylog_file()
{
  if (_fd == -1) {
    return;
  }
  if (close(_fd) == -1) {
    Error("Could not close keylog file: %s", strerror(errno));
  }
  _fd = -1;
}

void
TLSKeyLogger::enable_keylogging_internal(const char *keylog_file)
{
#if TS_HAS_TLS_KEYLOGGING
  Debug("ssl_keylog", "Enabling TLS key logging to: %s.", keylog_file);
  std::unique_lock lock{_mutex};
  if (keylog_file == nullptr) {
    close_keylog_file();
    Debug("ssl_keylog", "Received a nullptr for keylog_file: disabling keylogging.");
    return;
  }

  _fd = open(keylog_file, O_WRONLY | O_APPEND | O_CREAT, S_IWUSR | S_IRUSR);
  if (_fd == -1) {
    Error("Could not open keylog file %s: %s", keylog_file, strerror(errno));
    return;
  }
  Note("Opened %s for TLS key logging.", keylog_file);
#else
  Error("TLS keylogging is configured, but Traffic Server is not compiled with a version of OpenSSL that supports it.");
  return;
#endif /* TS_HAS_TLS_KEYLOGGING */
}

void
TLSKeyLogger::disable_keylogging_internal()
{
  std::unique_lock lock{_mutex};
  if (is_enabled()) {
    Note("Disabling TLS key logging.");
  }
  close_keylog_file();
  Debug("ssl_keylog", "TLS keylogging is disabled.");
}

void
TLSKeyLogger::log(const char *line)
{
  std::shared_lock lock{_mutex};
  if (!is_enabled()) {
    return;
  }

  // writev() is guaranteed to be thread safe.
  struct iovec vector[2];
  vector[0].iov_base = const_cast<void *>(reinterpret_cast<const void *>(line));
  vector[0].iov_len  = strlen(line);
  vector[1].iov_base = const_cast<void *>(reinterpret_cast<const void *>("\n"));
  vector[1].iov_len  = 1;
  if (writev(_fd, vector, 2) <= 0) {
    Error("Could not write TLS session key to key log file: %s", strerror(errno));
  }
}
