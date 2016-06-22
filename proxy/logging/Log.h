/** @file

  Logging

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

  @section design Logging Design

  @verbatim
  o Configuration Variables (and sample values)

      proxy.config.log.format_file              logs/formats
      proxy.config.log.logging_enabled          1
      proxy.config.log.failsafe_logging_enabled 0
      proxy.config.log.sampling_interval        1
      proxy.config.log.buffer_segment_count     16
      proxy.config.log.buffer_segment_size      1MB
      proxy.config.log.max_entries_per_segment  100
      proxy.config.log.online_formats_enabled   0
      proxy.config.log.online_squid_format      0
      proxy.config.log.online_ns_common_format  0
      proxy.config.log.online_ns_ext_format     0
      proxy.config.log.online_ns_ext2_format    0
      proxy.config.log.filtering_enabled        0
      proxy.config.log.local_disk_enabled       2 (only if network fails)
      proxy.config.log.network_enabled          1
      proxy.config.log.network_port             4444
      proxy.config.log.network_timeout_ms       100
      proxy.config.log.lock_timeout_ms          10

  o Memory Layout

      Log data for each transaction is stored in a LogBufferEntry that has
      been allocated from a LogBufferSegment, which in-turn is located
      within a LogBufferPool.  A LogBufferPool looks like:

      +--++--++--+---+--+-----+--+--++--++--+----+--+---++--++...+
      |ph||sh||eh|eee|eh|eeeee|eh|ee||sh||eh|eeee|eh|eee||sh||   |
      +--++--++--+---+--+-----+--+--++--++--+----+--+---++--++...+
       ^   ^   ^  ^
       |   |   |  |
       |   |   |  +-- actual data for a LogBufferEntry
       |   |   +----- a LogBufferEntryHeader describing the following entry
       |   +--------- a LogBufferSegmentHeader describing the entries
       +------------- a LogBufferPoolHeader describing the segments

  o Initial State

      - A LogBufferPool is allocated, with storage equal to
        sizeof (LogBufferPoolHeader) + buffer_segment_count * buffer_segment_size

      - The buffer pool space is divided into buffer_segment_count
        segments, each with a fixed size of buffer_segment_size.

      - All of the segments are placed on the empty_segment list in the
        pool header (H), and the first segment is assigned as the
        current_segment.

  o LogBuffer Allocation

      - If logging is disabled (!proxy.config.log.logging_enabled),
        return Log::SKIP.

      - If proxy.config.log.sampling_interval > 1 but it's not time to
        sample, return Log::SKIP.

      - If proxy.config.log.filtering_enabled, then check host and domain
        in the request_header against the filter file.  If this request does
        not pass the filter, return Log::SKIP.

      - The pool_lock in the pool header is obtained. If it cannot be
        obtained after proxy.config.log.trylock_time_ms milliseconds, then
        a new pool is allocated and the lock is taken on the new pool.
        If a new pool cannot be allocated, then Log::FAIL is returned and
        the client (HttpStateMachineGet) can deal with the failure in the
        best possible way.

      - If there is not enough space in the current segment, the current
        segment is moved to the full_segment list and a segment from the
        empty_segment list is designated as the current_segment.  If there
        are no segments in the empty_segment list, the FAIL_MEMORY status
        is returned.

      - The entry count for the current segment is bumped and the next
        offset value is computed and stored.

      - The pool_lock is released.

      - The c_request_timestamp field is set, ensuring that each of the
        entries within the segment are automatically ordered, and the
        LogBuffer pointer is returned.

  o LogBuffer Completion

      - Since each client thread now has a unique buffer pointer, all
        writes can occur concurrently and without additional locking.

      - The logging data will be extracted from the LogAccessData object
        using the member functions within.  Only those data items that will
        be needed will be taken.  The data is then marshalled into the
        buffer previously allocated.

      - The LogBuffer is composed of two parts: a fixed-size part that
        contains all of the statically-sized fields, and a variable-sized
        buffer that follows, containing all of the space for strings.
        Variable-size fields in the LogBuffer are actually just
        fixed-size offsets into the variable-size region.

        +---------------------+
        |      fixed-size     |
        +---------------------+
        |                     |  contains strings and any custom logging
        :     variable-size   :  fields not in the union set.
        |                     |
        +---------------------+

      - Once the buffer has been completed, it is committed into the
        segment from which it was allocated by incrementing the
        commit_count within the segment header.  This is an ATOMIC update
        so that no locking is required.  If the commit_count is equal to
        the entry_count and the segment is on the full_list, then the
        logging thread will be awoken so that it can flush the segment
        to disk/net.

  o Flushing

        +-+---------------+
        |h|bbb|b|bbbb|bbb |
        +-+---------------+
           |
           |     **********      +---------------+
           +-?-> * format * ---> |abcdefghijklmno| ---> DISK
           |     **********      +---------------+
           |
           +-?-> DISK
           |
           +-?-> NETWORK

      - The logging thread wakes up whenever there is a LogBufferSegment
        ready to be flushed.  This occurs when it is on the full_segment
        list and the commit_count is the same as the entry_count.

      - If proxy.config.log.online_formats_enabled is set, then the segment
        is passed through the formatting module, which creates ASCII buffers
        for the selected pre-defined formats.  All ASCII buffers are then
        flushed to local disk using basic I/O primitives.  The logging
        thead is allowed to block on I/O since logging buffers can be
        continually allocated without the assistance of the logging thread.

      - If local disk logging is enabled, the complete binary segment
        structure is written to disk, and can be processed later with an
        off-line formatting tool.

      - If network logging is enabled, the segment is written to the
        pre-defined network port.  A timeout keeps this from being an
        unbounded operation.

      - Once the segment has been passed through all possible output
        channels, it must be placed back onto the free_list in its pool.
        The LogBufferPool lock is then obtained.

      - The evacuated segment is returned to the empty_segment list.

      - If all of the segments are on the empty_segment list and there is
        at least one other pool around, then this pool will be deleted.
        Otherwsise, the LogBufferPool lock is released.

      - If there are no more segments to be flushed, then the logging
        thread will go back to sleep (waiting on a condition variable).

  o Off-line Processing

      - In the default case, data written to disk will be in binary form,
        in units of a segment.  An off-line processing tool, called logcat,
        can be written to read this data (as stdin) and generate formatted
        log entries (as stdout).  Thus, this tool could be used as a pipe.

  o Log Collation

      - Log collation is managed by LogCollator nodes, which are
        stand-alone processes that receive LogBufferSegments from specified
        clients and merges them into larger, collated LogBufferSegments.
        At this point, the collated segments can be flushed as before.
        To ease the impact of the initial implementation on the manager,
        the collator will likely be implemented as a new thread within the
        proxy process on the node that will do the log collation.  That
        means that this node will have to run the proxy, but we should see
        about being able to disable it from actually participating in the
        proxy cluster.

      - Log collator processes attach to the network port for the client
        nodes they're receiving their logs from.  A single collator can
        attach to any number of client nodes, and is responsible for
        keeping the network pipes clean so that log entries don't back up.

      - Collation is accomplished with a merge-sort of the segments from
        each node, since segments from each node are guaranteed to be
        in-order.  If, due to a timeout, the collator has to continue with
        the sort and later receives an out-of-order buffer, the data will
        be processed and an error will be generated indicating that the
        collated logs are out of order.  A tool will be provided to
        re-order the entries in the event of a sorting error.

  o Custom Log Formats

      - Custom logging formats are possible, where the field selection is
        the "union" set of pre-defined log format fields plus any
        information from the HTTP headers.

      - Log formats are specified using a printf-like format string, where
        the conversion codes (%) are replaced with the appropriate logging
        fields.  Each of the union set fields will have its own conversion
        code (see LogBuffer below), and additional fields from a header
        will have the general form of %{field}header.

      - Formats are specified in the log format file.

  o Filtering

      - Currently, filtering will only be supported based on exact matching
        of the host or domain name of the request.

      - Filtering is specified in the log format file.

  o Log Format File

      - Each line of the log format file consists of four fields separated
        by whitespaces:

        + identifier - used to identify this format/filter
        + filename - the name of the log file
        + (filter) - the filter to apply for this file
        + "format" - the format string for each entry

        Filters have the form (field op value), where "field" can either be
        %shn (server host name) or %sdn (sever domain name); "op" can either
        be == or !=; and "value" can be any valid hostname or domain name,
        such as hoot.example.com or example.com.

      - Sample entries in the log format file:

        1 common () "%chi - %cun [%cqtf] \"%cqtx\" %pss %pcl"
        2 custom (%shn == hoot.example.com) "%cqu"
  @endverbatim

  @section api External API

  @verbatim
  int Log::access (LogAccess *entry);

  The return value is Log::OK if all log objects successfully logged the
  entry. Otherwise, it has the following bits set to indicate what happened

  ret_val & Log::SKIP      - at least one object filtered the entry
                           - transaction logging has been disabled
                           - the logging system is sampling and it is
                             not yet time for a new sample
                           - no log objects have been defined
                           - entry to log is empty

  ret_val & Log::FAIL      an internal limit of the logging system was
                           exceeded preventing entry from being logged

  ret_val & Log::FULL      the logging space has been exhausted

  @endverbatim

  @section example Example usage of the API

  @code
  LogAccessHttpSM entry (this);
  int ret = Log::access (&entry);
  @endcode

*/

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include "ts/ink_platform.h"
#include "ts/EventNotify.h"
#include "ts/ink_hash_table.h"
#include "ts/Regression.h"
#include "P_RecProcess.h"
#include "LogFile.h"
#include "LogBuffer.h"

class LogAccess;
class LogFieldList;
class LogFilterList;
class LogFormatList;
// class LogBufferList; vl: we don't need it here
struct LogBufferHeader;
class LogFile;
class LogBuffer;
class LogFormat;
class LogObject;
class LogConfig;
class TextLogObject;

class LogFlushData
{
public:
  LINK(LogFlushData, link);
  Ptr<LogFile> m_logfile;
  LogBuffer *logbuffer;
  void *m_data;
  int m_len;

  LogFlushData(LogFile *logfile, void *data, int len = -1) : m_logfile(logfile), m_data(data), m_len(len) {}
  ~LogFlushData()
  {
    switch (m_logfile->m_file_format) {
    case LOG_FILE_BINARY:
      logbuffer = (LogBuffer *)m_data;
      LogBuffer::destroy(logbuffer);
      break;
    case LOG_FILE_ASCII:
    case LOG_FILE_PIPE:
      free(m_data);
      break;
    case N_LOGFILE_TYPES:
    default:
      ink_release_assert(!"Unknown file format type!");
    }
  }
};

/**
   This object exists to provide a namespace for the logging system.
   It contains all data types and global variables relevant to the
   logging system.  You can't actually create a Log object, so all
   members are static.
 */
class Log
{
public:
  enum ReturnCodeFlags {
    LOG_OK = 1,
    SKIP   = 2,
    AGGR   = 4,
    FAIL   = 8,
    FULL   = 16,
  };

  enum LoggingMode {
    LOG_MODE_NONE = 0,
    LOG_MODE_ERRORS,       // log *only* errors
    LOG_MODE_TRANSACTIONS, // log *only* transactions
    LOG_MODE_FULL
  };

  enum InitFlags {
    FIELDS_INITIALIZED = 1,
    FULLY_INITIALIZED  = 2,
  };

  enum ConfigFlags {
    NO_REMOTE_MANAGEMENT = 1,
    STANDALONE_COLLATOR  = 2,
    LOGCAT               = 4,
  };

  enum CollationMode {
    NO_COLLATION = 0,
    COLLATION_HOST,
    SEND_STD_FMTS,
    SEND_NON_XML_CUSTOM_FMTS,
    SEND_STD_AND_NON_XML_CUSTOM_FMTS,
    N_COLLATION_MODES
  };

  enum RollingEnabledValues {
    NO_ROLLING = 0,
    ROLL_ON_TIME_ONLY,
    ROLL_ON_SIZE_ONLY,
    ROLL_ON_TIME_OR_SIZE,
    ROLL_ON_TIME_AND_SIZE,
    INVALID_ROLLING_VALUE
  };

  enum {
    MIN_ROLLING_INTERVAL_SEC = 60,   // 5 minute minimum rolling interval
    MAX_ROLLING_INTERVAL_SEC = 86400 // 24 hrs rolling interval max
  };

  // main interface
  static void init(int configFlags = 0);
  static void init_fields();
  inkcoreapi static bool
  transaction_logging_enabled()
  {
    return (logging_mode == LOG_MODE_FULL || logging_mode == LOG_MODE_TRANSACTIONS);
  }

  inkcoreapi static bool
  error_logging_enabled()
  {
    return (logging_mode == LOG_MODE_FULL || logging_mode == LOG_MODE_ERRORS);
  }

  inkcoreapi static int access(LogAccess *lad);
  inkcoreapi static int va_error(const char *format, va_list ap);
  inkcoreapi static int error(const char *format, ...) TS_PRINTFLIKE(1, 2);

  /////////////////////////////////////////////////////////////////////////
  // 'Wire tracing' enabled by source ip or by percentage of connections //
  /////////////////////////////////////////////////////////////////////////
  static void trace_in(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...) TS_PRINTFLIKE(3, 4);
  static void trace_out(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...) TS_PRINTFLIKE(3, 4);
  static void trace_va(bool in, const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, va_list ap);

  // public data members
  inkcoreapi static LogObject *error_log;
  static LogConfig *config;
  static LogFieldList global_field_list;
  //    static LogBufferList global_buffer_full_list; vl: not used
  //    static LogBufferList global_buffer_delete_list; vl: not used
  static InkHashTable *field_symbol_hash;
  static LogFormat *global_scrap_format;
  static LogObject *global_scrap_object;
  static LoggingMode logging_mode;

  // logging thread stuff
  static EventNotify *preproc_notify;
  static void *preproc_thread_main(void *args);
  static EventNotify *flush_notify;
  static InkAtomicList *flush_data_list;
  static void *flush_thread_main(void *args);

  // collation thread stuff
  static EventNotify collate_notify;
  static ink_thread collate_thread;
  static int collation_preproc_threads;
  static int collation_accept_file_descriptor;
  static int collation_port;
  static void *collate_thread_main(void *args);
  static LogObject *match_logobject(LogBufferHeader *header);

  // reconfiguration stuff
  static void change_configuration();
  static int handle_logging_mode_change(const char *name, RecDataT data_type, RecData data, void *cookie);
  static int handle_periodic_tasks_int_change(const char *name, RecDataT data_type, RecData data, void *cookie);

  Log(); // shut up stupid DEC C++ compiler

  friend void RegressionTest_LogObjectManager_Transfer(RegressionTest *, int, int *);

private:
  static void periodic_tasks(long time_now);
  static void create_threads();
  static void init_when_enabled();

  static int init_status;
  static int config_flags;
  static bool logging_mode_changed;
  static uint32_t periodic_tasks_interval;

  // -- member functions that are not allowed --
  Log(const Log &rhs);
  Log &operator=(const Log &rhs);
};

static inline bool
LogRollingEnabledIsValid(int enabled)
{
  return (enabled >= Log::NO_ROLLING || enabled < Log::INVALID_ROLLING_VALUE);
}

#define TraceIn(flag, ...) \
  if (flag)                \
  Log::trace_in(__VA_ARGS__)
#define TraceOut(flag, ...) \
  if (flag)                 \
  Log::trace_out(__VA_ARGS__)

#endif
