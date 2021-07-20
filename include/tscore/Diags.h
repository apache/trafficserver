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

extern Diags *diags;

// Note that the log functions being implemented as a macro has the advantage
// that the pre-compiler expands this in place such that the call to
// MakeSourceLocation happens at the call site for the function.
#define DiagsError(level, ...)                              \
  do {                                                      \
    static const SourceLocation loc = MakeSourceLocation(); \
    static LogMessage log_message;                          \
    log_message.message(level, loc, __VA_ARGS__);           \
  } while (0)

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
  } while (0)

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
  } while (0)

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
  } while (0)

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
    if (unlikely(diags->on())) {                              \
      static const SourceLocation loc = MakeSourceLocation(); \
      static LogMessage log_message;                          \
      log_message.diag(tag, loc, __VA_ARGS__);                \
    }                                                         \
  } while (0)

/// A Debug version of the above.
#define Debug(tag, ...)                                       \
  do {                                                        \
    if (unlikely(diags->on())) {                              \
      static const SourceLocation loc = MakeSourceLocation(); \
      static LogMessage log_message;                          \
      log_message.debug(tag, loc, __VA_ARGS__);               \
    }                                                         \
  } while (0)

/** Same as Debug above, but this allows a positive override of the tag
 * mechanism by a flag boolean.
 *
 * @param[in] flag True if the message should be logged regardless of tag
 * configuration, false if the logging of the message should respsect the tag
 * configuration.
 */
#define SpecificDebug(flag, tag, ...)                                                                       \
  do {                                                                                                      \
    if (unlikely(diags->on())) {                                                                            \
      static const SourceLocation loc = MakeSourceLocation();                                               \
      static LogMessage log_message;                                                                        \
      flag ? log_message.print(tag, DL_Debug, loc, __VA_ARGS__) : log_message.debug(tag, loc, __VA_ARGS__); \
    }                                                                                                       \
  } while (0)

#define is_debug_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Debug))
#define is_action_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Action))
#define debug_tag_assert(_t, _a) (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t, _a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t) unlikely(diags->on(_t))

#else // TS_USE_DIAGS

#define Diag(tag, fmt, ...)
#define Debug(tag, fmt, ...)
#define SpecificDebug(flag, tag, ...)

#define is_debug_tag_set(_t) 0
#define is_action_tag_set(_t) 0
#define debug_tag_assert(_t, _a)  /**/
#define action_tag_assert(_t, _a) /**/
#define is_diags_on(_t) 0

#endif // TS_USE_DIAGS
