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
#if !defined(_ink_cap_h_)
#define _ink_cap_h_

#include <unistd.h>
#include <sys/types.h>

#include "ts/ink_mutex.h"

/// Generate a debug message with the current capabilities for the process.
extern void DebugCapabilities(char const *tag ///< Debug message tag.
                              );
/// Set capabilities to persist across change of user id.
/// @return true on success
extern bool PreserveCapabilities();
/// Initialize and restrict the capabilities of a thread.
/// @return true on success
extern bool RestrictCapabilities();
/** Open a file, elevating privilege only if needed.

    @internal This is necessary because the CI machines run the regression tests
    as a normal user, not as root, so attempts to get privilege fail even though
    the @c open would succeed without elevation. So, try that first and ask for
    elevation only on an explicit permission failure.
*/
extern int elevating_open(char const *path, unsigned int flags, unsigned int fperms);
/// Open a file, elevating privilege only if needed.
extern int elevating_open(char const *path, unsigned int flags);
/// Open a file, elevating privilege only if needed.
extern FILE *elevating_fopen(char const *path, const char *mode);

/** Control generate of core file on crash.
    @a flag sets whether core files are enabled on crash.
    @return true on success
 */
extern bool EnableCoreFile(bool flag ///< New enable state.
                           );

void EnableDeathSignal(int signum);

enum ImpersonationLevel {
  IMPERSONATE_EFFECTIVE, // Set the effective credential set.
  IMPERSONATE_PERMANENT  // Set the real credential (permanently).
};

void ImpersonateUser(const char *user, ImpersonationLevel level);
void ImpersonateUserID(uid_t user, ImpersonationLevel level);

class ElevateAccess
{
public:
  typedef enum {
    FILE_PRIVILEGE     = 0x1u, ///< Access filesystem objects with privilege
    TRACE_PRIVILEGE    = 0x2u, ///< Trace other processes with privilege
    LOW_PORT_PRIVILEGE = 0x4u  ///< Bind to privilege ports.
  } privilege_level;

  ElevateAccess(unsigned level = FILE_PRIVILEGE);
  ~ElevateAccess();

  void elevate(unsigned level);
  void demote();

private:
  bool elevated;
  uid_t saved_uid;
  unsigned level;

  /// Acquire the privileges marked in @a mask for this process.
  void acquirePrivilege(unsigned priv_mask);
  /// Restore the privilege set to the state before acquiring them.
  void releasePrivilege();
#if !TS_USE_POSIX_CAP
  static ink_mutex lock; // only one thread at a time can elevate
#else
  void *cap_state; ///< Original capabilities state to restore.
#endif
};

#endif
