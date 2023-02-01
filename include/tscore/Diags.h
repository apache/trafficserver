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

/****************************************************************************

  Diags.h

  This file contains code to manipulate run-time diagnostics, and print
  warnings and errors at runtime.  Action tags and debugging tags are
  supported, allowing run-time conditionals affecting diagnostics.

 ****************************************************************************/

#pragma once

#include "DiagsTypes.h"
#include "SourceLocation.h"
#include "LogMessage.h"

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//      Macros                                                          //
//                                                                      //
//      The following are diagnostic macros that wrap up the compiler   //
//      __FILE__, __FUNCTION__, and __LINE__ macros into closures       //
//      and then invoke the closure on the remaining arguments.         //
//                                                                      //
//      This closure hack is done, because the cpp preprocessor doesn't //
//      support manipulation and union of varargs parameters.           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#if !defined(__GNUC__)
#ifndef __FUNCTION__
#define __FUNCTION__ nullptr
#endif
#endif

class DiagsPtr
{
public:
  friend Diags *diags();
  static void set(Diags *new_ptr);

private:
  static Diags *_diags_ptr;
};

inline Diags *
diags()
{
  return DiagsPtr::_diags_ptr;
}

// Note that the log functions being implemented as a macro has the advantage
// that the pre-compiler expands this in place such that the call to
// MakeSourceLocation happens at the call site for the function.
#define DiagsError(LEVEL, ...)                                          \
  do {                                                                  \
    static const SourceLocation DiagsError_loc = MakeSourceLocation();  \
    static LogMessage DiagsError_log_message;                           \
    DiagsError_log_message.message(LEVEL, DiagsError_loc, __VA_ARGS__); \
  } while (false)

#define Status(...)    DiagsError(DL_Status, __VA_ARGS__)    // Log information
#define Note(...)      DiagsError(DL_Note, __VA_ARGS__)      // Log significant information
#define Warning(...)   DiagsError(DL_Warning, __VA_ARGS__)   // Log concerning information
#define Error(...)     DiagsError(DL_Error, __VA_ARGS__)     // Log operational failure, fail CI
#define Fatal(...)     DiagsError(DL_Fatal, __VA_ARGS__)     // Log recoverable crash, fail CI, exit & allow restart
#define Alert(...)     DiagsError(DL_Alert, __VA_ARGS__)     // Log recoverable crash, fail CI, exit & restart, Ops attention
#define Emergency(...) DiagsError(DL_Emergency, __VA_ARGS__) // Log unrecoverable crash, fail CI, exit, Ops attention

/** Apply throttling to a log site.
 *
 * Logs using SiteThrottled* version will be throttled at a certain interval
 * that applies to the call site, regardless of whether the messages within
 * that interval are unique or not. This is helpful for logs which can be noisy
 * and frequently have differing content, such as the length of a buffer or a
 * counter. Rather than changing the log to contain less information, this can
 * be applied to the site so that when it is emitted, the information is
 * present, but the set of possibly slightly different logs will still be
 * suppressed against a configurable interval as a whole.
 */
#define SiteThrottledDiagsError(LEVEL, ...)                      \
  do {                                                           \
    static const SourceLocation STDE_loc = MakeSourceLocation(); \
    static LogMessage STDE_log_message{IS_THROTTLED};            \
    STDE_log_message.message(LEVEL, STDE_loc, __VA_ARGS__);      \
  } while (false)

#define SiteThrottledStatus(...)  SiteThrottledDiagsError(DL_Status, __VA_ARGS__)  // Log information
#define SiteThrottledNote(...)    SiteThrottledDiagsError(DL_Note, __VA_ARGS__)    // Log significant information
#define SiteThrottledWarning(...) SiteThrottledDiagsError(DL_Warning, __VA_ARGS__) // Log concerning information
#define SiteThrottledError(...)   SiteThrottledDiagsError(DL_Error, __VA_ARGS__)   // Log operational failure, fail CI
#define SiteThrottledFatal(...) \
  SiteThrottledDiagsError(DL_Fatal, __VA_ARGS__) // Log recoverable crash, fail CI, exit & allow restart
#define SiteThrottledAlert(...) \
  SiteThrottledDiagsError(DL_Alert, __VA_ARGS__) // Log recoverable crash, fail CI, exit & restart, Ops attention
#define SiteThrottledEmergency(...) \
  SiteThrottledDiagsError(DL_Emergency, __VA_ARGS__) // Log unrecoverable crash, fail CI, exit, Ops attention

#define DiagsErrorV(LEVEL, FMT, AP)                                      \
  do {                                                                   \
    static const SourceLocation DiagsErrorV_loc = MakeSourceLocation();  \
    static LogMessage DiagsErrorV_log_message;                           \
    DiagsErrorV_log_message.message_va(LEVEL, DiagsErrorV_loc, FMT, AP); \
  } while (false)

#define StatusV(fmt, ap)    DiagsErrorV(DL_Status, fmt, ap)
#define NoteV(fmt, ap)      DiagsErrorV(DL_Note, fmt, ap)
#define WarningV(fmt, ap)   DiagsErrorV(DL_Warning, fmt, ap)
#define ErrorV(fmt, ap)     DiagsErrorV(DL_Error, fmt, ap)
#define FatalV(fmt, ap)     DiagsErrorV(DL_Fatal, fmt, ap)
#define AlertV(fmt, ap)     DiagsErrorV(DL_Alert, fmt, ap)
#define EmergencyV(fmt, ap) DiagsErrorV(DL_Emergency, fmt, ap)

/** See the comment above SiteThrottledDiagsError for an explanation of how the
 * SiteThrottled functions behave. */
#define SiteThrottledDiagsErrorV(LEVEL, FMT, AP)                  \
  do {                                                            \
    static const SourceLocation STDEV_loc = MakeSourceLocation(); \
    static LogMessage STDEV_log_message{IS_THROTTLED};            \
    STDEV_log_message.message_va(LEVEL, STDEV_loc, FMT, AP);      \
  } while (false)

#define SiteThrottledStatusV(fmt, ap)    SiteThrottledDiagsErrorV(DL_Status, fmt, ap)
#define SiteThrottledNoteV(fmt, ap)      SiteThrottledDiagsErrorV(DL_Note, fmt, ap)
#define SiteThrottledWarningV(fmt, ap)   SiteThrottledDiagsErrorV(DL_Warning, fmt, ap)
#define SiteThrottledErrorV(fmt, ap)     SiteThrottledDiagsErrorV(DL_Error, fmt, ap)
#define SiteThrottledFatalV(fmt, ap)     SiteThrottledDiagsErrorV(DL_Fatal, fmt, ap)
#define SiteThrottledAlertV(fmt, ap)     SiteThrottledDiagsErrorV(DL_Alert, fmt, ap)
#define SiteThrottledEmergencyV(fmt, ap) SiteThrottledDiagsErrorV(DL_Emergency, fmt, ap)

#if TS_USE_DIAGS

/// A Diag version of the above.
#define Diag(TAG, ...)                                             \
  do {                                                             \
    if (unlikely(diags()->on())) {                                 \
      static const SourceLocation Diag_loc = MakeSourceLocation(); \
      static LogMessage Diag_log_message;                          \
      Diag_log_message.diag(TAG, Diag_loc, __VA_ARGS__);           \
    }                                                              \
  } while (false)

inline bool
is_dbg_ctl_enabled(DbgCtl const &ctl)
{
  return unlikely(diags()->on()) && ctl.ptr()->on;
}

// printf-line debug output.  First parameter must be DbgCtl instance. Assumes debug control is enabled, and
// debug output globablly enabled.
//
#define DbgPrint(CTL, ...)                                                             \
  do {                                                                                 \
    static const SourceLocation DbgPrintf_loc = MakeSourceLocation();                  \
    static LogMessage DbgPrintf_log_message;                                           \
    DbgPrintf_log_message.print(CTL.ptr()->tag, DL_Debug, DbgPrintf_loc, __VA_ARGS__); \
  } while (false)

// printf-like debug output.  First parameter must be an instance of DbgCtl.
//
#define Dbg(CTL, ...)                               \
  do {                                              \
    if (unlikely(diags()->on()) && CTL.ptr()->on) { \
      DbgPrint(CTL, __VA_ARGS__);                   \
    }                                               \
  } while (false)

// A BufferWriter version of Debug().
#define Debug_bw(tag__, fmt, ...)                                                        \
  do {                                                                                   \
    if (unlikely(diags()->on())) {                                                       \
      static DbgCtl Debug_bw_ctl(tag__);                                                 \
      if (Debug_bw_ctl.ptr()->on) {                                                      \
        DbgPrint(Debug_bw_ctl, "%s", ts::bwprint(ts::bw_dbg, fmt, __VA_ARGS__).c_str()); \
      }                                                                                  \
    }                                                                                    \
  } while (false)

// printf-like debug output.  First parameter must be tag (C-string literal, or otherwise
// a constexpr returning char const pointer to null-terminated C-string).
//
#define Debug(TAG, ...)                   \
  do {                                    \
    if (unlikely(diags()->on())) {        \
      static DbgCtl Debug_ctl(TAG);       \
      if (Debug_ctl.ptr()->on) {          \
        DbgPrint(Debug_ctl, __VA_ARGS__); \
      }                                   \
    }                                     \
  } while (false)

// Same as Dbg above, but this allows a positive override of the DbgCtl, if flag is true.
//
#define SpecificDbg(FLAG, CTL, ...) \
  do {                              \
    if (unlikely(diags()->on())) {  \
      if (FLAG || CTL.ptr()->on) {  \
        DbgPrint(CTL, __VA_ARGS__); \
      }                             \
    }                               \
  } while (false)

// For better performance, use this instead of diags()->on(tag) when the tag parameter is a C-string literal.
//
#define is_debug_tag_set(TAG)                 \
  (unlikely(diags()->on()) && ([]() -> bool { \
     static DbgCtl idts_ctl(TAG);             \
     return idts_ctl.ptr()->on != 0;          \
   }()))

#define SpecificDebug(FLAG, TAG, ...)               \
  do {                                              \
    {                                               \
      static DbgCtl SpecificDebug_ctl(TAG);         \
      if (unlikely(diags()->on())) {                \
        if (FLAG || SpecificDebug_ctl.ptr()->on) {  \
          DbgPrint(SpecificDebug_ctl, __VA_ARGS__); \
        }                                           \
      }                                             \
    }                                               \
  } while (false)

#define is_action_tag_set(_t)     unlikely(diags()->on(_t, DiagsTagType_Action))
#define debug_tag_assert(_t, _a)  (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t, _a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t)           is_debug_tag_set(_t) // Deprecated.

#else // TS_USE_DIAGS

#define Diag(...)
#define Dbg(...)
#define Debug(...)
#define SpecificDbg(...)

#define is_debug_tag_set(_t)      0
#define is_action_tag_set(_t)     0
#define debug_tag_assert(_t, _a)  /**/
#define action_tag_assert(_t, _a) /**/
#define is_diags_on(_t)           0

#endif // TS_USE_DIAGS
