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
#include <memory>
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

/// Sentinel value stored in Diags::magic to detect heap corruption or
/// use-after-free of a Diags instance. Diags::magic == DIAGS_MAGIC for
/// any properly constructed, living instance.
#define DIAGS_MAGIC 0x12345678

/// Bytes-per-megabyte (decimal, not binary). Used by configuration
/// parsing to convert MB-valued knobs into byte counts.
#define BYTES_IN_MB 1000000

/**
 * @brief Selects which of the two tag tables a Diags operation addresses:
 *   debug tags (controlling Dbg/Diag emission) or action tags (controlling
 *   is_action_tag_set conditional code paths).
 *
 * @note Numeric values are used as array indices into the enable-state
 *   array and Diags::activated_tags. Do not renumber.
 */
enum DiagsTagType { DiagsTagType_Debug = 0, DiagsTagType_Action = 1 };

/**
 * @brief Per-DiagsLevel output destination configuration.
 *
 * Each boolean controls whether messages at the owning level are emitted
 * to the corresponding sink. Multiple sinks may be set simultaneously;
 * the message is emitted to all enabled sinks in unspecified order.
 *
 * @par Thread safety
 * Carries no internal synchronization. Callers reading or
 *   modifying Diags::config.outputs[] must respect the concurrency contract
 *   of the containing DiagsConfigState.
 */
struct DiagsModeOutput {
  bool to_stdout;
  bool to_stderr;
  bool to_syslog;
  bool to_diagslog;
};

/**
 * @brief Identifies which standard stream Diags::set_std_output reseats.
 *
 * STDOUT targets the standard output stream; STDERR targets the standard
 * error stream.
 */
enum StdStream { STDOUT = 0, STDERR };

/**
 * @brief Selects the rolling policy for a managed log file.
 *
 * - NO_ROLLING             — never roll; file grows without bound.
 * - ROLL_ON_TIME           — roll when the configured time interval elapses.
 * - ROLL_ON_SIZE           — roll when the configured size threshold is exceeded.
 * - ROLL_ON_TIME_OR_SIZE   — roll when either condition is satisfied.
 * - INVALID_ROLLING_VALUE  — sentinel; any code path receiving this value MUST
 *   treat it as NO_ROLLING. MUST be the largest enumerator so that range checks
 *   can detect out-of-range configuration values.
 */
enum RollingEnabledValues { NO_ROLLING = 0, ROLL_ON_TIME, ROLL_ON_SIZE, ROLL_ON_TIME_OR_SIZE, INVALID_ROLLING_VALUE };

/// Count of valid DiagsLevel values; equal to DL_Undefined and used to
/// size DiagsConfigState::outputs[].
#define DiagsLevel_Count DL_Undefined

/// True if the given DiagsLevel causes process termination after emission.
/// Terminal levels are DL_Fatal and above (excluding DL_Undefined).
#define DiagsLevel_IsTerminal(_l) (((_l) >= DL_Fatal) && ((_l) < DL_Undefined))

/**
 * @brief Cleanup callback invoked before process termination on a terminal
 *   DiagsLevel (Fatal, Alert, Emergency).
 *
 * @pre The callback runs on the thread that emitted the terminal message.
 *   It MUST NOT throw. It MUST be async-signal-safe if terminal-level
 *   emission can occur from a signal handler.
 * @post On return, the process continues into the exit path for the terminal
 *   level: DL_Fatal and DL_Alert invoke ink_fatal_va; DL_Emergency invokes
 *   ink_emergency_va (a distinct, stronger exit path).
 * @par Thread safety
 * Only one callback is installed per Diags instance.
 *   The callback is invoked at most once per process.
 */
using DiagsCleanupFunc = void (*)();

/**
 * @brief Bundles the two orthogonal pieces of Diags output configuration:
 *   the process-global per-tag enable state and the per-level output routing.
 *
 * The enable state for each DiagsTagType is one of:
 *   0 — disabled (no emission),
 *   1 — enabled for all callers,
 *   2 — enabled only when ContFlags::DEBUG_OVERRIDE is set on the
 *       current Continuation (per-connection debug override),
 *   3 — treated identically to 1 (always-enabled).
 *
 * The output routing is stored in @c outputs[], one entry per DiagsLevel.
 *
 * @par Thread safety
 * The enable state is process-global (static storage). Reads via
 *   enabled() and writes via enabled(dtt, value) carry no internal
 *   synchronization; callers must provide external serialization when a
 *   write may race a read. The @c outputs[] array carries no internal
 *   synchronization either; the same serialization requirement applies.
 */
class DiagsConfigState
{
public:
  /**
   * @brief Return the current enable state for the given tag type.
   *
   * @param[in] dtt DiagsTagType_Debug or DiagsTagType_Action.
   * @return 0 (disabled), 1 (enabled), 2 (DEBUG_OVERRIDE mode), or 3
   *   (always-enabled, same effect as 1).
   * @pre None.
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Not thread-safe. The read carries no synchronization;
   *   concurrent writes from a reconfiguration thread are a data race.
   */
  static int
  enabled(DiagsTagType dtt)
  {
    return _enabled[dtt];
  }

  /**
   * @brief Set the enable state for the given tag type.
   *
   * @param[in] dtt DiagsTagType_Debug or DiagsTagType_Action.
   * @param[in] new_value 0, 1, 2, or 3 (see class contract); 3 behaves
   *   identically to 1.
   * @pre new_value is in [0, 3]; values outside this range produce
   *   unspecified behavior.
   * @post enabled(dtt) returns new_value on all subsequent calls. When
   *   dtt == DiagsTagType_Debug and the value changes, the DbgCtl fast-path
   *   enable state is updated via a relaxed atomic store so that DbgCtl-based
   *   checks remain in sync without acquiring a lock.
   * @par Errors
   * None.
   * @par Thread safety
   * Must be called with external serialization against
   *   concurrent enabled(dtt) reads.
   */
  static void enabled(DiagsTagType dtt, int new_value);

  /// Per-level output routing. Each element controls where messages at
  /// the corresponding DiagsLevel are sent. See DiagsModeOutput.
  DiagsModeOutput outputs[DiagsLevel_Count];

private:
  static int _enabled[2]; // one debug, one action
};

/**
 * @brief Active diagnostic emission and tag-table state for the process.
 *
 * Owns the diags.log, stdout, and stderr BaseLogFile handles, the per-level
 * output routing configuration, the regex tables for debug and action tags,
 * and the rolling policy for the diagnostic and standard-output logs.
 * All diagnostic emission macros (Status, Note, Warning, Error, Fatal, Alert,
 * Emergency, Diag) route through the process-global Diags instance installed
 * via DiagsPtr.
 *
 * @par Thread safety
 * Concurrent emission calls (print, log, error) hold tag_table_lock for
 *   diagslog, stdout, and stderr output. On non-FreeBSD platforms the lock is
 *   released before the syslog() call, so concurrent syslog output is not
 *   serialized against other emission calls. Reconfiguration writes to
 *   config.outputs do not acquire tag_table_lock, so concurrent emission
 *   and reconfiguration are a data race on config.outputs. Tag-table
 *   mutation methods (activate_taglist,
 *   deactivate_all) acquire tag_table_lock internally. Log-pointer swap methods
 *   (reseat_diagslog, should_roll_*, set_std_output) acquire tag_table_lock
 *   only for their pointer swap step; file operations run outside the lock.
 *   setup_diagslog() acquires no lock; callers must serialize it.
 *   Rolling-policy configuration methods (config_roll_diagslog,
 *   config_roll_outputlog) and dump() do not acquire any lock; see the
 *   per-method @par Thread safety paragraphs below for details.
 */
class Diags : public DebugInterface
{
public:
  /**
   * @brief Construct a Diags instance with the given initial configuration.
   *
   * @param[in] prefix_string Tag prefix prepended to all debug output. Must
   *   be non-empty.
   * @param[in] base_debug_tags Initial debug tag regex, or nullptr for none.
   * @param[in] base_action_tags Initial action tag regex, or nullptr for none.
   * @param[in] _diags_log BaseLogFile for diags.log output, or nullptr to
   *   suppress file logging. Ownership transfers to this Diags instance.
   * @param[in] diags_log_perm File permission bits for diags.log, or -1 for
   *   the system default (LOGFILE_DEFAULT_PERMS).
   * @param[in] output_log_perm File permission bits for stdout/stderr log
   *   files, or -1 for the system default.
   * @pre prefix_string is non-empty; safe to construct before any thread
   *   reads via diags().
   * @post magic == DIAGS_MAGIC; tag regex strings are stored; activated tag
   *   tables are empty until activate_taglist() is called (typically via
   *   reconfigure_diags()); diags_log is set and open if _diags_log is
   *   non-null and openable, otherwise diags_log is null.
   * @par Errors
   * Allocation failures abort via ink_abort. Log-open failures result in
   *   diags_log being null and are reported via log_log_error only when
   *   BASELOGFILE_DEBUG_MODE is enabled (off by default).
   * @par Thread safety
   * Single-threaded construction expected. After construction
   *   the instance may be installed via DiagsPtr::set and used concurrently.
   */
  Diags(std::string_view prefix_string, const char *base_debug_tags, const char *base_action_tags, BaseLogFile *_diags_log,
        int diags_log_perm = -1, int output_log_perm = -1);

  /**
   * @brief Destroy the Diags instance, closing all owned log files.
   *
   * @pre No thread is currently executing a method on this instance.
   * @post All owned BaseLogFile handles are deleted (closing their FILE *).
   * @par Errors
   * None.
   * @par Thread safety
   * Single-threaded destruction. The caller must ensure no
   *   other thread holds or will acquire a reference to this instance.
   */
  virtual ~Diags();

  /// Diagnostic log file. Owned by this instance. Read-only from outside
  /// the tscore module; mutation via any path other than setup_diagslog /
  /// reseat_diagslog is undefined behavior.
  BaseLogFile *diags_log;

  /// Standard-output redirection file. Owned by this instance.
  /// Read-only from outside the tscore module.
  BaseLogFile *stdout_log;

  /// Standard-error redirection file. Owned by this instance.
  /// Read-only from outside the tscore module.
  BaseLogFile *stderr_log;

  /// Sentinel. Always DIAGS_MAGIC for a live, properly constructed instance.
  const unsigned int magic;

  /// Per-level output routing and tag-enable state. See DiagsConfigState.
  DiagsConfigState config;

  /// Controls whether source location is appended to emitted messages.
  DiagsShowLocation show_location;

  /// Optional cleanup callback invoked before terminal-level process exit.
  /// May be nullptr (no cleanup). See DiagsCleanupFunc.
  DiagsCleanupFunc cleanup_func;

  ///////////////////////////
  // conditional debugging //
  ///////////////////////////

  /**
   * @brief Return the per-connection DEBUG_OVERRIDE flag for the current
   *   Continuation.
   *
   * @return True if ContFlags::DEBUG_OVERRIDE is set on the currently
   *   executing Continuation; false otherwise or if no Continuation is active.
   * @pre None.
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Safe to call from any thread; reads a thread-local
   *   Continuation flag.
   */
  bool
  get_override() const override
  {
    return get_cont_flag(ContFlags::DEBUG_OVERRIDE);
  }

  /**
   * @brief Test whether the given IP endpoint matches the configured debug
   *   client IP.
   *
   * @param[in] test_ip Endpoint whose IP address is compared against the
   *   configured debug client IP. The port is ignored.
   * @return True if the IP address of @a test_ip equals the configured
   *   debug client IP; false otherwise or if no debug client IP is set.
   * @pre None.
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Not thread-safe. Reads debug_client_ip without
   *   synchronization; concurrent writes from the config-update callback are
   *   a data race.
   */
  bool
  test_override_ip(IpEndpoint const &test_ip)
  {
    return this->debug_client_ip == test_ip;
  }

  /**
   * @brief Test whether emission for the given tag mode is globally enabled.
   *
   * @param[in] mode DiagsTagType_Debug or DiagsTagType_Action; defaults to
   *   DiagsTagType_Debug.
   * @return True if the tag type is unconditionally enabled (value 1 or 3),
   *   OR if it is in DEBUG_OVERRIDE mode (value 2) and the current
   *   Continuation has the override flag set.
   * @pre None.
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Not thread-safe. The enable-state read carries no synchronization;
   *   concurrent writes from a reconfiguration thread are a data race.
   * @note This method is on the hot path of every Dbg/Diag macro. Call it
   *   before any block-local static with non-const initialization to avoid
   *   the hidden initialization-check overhead when diagnostics are off.
   */
  bool
  on(DiagsTagType mode = DiagsTagType_Debug) const
  {
    return (config.enabled(mode) & 1) || (config.enabled(mode) == 2 && this->get_override());
  }

  /**
   * @brief Test whether the given tag is active for the given mode.
   *
   * @param[in] tag C string naming the tag to check, or nullptr.
   * @param[in] mode DiagsTagType_Debug or DiagsTagType_Action.
   * @return True if the tag mode is globally enabled AND the tag matches
   *   the configured tag regex. A null tag matches unconditionally.
   * @pre None (null tag is safe).
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * The tag_activated sub-call is safe: the active regex
   *   pointer is snapshotted under tag_table_lock, with regex execution
   *   outside the lock. The on(mode) sub-call carries no synchronization on
   *   the enable-state read; see DiagsConfigState::enabled().
   */
  bool
  on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const
  {
    return unlikely(this->on(mode)) && tag_activated(tag, mode);
  }

  /////////////////////////////////////
  // low-level tag inquiry functions //
  /////////////////////////////////////

  /**
   * @brief Test whether a tag string matches the active regex for the given
   *   tag type.
   *
   * @param[in] tag C string to match, or nullptr.
   * @param[in] mode DiagsTagType_Debug or DiagsTagType_Action.
   * @return True if tag matches the compiled regex for mode. If tag is
   *   nullptr, returns true unconditionally.
   * @pre None (null tag is safe).
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Safe to call concurrently. The active regex pointer is
   *   snapshotted under tag_table_lock; regex execution runs outside the lock.
   */
  bool tag_activated(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const;

  /**
   * @brief DebugInterface override: test whether a debug tag is active.
   *
   * @param[in] tag C string naming the debug tag, or nullptr.
   * @return True if tag is active in DiagsTagType_Debug mode. A null tag
   *   returns true unconditionally.
   * @pre None (null tag is safe).
   * @post No state change.
   * @par Errors
   * None.
   * @par Thread safety
   * Same as tag_activated.
   */
  bool
  debug_tag_activated(const char *tag) const override
  {
    return tag_activated(tag);
  }

  /////////////////////////////
  // raw printing interfaces //
  /////////////////////////////

  /**
   * @brief Emit a message unconditionally, regardless of tag state.
   *
   * @param[in] tag C string label included in output, or nullptr to omit the
   *   tag prefix. Not checked against the tag regex.
   * @param[in] level DiagsLevel for sink routing.
   * @param[in] loc Source location of the call site, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @pre fmt is non-null and arguments match its conversions.
   * @post Message emitted to all sinks enabled for level in config.outputs.
   *   stderr is also written when regression_testing_on is true, even if
   *   config.outputs[level].to_stderr is false.
   *   Does not handle terminal levels; the process does not exit.
   *   Use error_va() if terminal-level exit behavior is required.
   * @par Errors
   * I/O errors during emission are absorbed; output may be lost.
   * @par Thread safety
   * Safe to call concurrently from any thread.
   */
  void
  print(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(5, 6)
  {
    va_list ap;
    va_start(ap, fmt);
    print_va(tag, level, loc, fmt, ap);
    va_end(ap);
  }

  /**
   * @brief va_list form of print().
   *
   * @param[in] tag Tag label, or nullptr to omit the tag prefix.
   * @param[in] level DiagsLevel for routing.
   * @param[in] loc Source location, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @param[in] ap Initialized va_list whose types match fmt. Consumed by this
   *   call; the caller MUST NOT reuse ap without va_end + va_start.
   * @pre fmt is non-null; ap is initialized.
   * @post Same as print().
   * @par Errors
   * Same as print().
   * @par Thread safety
   * Same as print().
   */
  void print_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const override;

  /**
   * @brief Emit a message only if the tag is active in DiagsTagType_Debug.
   *
   * @param[in] tag C string naming the debug tag, or nullptr (null matches
   *   unconditionally).
   * @param[in] level DiagsLevel for routing.
   * @param[in] loc Source location, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @pre fmt is non-null; arguments match fmt's conversions.
   * @post If on(tag) is true, message is emitted identically to print().
   *   If on(tag) is false, no output is produced.
   * @par Errors
   * Same as print().
   * @par Thread safety
   * Same as print(). The on() call carries no synchronization on
   *   the enable-state read; see DiagsConfigState::enabled().
   */
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

  /**
   * @brief va_list form of log().
   *
   * @param[in] tag Tag name, or nullptr (null matches unconditionally).
   * @param[in] level DiagsLevel for routing.
   * @param[in] loc Source location, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @param[in] ap Initialized va_list. Consumed; caller MUST NOT reuse
   *   without va_end + va_start.
   * @pre fmt is non-null; ap is initialized.
   * @post Same as log().
   * @par Errors
   * Same as print().
   * @par Thread safety
   * Same as log(). The on() call carries no synchronization on
   *   the enable-state read; see DiagsConfigState::enabled().
   */
  void
  log_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap)
  {
    if (on(tag)) {
      print_va(tag, level, loc, fmt, ap);
    }
  }

  /**
   * @brief Emit a message at the given level unconditionally.
   *
   * @param[in] level DiagsLevel for routing and terminal handling.
   * @param[in] loc Source location, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @pre fmt is non-null; arguments match its conversions.
   * @post Message emitted to all enabled sinks. For terminal levels the
   *   process exits — does not return.
   * @par Errors
   * Same as print().
   * @par Thread safety
   * Safe to call concurrently from any thread.
   */
  void
  error(DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(4, 5)
  {
    va_list ap;
    va_start(ap, fmt);
    error_va(level, loc, fmt, ap);
    va_end(ap);
  }

  /**
   * @brief va_list form of error().
   *
   * @param[in] level DiagsLevel for routing and terminal handling.
   * @param[in] loc Source location, or nullptr.
   * @param[in] fmt Non-null printf-format string.
   * @param[in] ap Initialized va_list. Consumed; caller MUST NOT reuse
   *   without va_end + va_start.
   * @pre fmt is non-null; ap is initialized.
   * @post Same as error(). For terminal levels, invokes cleanup_func (if
   *   set) then exits the process; does not return.
   * @par Errors
   * None signaled; I/O errors absorbed.
   * @par Thread safety
   * Safe to call concurrently.
   */
  virtual void error_va(DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const;

  /**
   * @brief Print the current Diags configuration to fp.
   *
   * @param[in] fp Destination FILE *; defaults to stdout.
   * @pre fp is a valid open stream.
   * @post Configuration summary written to fp.
   * @par Errors
   * I/O errors on fp are not signaled.
   * @par Thread safety
   * Not thread-safe. Reads config fields (enable state,
   *   outputs, base_debug_tags, base_action_tags) without holding
   *   tag_table_lock. Concurrent reconfiguration may produce
   *   interleaved or inconsistent output.
   */
  void dump(FILE *fp = stdout) const;

  /**
   * @brief Enable tags matching the given PCRE2 pattern for the given mode.
   *
   * @param[in] taglist PCRE2 regex string, or nullptr. If nullptr, this call
   *   is a no-op and the previous pattern (if any) is unchanged.
   * @param[in] mode DiagsTagType_Debug or DiagsTagType_Action.
   * @pre taglist is null or a valid PCRE2 pattern string. The regex engine is
   *   PCRE2, not POSIX ERE; PCRE2-specific syntax (lookaheads, named groups,
   *   etc.) is accepted.
   * @post If taglist is non-null: the new pattern unconditionally replaces the
   *   previous one. If the pattern fails to compile, the previous pattern is
   *   cleared and no tags will match until a valid pattern is installed.
   *   For DiagsTagType_Debug, if this is the process-global Diags instance,
   *   all registered debug controls are updated immediately to reflect the
   *   new pattern.
   * @par Errors
   * Invalid regex is silently accepted; compile failure is not
   *   signaled. The previous pattern is NOT retained on compile failure.
   * @par Thread safety
   * Acquires tag_table_lock. Safe to call concurrently with
   *   emission, but serialized with other reconfiguration methods.
   */
  void activate_taglist(const char *taglist, DiagsTagType mode = DiagsTagType_Debug);

  /**
   * @brief Disable all tags for the given mode.
   *
   * @param[in] mode DiagsTagType_Debug or DiagsTagType_Action.
   * @pre None.
   * @post No tag matches for mode; activated_tags[mode] is null.
   * @par Errors
   * None.
   * @par Thread safety
   * Acquires tag_table_lock. Safe to call concurrently with
   *   emission, but serialized with other reconfiguration methods.
   */
  void deactivate_all(DiagsTagType mode = DiagsTagType_Debug);

  /**
   * @brief Open the given BaseLogFile for use as a diagnostic log destination.
   *
   * Does NOT assign diags_log; the caller is responsible for that assignment
   * on success. On failure, blf is deleted before returning.
   *
   * @param[in] blf Pointer to a constructed but not-yet-opened BaseLogFile,
   *   or nullptr (null is a no-op that returns true). When non-null, ownership
   *   transfers unconditionally: on success the caller must assign blf to
   *   diags_log; on failure blf has been deleted.
   * @return True if blf was opened successfully (or blf is nullptr); false if
   *   the open failed (blf is deleted before returning false).
   * @pre blf is null or points to a BaseLogFile that has not yet been opened.
   * @post On true return: blf (if non-null) is open; caller must assign it to
   *   diags_log. On false return: blf has been deleted; diags_log is unchanged.
   * @par Errors
   * Open failures cause a false return. A log_log_error diagnostic at
   *   LL_Error is emitted only when BASELOGFILE_DEBUG_MODE is enabled
   *   (off by default).
   * @par Thread safety
   * Caller MUST serialize with all other reconfiguration
   *   methods. reseat_diagslog() acquires tag_table_lock only for the
   *   pointer swap that follows this call; setup_diagslog() itself is not
   *   called under the lock. Direct callers must provide equivalent
   *   serialization for the pointer swap.
   */
  bool setup_diagslog(BaseLogFile *blf);

  /**
   * @brief Configure the rolling policy for diags.log.
   *
   * @param[in] re Rolling policy (see RollingEnabledValues).
   * @param[in] ri Rolling interval in seconds (used by time-based policies).
   * @param[in] rs Rolling size threshold in bytes (used by size-based
   *   policies).
   * @pre None.
   * @post Rolling policy fields are updated in the calling thread.
   * @par Errors
   * None.
   * @par Thread safety
   * No lock is acquired. The caller must ensure no concurrent
   *   calls to should_roll_diagslog() occur during reconfiguration.
   */
  void config_roll_diagslog(RollingEnabledValues re, int ri, int rs);

  /**
   * @brief Configure the rolling policy for the output log (traffic.out).
   *
   * @param[in] re Rolling policy.
   * @param[in] ri Rolling interval in seconds.
   * @param[in] rs Rolling size threshold in bytes.
   * @pre None.
   * @post Rolling policy fields are updated in the calling thread.
   * @par Errors
   * None.
   * @par Thread safety
   * No lock is acquired. The caller must ensure no concurrent
   *   calls to should_roll_outputlog() occur during reconfiguration.
   */
  void config_roll_outputlog(RollingEnabledValues re, int ri, int rs);

  /**
   * @brief Close the current diags.log and reopen it at the configured path.
   *
   * Intended for use after an external log rotation tool has renamed or
   * removed the active diags.log. The implementation:
   *  1. Flushes the current file's buffered output.
   *  2. Captures the configured filename from the active BaseLogFile.
   *  3. Constructs a new BaseLogFile at that filename. The filename is
   *     passed to the OS verbatim — symlinks are re-resolved at each call.
   *  4. On success: atomically swaps the new file into place under
   *     tag_table_lock and destroys the previous BaseLogFile (closing its
   *     FILE *).
   *  5. On failure: leaves the previous file in place.
   *
   * @return False if diags_log is null or not yet initialized (safe no-op).
   *   True in all other cases, including when the internal reopen fails —
   *   reopen failures are not reflected in the return value and are
   *   observable only via log_log_trace, which is compiled out unless
   *   BASELOGFILE_DEBUG_MODE is enabled (off by default).
   * @pre A Diags instance is active and diags_log is initialized
   *   (is_init() == true). If this precondition is not met, returns false
   *   without performing a reopen — this is the safe no-op path for calls
   *   that arrive before the diagnostics subsystem is initialized.
   * @post On success: diags_log is a fresh BaseLogFile at the configured
   *   path; the previous BaseLogFile (and its FILE *) is destroyed; all
   *   subsequent emission goes to the new file.
   *   On failure: diags_log is unchanged; the newly allocated BaseLogFile
   *   is deleted before it returns.
   * @par Errors
   * Open failures are not signaled via the return value. They are reported
   *   via log_log_trace only when BASELOGFILE_DEBUG_MODE is enabled (off
   *   by default); in normal builds the failure is completely silent.
   * @par Thread safety
   * The swap in step 4 is performed under tag_table_lock.
   *   Concurrent emission either observes the pre-swap or post-swap log;
   *   no message is lost or written to a destroyed FILE *.
   * @note When diagnostic output is disabled or redirected to a non-file
   *   sink (e.g., syslog), diags_log is null or uninitialized and the
   *   initialization guard causes an early false return.
   */
  bool reseat_diagslog();

  /**
   * @brief Roll diags.log if the current rolling policy condition is met.
   *
   * @return True if the underlying file was renamed (rolled); false if no
   *   roll condition was triggered, or if fstat failed. Note: true is
   *   returned even when the subsequent reopen of the new log file fails.
   * @pre None.
   * @post If the rolling condition is met: the current diags.log is flushed,
   *   rolled (renamed), and replaced by a new BaseLogFile at the same path.
   *   If not: no state change. fstat failure causes an early false return
   *   without rolling.
   * @par Errors
   * None signaled. fstat and reopen failures silently suppress the
   *   replacement; the rolled state of the original file is not reversed.
   * @par Thread safety
   * tag_table_lock is acquired only during the BaseLogFile
   *   pointer swap. The rolling-condition checks and file operations execute
   *   without a lock; the caller must ensure no concurrent reconfiguration
   *   (see config_roll_diagslog()).
   */
  bool should_roll_diagslog();

  /**
   * @brief Roll stdout_log and stderr_log if the current rolling policy
   *   condition is met.
   *
   * @return True if any output log was rolled; false otherwise.
   * @pre stdout_log and stderr_log are non-null.
   * @post If the rolling condition is met: affected logs are flushed, rolled,
   *   and replaced by new BaseLogFile instances at the same paths.
   *   If not: no state change. fstat failure causes an early false return.
   * @par Errors
   * None signaled. fstat failures silently suppress the roll.
   * @par Thread safety
   * Same as should_roll_diagslog(); see config_roll_outputlog().
   */
  bool should_roll_outputlog();

  /**
   * @brief Reseat the named standard stream to a file at the given path.
   *
   * @param[in] stream STDOUT or STDERR (see StdStream).
   * @param[in] file Non-null filesystem path. Symlinks are re-resolved at
   *   each call. An empty string causes an immediate false return without
   *   modifying any state.
   * @return True on success; false if file is empty, the file could not be
   *   opened, or the resulting FILE * is null.
   * @pre file must not be null.
   * @post On success: the new file is open and bound as the named stream;
   *   the previous BaseLogFile is deleted.
   *   On file-open failure: the stream pointer is set to nullptr and the
   *   previous BaseLogFile is not freed.
   *   On empty-string return: no state is modified.
   * @par Errors
   * File-open failures cause a false return; the named stream is left
   *   unbound (nullptr). Internal log_log_error messages are emitted only
   *   when BASELOGFILE_DEBUG_MODE is enabled (off by default).
   * @par Thread safety
   * Acquires tag_table_lock for the pointer update on success and on
   *   file-open failure. The empty-string early return does not acquire
   *   the lock.
   */
  bool set_std_output(StdStream stream, const char *file);

  /// Initial debug tag regex string; copied at construction. May be nullptr.
  const char *base_debug_tags;

  /// Initial action tag regex string; copied at construction. May be nullptr.
  const char *base_action_tags;

  /// Optional IP address for per-connection debug override. When set,
  /// connections whose remote IP matches this address have
  /// ContFlags::DEBUG_OVERRIDE set, which causes on(mode) to return true
  /// only when the global debug state is in DEBUG_OVERRIDE mode (value 2).
  IpAddr debug_client_ip;

private:
  const std::string      prefix_str;
  mutable ink_mutex      tag_table_lock;    // prevents reconfig/read races
  std::shared_ptr<Regex> activated_tags[2]; // 1 table for debug, 1 for action

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
