/** @file

  Definitions for the Lockfile class.

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

#include "tscore/ink_defs.h"
#include "tscore/ink_string.h"

#define COP_LOCK "cop.lock"
#define MANAGER_LOCK "manager.lock"
#define SERVER_LOCK "server.lock"

class Lockfile
{
public:
  Lockfile() { fname[0] = '\0'; }
  // coverity[uninit_member]
  Lockfile(const char *filename) { ink_strlcpy(fname, filename, sizeof(fname)); }
  ~Lockfile() {}
  void
  SetLockfileName(const char *filename)
  {
    ink_strlcpy(fname, filename, sizeof(fname));
  }

  const char *
  GetLockfileName()
  {
    return fname;
  }

  // Open()
  //
  // Tries to open a lock file, returning:
  //   -errno on error
  //   0 if someone is holding the lock (with holding_pid set)
  //   1 if we now have a writable lock file
  int Open(pid_t *holding_pid);

  // Get()
  //
  // Gets write access to a lock file, and if successful, truncates
  // file, and writes the current process ID.  Returns:
  //   -errno on error
  //   0 if someone is holding the lock (with holding_pid set)
  //   1 if we now have a writable lock file
  int Get(pid_t *holding_pid);

  // Close()
  //
  // Closes the file handle on the opened Lockfile.
  void Close();

  // Kill()
  // KillGroup()
  //
  // Ensures no one is holding the lock. It tries to open the lock file
  // and if that does not succeed, it kills the process holding the lock.
  // If the lock file open succeeds, it closes the lock file releasing
  // the lock.
  //
  // The intial signal can be used to generate a core from the process while
  // still ensuring it dies.
  void Kill(int sig, int initial_sig = 0, const char *pname = nullptr);
  void KillGroup(int sig, int initial_sig = 0, const char *pname = nullptr);

private:
  char fname[PATH_NAME_MAX];
  int fd = 0;
};
