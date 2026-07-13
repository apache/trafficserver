/** @file

  ConfigContextDiags.h — Convenience macros for config handler logging.

  These macros combine diags output (Note/Warning/Error) with ConfigContext
  task tracking in a single call. They format the message once and send it
  to both destinations:
    1. The ATS diagnostics system (diags.log / error.log)
    2. The ConfigContext reload task log (visible via traffic_ctl config status)

  This ensures operators see the same information whether they look at
  diags.log or query reload status via traffic_ctl / JSONRPC.

  @section when When to use these macros

  Use a macro when you want the message in BOTH diags and the reload task log.
  This is the common case for operational messages in config handlers:

  @code
    CfgLoadInProgress(ctx, "%s loading ...", filename);  // subtasks
    CfgLoadLog(ctx, DL_Note, "%s loading ...", filename); // top-level handlers
    CfgLoadComplete(ctx, "%s finished loading", filename);
    CfgLoadFail(ctx, "%s failed to load", filename);
  @endcode

  Use ctx methods directly when you only want the reload task log (no diags):

  @code
    ctx.log("parsed %d rules", count);         // task log only
    ctx.in_progress();                          // state change only, no message
    ctx.complete();                             // state change only, no message
  @endcode

  Use Dbg() directly when you only want debug output (no task log):

  @code
    Dbg(dbg_ctl_ssl, "internal detail ...");    // diags only, not in reload status
  @endcode

  Use CfgLoadDbg when you want BOTH debug output and the task log:

  @code
    CfgLoadDbg(ctx, dbg_ctl_ssl, "Reload SNI file");
  @endcode

  @section summary Quick reference

  | Want diags? | Want task log? | Use                               |
  |-------------|----------------|-----------------------------------|
  | Note        | yes + in_progress| CfgLoadInProgress(ctx, ...)     |
  | Note        | yes + complete  | CfgLoadComplete(ctx, ...)       |
  | Error       | yes + fail      | CfgLoadFail(ctx, ...)           |
  | Err + Errata| yes + fail      | CfgLoadFailWithErrata(...)      |
  | Note/Warn   | yes (no state)  | CfgLoadLog(ctx, DL_xxx, ...)    |
  | Dbg(tag)    | yes            | CfgLoadDbg(ctx, ctl, ...)       |
  | no          | yes            | ctx.log(...)                      |
  | no          | yes + state    | ctx.complete() / ctx.fail()       |
  | yes         | no             | Note/Warning/Error/Dbg directly   |

  @section errata Errata handling

  For failures with swoc::Errata detail, use CfgLoadFailWithErrata to
  combine the diags summary, errata detail, and state change in one call:

  @code
    CfgLoadFailWithErrata(ctx, errata, "%s failed to load", filename);
  @endcode

  This logs the formatted message to diags and the task log at the given
  severity, appends each errata annotation (with its own severity) to the
  task log, and marks the task as FAIL.

  @section fatal Fatal errors

  Fatal/Emergency terminate the process — reload status is irrelevant.
  Call Fatal() directly; do not use these macros for it.

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

#include "mgmt/config/ConfigContext.h"
#include "tscore/Diags.h"

/// Log a Note and mark the context as IN_PROGRESS.
/// The framework sets IN_PROGRESS on handler tasks automatically, so
/// CfgLoadLog(ctx, DL_Note, ...) is preferred for top-level "loading..."
/// messages. Use this macro for subtasks created via add_dependent_ctx().
///
///   CfgLoadInProgress(ctx, "%s loading ...", ts::filename::IP_ALLOW);
///
#define CfgLoadInProgress(CTX, FMT, ...)                            \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    Note("%s", _cfgctx_buf);                                        \
    (CTX).in_progress(_cfgctx_buf);                                 \
  } while (false)

/// Log a Note and mark the context as SUCCESS.
/// Use when a config load/reload operation finishes successfully.
///
///   CfgLoadComplete(ctx, "%s finished loading", ts::filename::IP_ALLOW);
///
#define CfgLoadComplete(CTX, FMT, ...)                              \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    Note("%s", _cfgctx_buf);                                        \
    (CTX).complete(_cfgctx_buf);                                    \
  } while (false)

/// Log an Error and mark the context as FAIL.
/// Use when a config load/reload operation fails.  Fail always implies
/// DL_Error — if the condition is merely degraded (not fatal to the load),
/// use CfgLoadLog(ctx, DL_Warning, ...) + CfgLoadComplete() instead.
///
///   CfgLoadFail(ctx, "%s failed to load", ts::filename::IP_ALLOW);
///
#define CfgLoadFail(CTX, FMT, ...)                                  \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    DiagsError(DL_Error, "%s", _cfgctx_buf);                        \
    (CTX).log(DL_Error, _cfgctx_buf);                               \
    (CTX).fail();                                                   \
  } while (false)

/// Log an Error, append errata detail to the task log, and mark the context
/// as FAIL.  Combines CfgLoadFail + ctx.fail(errata) in one call.
///
///   CfgLoadFailWithErrata(ctx, errata, "%s failed to load", filename);
///
#define CfgLoadFailWithErrata(CTX, ERRATA, FMT, ...)                \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    DiagsError(DL_Error, "%s", _cfgctx_buf);                        \
    (CTX).log(DL_Error, _cfgctx_buf);                               \
    (CTX).fail(ERRATA);                                             \
  } while (false)

/// Log at the given DiagsLevel and add to the task log, without changing state.
/// Use for intermediate informational messages during load/reload.
///
///   CfgLoadLog(ctx, DL_Note, "loaded %d categories from %s", count, filename);
///
#define CfgLoadLog(CTX, LEVEL, FMT, ...)                            \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    DiagsError(LEVEL, "%s", _cfgctx_buf);                           \
    (CTX).log(LEVEL, _cfgctx_buf);                                  \
  } while (false)

/// Log via a DbgCtl (debug-level, conditional on the tag) and add to the task
/// log. The debug output only appears when the tag is enabled; the task log
/// always receives the message.
///
///   CfgLoadDbg(ctx, dbg_ctl_ssl, "Reload SNI file");
///
#define CfgLoadDbg(CTX, CTL, FMT, ...)                              \
  do {                                                              \
    char _cfgctx_buf[1024];                                         \
    snprintf(_cfgctx_buf, sizeof(_cfgctx_buf), FMT, ##__VA_ARGS__); \
    Dbg((CTL), "%s", _cfgctx_buf);                                  \
    (CTX).log(DL_Debug, _cfgctx_buf);                               \
  } while (false)
