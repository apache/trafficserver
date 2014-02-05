/** @file

    A brief file description

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

# include "ink_config.h"
# include "Diags.h"
# include "ink_cap.h"
# include "ink_thread.h"

# if TS_USE_POSIX_CAP
#   include <sys/capability.h>
#   include <sys/prctl.h>
# endif

# if !TS_USE_POSIX_CAP
ink_mutex ElevateAccess::lock = INK_MUTEX_INIT;
#endif

void
DebugCapabilities(char const* tag) {
  if (is_debug_tag_set(tag)) {
#   if TS_USE_POSIX_CAP
      cap_t caps = cap_get_proc();
      char* caps_text = cap_to_text(caps, 0);
#   endif

#     if TS_USE_POSIX_CAP
    Debug(tag, "uid=%u, gid=%u, euid=%u, egid=%u, caps %s core=%s thread=0x%llx",
	  static_cast<unsigned int>(getuid()),
	  static_cast<unsigned int>(getgid()),
	  static_cast<unsigned int>(geteuid()),
	  static_cast<unsigned int>(getegid()),
	  caps_text,
	  prctl(PR_GET_DUMPABLE) != 1 ? "disabled" : "enabled",
	  (unsigned long long)pthread_self() );
    cap_free(caps_text);
    cap_free(caps);
#else
    Debug(tag, "uid=%u, gid=%u, euid=%u, egid=%u",
	  static_cast<unsigned int>(getuid()),
	  static_cast<unsigned int>(getgid()),
	  static_cast<unsigned int>(geteuid()),
	  static_cast<unsigned int>(getegid()) );
#endif
  }
}

int
PreserveCapabilities() {
  int zret = 0;
# if TS_USE_POSIX_CAP
    zret = prctl(PR_SET_KEEPCAPS, 1);
# endif
  Debug("proxy_priv", "[PreserveCapabilities] zret : %d\n", zret);
  return zret;
}

// Adjust the capabilities to only those needed.
int
RestrictCapabilities() {
  int zret = 0; // return value.
# if TS_USE_POSIX_CAP
    cap_t caps = cap_init(); // start with nothing.
    // Capabilities we need.
    cap_value_t perm_list[] = { CAP_NET_ADMIN, CAP_NET_BIND_SERVICE, CAP_IPC_LOCK, CAP_DAC_OVERRIDE};
    static int const PERM_CAP_COUNT = sizeof(perm_list)/sizeof(*perm_list);
    cap_value_t eff_list[] = { CAP_NET_ADMIN, CAP_NET_BIND_SERVICE, CAP_IPC_LOCK};
    static int const EFF_CAP_COUNT = sizeof(eff_list)/sizeof(*eff_list);

    cap_set_flag(caps, CAP_PERMITTED, PERM_CAP_COUNT, perm_list, CAP_SET);
    cap_set_flag(caps, CAP_EFFECTIVE, EFF_CAP_COUNT, eff_list, CAP_SET);
    zret = cap_set_proc(caps);
    cap_free(caps);
#  endif
  Debug("proxy_priv", "[RestrictCapabilities] zret : %d\n", zret);
  return zret;
}

int
EnableCoreFile(bool flag) {
  int zret = 0;
# if defined(linux)
    int state = flag ? 1 : 0;
    if (0 > (zret = prctl(PR_SET_DUMPABLE, state, 0, 0, 0))) {
      Warning("Unable to set PR_DUMPABLE : %s", strerror(errno));
    } else if (state != prctl(PR_GET_DUMPABLE)) {
      zret = ENOSYS; // best guess
      Warning("Call to set PR_DUMPABLE was ineffective");
    }
# endif  // linux check
  Debug("proxy_priv", "[EnableCoreFile] zret : %d\n", zret);
  return zret;
}

#if TS_USE_POSIX_CAP
/** Control file access privileges to bypass DAC.
    @parm state Use @c true to enable elevated privileges,
    @c false to disable.
    @return @c true if successful, @c false otherwise.

    @internal After some pondering I decided that the file access
    privilege was worth the effort of restricting. Unlike the network
    privileges this can protect a host system from programming errors
    by not (usually) permitting such errors to access arbitrary
    files. This is particularly true since none of the config files
    current enable this feature so it's not actually called. Still,
    best to program defensively and have it available.
 */
bool
elevateFileAccess(bool state)
{
  Debug("proxy_priv", "[elevateFileAccess] state : %d\n", state);

  bool zret = false; // return value.
  cap_t cap_state = cap_get_proc(); // current capabilities
  // Make a list of the capabilities we changed.
  cap_value_t cap_list[] = { CAP_DAC_OVERRIDE };
  static int const CAP_COUNT = sizeof(cap_list)/sizeof(*cap_list);

  cap_set_flag(cap_state, CAP_EFFECTIVE, CAP_COUNT, cap_list, state ? CAP_SET : CAP_CLEAR);
  zret = (0 == cap_set_proc(cap_state));
  cap_free(cap_state);
  Debug("proxy_priv", "[elevateFileAccess] zret : %d\n", zret);
  return zret;
}
#else
//  bool removeRootPriv()
//
//    - Returns true on success
//      and false on failure
bool
removeRootPriv(uid_t euid)
{
  if (seteuid(euid) < 0) {
    Debug("proxy_priv", "[removeRootPriv] seteuid failed : %s\n", strerror(errno));
    return false;
  }

  Debug("proxy_priv", "[removeRootPriv] removed root privileges.  Euid is %d\n", euid);
  return true;
}

//  bool restoreRootPriv()
//
//    - Returns true on success
//      and false on failure
bool
restoreRootPriv(uid_t *old_euid)
{
  if (old_euid)
    *old_euid = geteuid();
  if (seteuid(0) < 0) {
    Debug("proxy_priv", "[restoreRootPriv] seteuid root failed : %s\n", strerror(errno));
    return false;
  }

  Debug("proxy_priv", "[restoreRootPriv] restored root privileges.  Euid is %d\n", 0);

  return true;
}
#endif
