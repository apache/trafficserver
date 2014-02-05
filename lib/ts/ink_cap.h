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
/// @return 0 on success, non-zero otherwise.
extern int PreserveCapabilities();
/// Initialize and restrict the capabilities of a thread.
/// @return 0 on success, non-zero otherwise.
extern int RestrictCapabilities();

/** Control generate of core file on crash.
    @a flag sets whether core files are enabled on crash.
    @return 0 on success, @c errno on failre.
 */
extern int EnableCoreFile(
  bool flag ///< New enable state.
);



#if TS_USE_POSIX_CAP
bool elevateFileAccess(bool);
#else
bool restoreRootPriv(uid_t *old_euid = NULL);
bool removeRootPriv(uid_t euid);
#endif


class ElevateAccess {
public:
  ElevateAccess(const bool state): elevated(false), saved_uid(0) {
    if (state == true) {
      elevate();
    }
  }

  void elevate() {
#if TS_USE_POSIX_CAP
    elevateFileAccess(true);
#else
    // Since we are setting a process-wide credential, we have to block any other thread
    // attempting to elevate until this one demotes.
    restoreRootPriv(&saved_uid);
    ink_mutex_acquire(&lock);
#endif
    elevated = true;
  }

  void demote() {
#if TS_USE_POSIX_CAP
    elevateFileAccess(false);
#else
    removeRootPriv(saved_uid);
    ink_mutex_release(&lock);
#endif
    elevated = false;
  }

  ~ElevateAccess() {
    if (elevated == true) {
      demote();
    }
  }

private:
  bool elevated;
  uid_t saved_uid;
#if !TS_USE_POSIX_CAP
  static ink_mutex lock; // only one thread at a time can elevate
#endif
};

#endif
