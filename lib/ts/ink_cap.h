/** @file

  POSIX Capability related utilities.

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
#if !defined (_ink_cap_h_)
#define _ink_cap_h_
#include "ink_mutex.h"

/// Generate a debug message with the current capabilities for the process.
extern void DebugCapabilities(
  char const* tag ///< Debug message tag.
);
/// Set capabilities to persist across change of user id.
/// @return true on success
extern bool PreserveCapabilities();
/// Initialize and restrict the capabilities of a thread.
/// @return true on success
extern bool RestrictCapabilities();

/** Control generate of core file on crash.
    @a flag sets whether core files are enabled on crash.
    @return true on success
 */
extern bool EnableCoreFile(
  bool flag ///< New enable state.
);

void EnableDeathSignal(int signum);

enum ImpersonationLevel {
  IMPERSONATE_EFFECTIVE,  // Set the effective credential set.
  IMPERSONATE_PERMANENT   // Set the real credential (permanently).
};

void ImpersonateUser(const char * user, ImpersonationLevel level);
void ImpersonateUserID(uid_t user, ImpersonationLevel level);

class ElevateAccess {
public:

  typedef enum {
    FILE_PRIVILEGE  = 0x1u, // Access filesystem objects with privilege
    TRACE_PRIVILEGE = 0x2u  // Trace other processes with privilege
  } privilege_level;

  ElevateAccess(const bool state, unsigned level = FILE_PRIVILEGE);
  ~ElevateAccess();

  void elevate();
  void demote();

private:
  bool elevated;
  uid_t saved_uid;
  unsigned level;

#if !TS_USE_POSIX_CAP
  static ink_mutex lock; // only one thread at a time can elevate
#endif
};

#endif
