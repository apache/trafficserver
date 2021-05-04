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
#define DiagsError(level, ...)                              \
  do {                                                      \
    static const SourceLocation loc = MakeSourceLocation(); \
    static LogMessage log_message;                          \
    log_message.message(level, loc, __VA_ARGS__);           \
  } while (false)

#define Status(...) DiagsError(DL_Status, __VA_ARGS__)       // Log information
#define Note(...) DiagsError(DL_Note, __VA_ARGS__)           // Log significant information
#define Warning(...) DiagsError(DL_Warning, __VA_ARGS__)     // Log concerning information
#define Error(...) DiagsError(DL_Error, __VA_ARGS__)         // Log operational failure, fail CI
#define Fatal(...) DiagsError(DL_Fatal, __VA_ARGS__)         // Log recoverable crash, fail CI, exit & allow restart
#define Alert(...) DiagsError(DL_Alert, __VA_ARGS__)         // Log recoverable crash, fail CI, exit & restart, Ops attention
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
#define SiteThrottledDiagsError(level, ...)                 \
  do {                                                      \
    static const SourceLocation loc = MakeSourceLocation(); \
    static LogMessage log_message{IS_THROTTLED};            \
    log_message.message(level, loc, __VA_ARGS__);           \
  } while (false)

#define SiteThrottledStatus(...) SiteThrottledDiagsError(DL_Status, __VA_ARGS__)   // Log information
#define SiteThrottledNote(...) SiteThrottledDiagsError(DL_Note, __VA_ARGS__)       // Log significant information
#define SiteThrottledWarning(...) SiteThrottledDiagsError(DL_Warning, __VA_ARGS__) // Log concerning information
#define SiteThrottledError(...) SiteThrottledDiagsError(DL_Error, __VA_ARGS__)     // Log operational failure, fail CI
#define SiteThrottledFatal(...) \
  SiteThrottledDiagsError(DL_Fatal, __VA_ARGS__) // Log recoverable crash, fail CI, exit & allow restart
#define SiteThrottledAlert(...) \
  SiteThrottledDiagsError(DL_Alert, __VA_ARGS__) // Log recoverable crash, fail CI, exit & restart, Ops attention
#define SiteThrottledEmergency(...) \
  SiteThrottledDiagsError(DL_Emergency, __VA_ARGS__) // Log unrecoverable crash, fail CI, exit, Ops attention

#define DiagsErrorV(level, fmt, ap)                         \
  do {                                                      \
    static const SourceLocation loc = MakeSourceLocation(); \
    static LogMessage log_message;                          \
    log_message.message_va(level, loc, fmt, ap);            \
  } while (false)

#define StatusV(fmt, ap) DiagsErrorV(DL_Status, fmt, ap)
#define NoteV(fmt, ap) DiagsErrorV(DL_Note, fmt, ap)
#define WarningV(fmt, ap) DiagsErrorV(DL_Warning, fmt, ap)
#define ErrorV(fmt, ap) DiagsErrorV(DL_Error, fmt, ap)
#define FatalV(fmt, ap) DiagsErrorV(DL_Fatal, fmt, ap)
#define AlertV(fmt, ap) DiagsErrorV(DL_Alert, fmt, ap)
#define EmergencyV(fmt, ap) DiagsErrorV(DL_Emergency, fmt, ap)

/** See the comment above SiteThrottledDiagsError for an explanation of how the
 * SiteThrottled functions behave. */
#define SiteThrottledDiagsErrorV(level, fmt, ap)            \
  do {                                                      \
    static const SourceLocation loc = MakeSourceLocation(); \
    static LogMessage log_message{IS_THROTTLED};            \
    log_message.message_va(level, loc, fmt, ap);            \
  } while (false)

#define SiteThrottledStatusV(fmt, ap) SiteThrottledDiagsErrorV(DL_Status, fmt, ap)
#define SiteThrottledNoteV(fmt, ap) SiteThrottledDiagsErrorV(DL_Note, fmt, ap)
#define SiteThrottledWarningV(fmt, ap) SiteThrottledDiagsErrorV(DL_Warning, fmt, ap)
#define SiteThrottledErrorV(fmt, ap) SiteThrottledDiagsErrorV(DL_Error, fmt, ap)
#define SiteThrottledFatalV(fmt, ap) SiteThrottledDiagsErrorV(DL_Fatal, fmt, ap)
#define SiteThrottledAlertV(fmt, ap) SiteThrottledDiagsErrorV(DL_Alert, fmt, ap)
#define SiteThrottledEmergencyV(fmt, ap) SiteThrottledDiagsErrorV(DL_Emergency, fmt, ap)

#if TS_USE_DIAGS

/// A Diag version of the above.
#define Diag(tag, ...)                                        \
  do {                                                        \
    if (unlikely(diags()->on())) {                            \
      static const SourceLocation loc = MakeSourceLocation(); \
      static LogMessage log_message;                          \
      log_message.diag(tag, loc, __VA_ARGS__);                \
    }                                                         \
  } while (false)

inline bool
is_dbg_ctl_enabled(DbgCtl const &ctl)
{
  return unlikely(diags()->on()) && ctl.ptr()->on;
}

// printf-line debug output.  First parameter must be DbgCtl instance. Assumes debug control is enabled, and
// debug output globablly enabled.
//
#define DbgPrint(ctl__, ...)                                             \
  do {                                                                   \
    static const SourceLocation loc__ = MakeSourceLocation();            \
    static LogMessage log_message__;                                     \
    log_message__.print(ctl__.ptr()->tag, DL_Debug, loc__, __VA_ARGS__); \
  } while (false)

// printf-like debug output.  First parameter must be an instance of DbgCtl.
//
#define Dbg(ctl__, ...)                               \
  do {                                                \
    if (unlikely(diags()->on()) && ctl__.ptr()->on) { \
      DbgPrint(ctl__, __VA_ARGS__);                   \
    }                                                 \
  } while (false)

// printf-like debug output.  First parameter must be tag (C-string literal, or otherwise
// a constexpr returning char const pointer to null-terminated C-string).
//
#define Debug(tag__, ...)             \
  do {                                \
    if (unlikely(diags()->on())) {    \
      static DbgCtl ctl__(tag__);     \
      if (ctl__.ptr()->on) {          \
        DbgPrint(ctl__, __VA_ARGS__); \
      }                               \
    }                                 \
  } while (false)

// Same as Dbg above, but this allows a positive override of the DbgCtl, if flag is true.
//
#define SpecificDbg(flag__, ctl__, ...) \
  do {                                  \
    if (unlikely(diags()->on())) {      \
      if (flag__ || ctl__.ptr()->on) {  \
        DbgPrint(ctl__, __VA_ARGS__);   \
      }                                 \
    }                                   \
  } while (false)

// For better performance, use this instead of diags()->on(tag) when the tag parameter is a C-string literal.
//
#define is_debug_tag_set(tag__)               \
  (unlikely(diags()->on()) && ([]() -> bool { \
     static DbgCtl ctl__(tag__);              \
     return ctl__.ptr()->on != 0;             \
   }()))

#define SpecificDebug(flag__, tag__, ...) \
  do {                                    \
    {                                     \
      static DbgCtl ctl__(tag__);         \
      if (unlikely(diags()->on())) {      \
        if (flag__ || ctl__.ptr()->on) {  \
          DbgPrint(ctl__, __VA_ARGS__);   \
        }                                 \
      }                                   \
    }                                     \
  } while (false)

#define is_action_tag_set(_t) unlikely(diags()->on(_t, DiagsTagType_Action))
#define debug_tag_assert(_t, _a) (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t, _a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t) is_debug_tag_set(_t) // Deprecated.

#else // TS_USE_DIAGS

#define Diag(...)
#define Dbg(...)
#define Debug(...)
#define SpecificDbg(...)

#define is_debug_tag_set(_t) 0
#define is_action_tag_set(_t) 0
#define debug_tag_assert(_t, _a)  /**/
#define action_tag_assert(_t, _a) /**/
#define is_diags_on(_t) 0

#endif // TS_USE_DIAGS
