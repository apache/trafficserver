/** @file

  Diags type declarations.

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

/****************************************************************************

  DiagsTypes.h

  This file contains the type declarations for Diags logging.

 ****************************************************************************/

#pragma once

#include <cstdarg>
#include <string>
#include <string_view>
#include "tsutil/DbgCtl.h"
#include "tsutil/SourceLocation.h"
#include "tsutil/ts_diag_levels.h"
#include "tscore/BaseLogFile.h"
#include "tscore/ContFlags.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_mutex.h"
#include "tsutil/Regex.h"

#define DIAGS_MAGIC 0x12345678
#define BYTES_IN_MB 1000000

enum DiagsTagType {
  DiagsTagType_Debug  = 0, // do not renumber --- used as array index
  DiagsTagType_Action = 1
};

struct DiagsModeOutput {
  bool to_stdout;
  bool to_stderr;
  bool to_syslog;
  bool to_diagslog;
};

enum StdStream { STDOUT = 0, STDERR };

enum RollingEnabledValues { NO_ROLLING = 0, ROLL_ON_TIME, ROLL_ON_SIZE, ROLL_ON_TIME_OR_SIZE, INVALID_ROLLING_VALUE };

#define DiagsLevel_Count DL_Undefined

#define DiagsLevel_IsTerminal(_l) (((_l) >= DL_Fatal) && ((_l) < DL_Undefined))

// Cleanup Function Prototype - Called before ink_fatal to
//   cleanup process state
using DiagsCleanupFunc = void (*)();

class DiagsConfigState
{
public:
  static int
  enabled(DiagsTagType dtt)
  {
    return _enabled[dtt];
  }

  static void enabled(DiagsTagType dtt, int new_value);

  DiagsModeOutput outputs[DiagsLevel_Count]; // where each level prints

private:
  static int _enabled[2]; // one debug, one action
};

//////////////////////////////////////////////////////////////////////////////
//
//      class Diags
//
//      The Diags class is used for global configuration of the run-time
//      diagnostics system.  This class provides the following services:
//
//      * run-time notices, debugging, warnings, errors
//      * debugging tags to selectively enable & disable diagnostics
//      * action tags to selectively enable & disable code paths
//      * configurable output to stdout, stderr, syslog, error logs
//      * interface to supporting on-the-fly reconfiguration
//
//////////////////////////////////////////////////////////////////////////////

class Diags : public DebugInterface
{
public:
  Diags(std::string_view prefix_string, const char *base_debug_tags, const char *base_action_tags, BaseLogFile *_diags_log,
        int diags_log_perm = -1, int output_log_perm = -1);
  virtual ~Diags();

  BaseLogFile *diags_log;
  BaseLogFile *stdout_log;
  BaseLogFile *stderr_log;

  const unsigned int magic;
  DiagsConfigState   config;
  DiagsShowLocation  show_location;
  DiagsCleanupFunc   cleanup_func;

  ///////////////////////////
  // conditional debugging //
  ///////////////////////////

  bool
  get_override() const override
  {
    return get_cont_flag(ContFlags::DEBUG_OVERRIDE);
  }

  bool
  test_override_ip(IpEndpoint const &test_ip)
  {
    return this->debug_client_ip == test_ip;
  }

  // It seems to make a big difference to performance (due to the caching of the enabled flag) to call this
  // function first before doing anything else for debug output.  This includes entering blocks with static
  // DbgCtl instances, or other static variables with non-const initialization.  Static variables with
  // non-const initialization inside a function have a hidden flag that is checked every time the containing
  // block is entered, to see if the variable has been initialized or not.
  //
  bool
  on(DiagsTagType mode = DiagsTagType_Debug) const
  {
    return (config.enabled(mode) & 1) || (config.enabled(mode) == 2 && this->get_override());
  }

  // Returns true if tag is enabled for mode.
  //
  bool
  on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const
  {
    return unlikely(this->on(mode)) && tag_activated(tag, mode);
  }

  /////////////////////////////////////
  // low-level tag inquiry functions //
  /////////////////////////////////////

  // This does regex match against the tag.
  //
  bool tag_activated(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const;

  bool
  debug_tag_activated(const char *tag) const override
  {
    return tag_activated(tag);
  }

  /////////////////////////////
  // raw printing interfaces //
  /////////////////////////////

  ///////////////////////////////////////////////////////////////////////
  // user diagnostic output interfaces --- enabled on or off based     //
  // on the value of the enable flag, and the state of the debug tags. //
  ///////////////////////////////////////////////////////////////////////

  /// Print the log message without respect to whether the tag is enabled.
  void
  print(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(5, 6)
  {
    va_list ap;
    va_start(ap, fmt);
    print_va(tag, level, loc, fmt, ap);
    va_end(ap);
  }

  void print_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const override;

  /// Print the log message only if tag is enabled.
  void
  log(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(5, 6)
  {
    if (on(tag)) {
      va_list ap;
      va_start(ap, fmt);
      print_va(tag, level, loc, fmt, ap);
      va_end(ap);
    }
  }

  void
  log_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap)
  {
    if (on(tag)) {
      print_va(tag, level, loc, fmt, ap);
    }
  }

  void
  error(DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(4, 5)
  {
    va_list ap;
    va_start(ap, fmt);
    error_va(level, loc, fmt, ap);
    va_end(ap);
  }

  virtual void error_va(DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const;

  void dump(FILE *fp = stdout) const;

  void activate_taglist(const char *taglist, DiagsTagType mode = DiagsTagType_Debug);

  void deactivate_all(DiagsTagType mode = DiagsTagType_Debug);

  bool setup_diagslog(BaseLogFile *blf);
  void config_roll_diagslog(RollingEnabledValues re, int ri, int rs);
  void config_roll_outputlog(RollingEnabledValues re, int ri, int rs);
  bool reseat_diagslog();
  bool should_roll_diagslog();
  bool should_roll_outputlog();

  bool set_std_output(StdStream stream, const char *file);

  const char *base_debug_tags;  // internal copy of default debug tags
  const char *base_action_tags; // internal copy of default action tags

  IpAddr debug_client_ip;

private:
  const std::string prefix_str;
  mutable ink_mutex tag_table_lock;    // prevents reconfig/read races
  DFA              *activated_tags[2]; // 1 table for debug, 1 for action

  // These are the default logfile permissions
  int diags_logfile_perm  = -1;
  int output_logfile_perm = -1;

  // log rotation variables
  RollingEnabledValues outputlog_rolling_enabled;
  int                  outputlog_rolling_size;
  int                  outputlog_rolling_interval;
  RollingEnabledValues diagslog_rolling_enabled;
  int                  diagslog_rolling_interval;
  int                  diagslog_rolling_size;
  time_t               outputlog_time_last_roll;
  time_t               diagslog_time_last_roll;

  bool rebind_std_stream(StdStream stream, int new_fd);

  void
  lock() const
  {
    ink_mutex_acquire(&tag_table_lock);
  }

  void
  unlock() const
  {
    ink_mutex_release(&tag_table_lock);
  }
};
