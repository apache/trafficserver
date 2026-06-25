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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_atomic.h"
#include "tscore/Diags.h"
#include "proxy/logging/LogFormat.h"
#include "proxy/logging/LogLimits.h"
#include "proxy/logging/LogAccess.h"

#include <cstddef>

class LogObject;
class LogConfig;
class LogBufferIterator;

#define LOG_SEGMENT_COOKIE 0xaceface

#define LOG_SEGMENT_VERSION               3 ///< Current default version.
#define LOG_SEGMENT_VERSION_MIN_SUPPORTED 2 ///< Oldest version this build can still read.
#define LOG_SEGMENT_VERSION_FIELDTYPES    3 ///< First version that carries the field-type schema (self-describing).

#if defined(__linux__)
#define LB_DEFAULT_ALIGN 512
#else
#define LB_DEFAULT_ALIGN 8
#endif

/*-------------------------------------------------------------------------
  LogEntryHeader

  This struct is automatically laid down at the head of each entry in the
  buffer.
  -------------------------------------------------------------------------*/

struct LogEntryHeader {
  int64_t  timestamp;      // the seconds portion of the timestamp
  int32_t  timestamp_usec; // the microseconds portion of the timestamp
  uint32_t entry_len;
};

/*-------------------------------------------------------------------------
  LogFieldTypeSchema

  Self-describing field-type table for v3 segments, written once per segment at
  LogBufferHeader::fmt_fieldtypes_offset (alongside fmt_fieldlist). It lets a
  generic reader decode every field from the file alone, dispatching on the
  LogField::Type codes with no embedded ATS symbol->type table.

  On-wire layout:

    uint16_t field_count;             // == number of symbols in fmt_fieldlist
    uint8_t  type_code[field_count];  // LogField::Type, in fieldlist order

  No independent schema version: the segment's LOG_SEGMENT_VERSION governs this
  layout. All integers are host byte order, like the rest of LogBufferHeader;
  the blob is padded with the header to 8-byte alignment.
  -------------------------------------------------------------------------*/

struct LogFieldTypeSchema {
  uint16_t field_count;
  // Immediately followed by uint8_t type_code[field_count].

  const uint8_t *
  type_codes() const
  {
    return reinterpret_cast<const uint8_t *>(this) + sizeof(LogFieldTypeSchema);
  }
};

/*-------------------------------------------------------------------------
  LogBufferHeader

  This struct is automatically laid down at the head of each buffer.
  -------------------------------------------------------------------------*/

struct LogBufferHeader {
  uint32_t cookie;               // so we can find it on disk
  uint32_t version;              // in case we want to change it later
  uint32_t format_type;          // SQUID_LOG, COMMON_LOG, ...
  uint32_t byte_count;           // actual # of bytes for the segment
  uint32_t entry_count;          // actual number of entries stored
  uint32_t low_timestamp;        // lowest timestamp value of entries
  uint32_t high_timestamp;       // highest timestamp value of entries
  uint32_t log_object_flags;     // log object flags
  uint64_t log_object_signature; // log object signature
#if defined(LOG_BUFFER_TRACKING)
  uint32_t id;
#endif // defined(LOG_BUFFER_TRACKING)

  // all offsets are computed from the start of the buffer (ie, "this"),
  // and so any valid offset will be at least sizeof(LogBufferHeader).

  uint32_t fmt_name_offset;      // offset to format name string
  uint32_t fmt_fieldlist_offset; // offset to format fieldlist string
  uint32_t fmt_printf_offset;    // offset to format printf string
  uint32_t src_hostname_offset;  // offset to source (client) hostname
  uint32_t log_filename_offset;  // offset to log filename
  uint32_t data_offset;          // offset to start of data entry
  // section

  // NEW in v3: offset to the LogFieldTypeSchema blob, or 0 if absent. Appended
  // after data_offset so the layout through data_offset is byte-identical to
  // v2; v2 readers ignore it and v3 readers tolerate v2 segments lacking it.
  uint32_t fmt_fieldtypes_offset;

  // some helper functions to return the header strings

  char *fmt_fieldlist();
  char *fmt_printf();
  char *fmt_fieldtypes(); // v3 field-type schema blob; nullptr for v2 segments
  char *src_hostname();
  char *log_filename();
};

/** Whether this build can read a segment of the given @a version.

    The single source of truth for the supported range: readers accept the
    inclusive range [LOG_SEGMENT_VERSION_MIN_SUPPORTED, LOG_SEGMENT_VERSION] so
    a new build keeps decoding logs written by an older one.
*/
inline bool
log_segment_version_supported(unsigned version)
{
  return LOG_SEGMENT_VERSION_MIN_SUPPORTED <= version && version <= LOG_SEGMENT_VERSION;
}

/** On-disk size of LogBufferHeader for a given segment version.

    Raw readers that read the header by size (rather than via the data_offset
    field) must size the read to the version on disk: v3 appended
    fmt_fieldtypes_offset after data_offset, so a v2 segment's header is
    shorter. Reading a v2 segment with the (larger) v3 struct size would
    consume bytes belonging to the data section.

    @return the header size in bytes, or 0 if @a version is unsupported.
*/
inline size_t
log_buffer_header_size(unsigned version)
{
  if (!log_segment_version_supported(version)) {
    return 0;
  }
  // v2 stops at data_offset; v3 and later include fmt_fieldtypes_offset.
  return version >= LOG_SEGMENT_VERSION_FIELDTYPES ? sizeof(LogBufferHeader) : offsetof(LogBufferHeader, fmt_fieldtypes_offset);
}

union LB_State {
  LB_State() : ival(0) {}
  LB_State(LB_State &vs) { ival = vs.ival; }
  LB_State &
  operator=(LB_State &vs)
  {
    ival = vs.ival;
    return *this;
  }

  int64_t ival; // ival is used to help do an atomic CAS for struct s
  struct {
    uint32_t offset;           // buffer offset(bytes in buffer)
    uint16_t num_entries;      // number of entries in buffer
    uint16_t full        : 1;  // not accepting more checkouts
    uint16_t num_writers : 15; // number of writers
  } s;
};

/*-------------------------------------------------------------------------
  LogBuffer
  -------------------------------------------------------------------------*/
class LogBuffer
{
public:
  SLINK(LogBuffer, write_link);
  enum LB_ResultCode {
    LB_OK = 0,
    LB_FULL_NO_WRITERS,
    LB_FULL_ACTIVE_WRITERS,
    LB_RETRY,
    LB_ALL_WRITERS_DONE,
    LB_BUSY,
    LB_BUFFER_TOO_SMALL
  };

  LogBuffer(const LogConfig *cfg, LogObject *owner, size_t size, size_t buf_align = LB_DEFAULT_ALIGN,
            size_t write_align = INK_MIN_ALIGN);
  LogBuffer(LogObject *owner, LogBufferHeader *header);
  ~LogBuffer();

  char &
  operator[](int idx)
  {
    ink_assert(idx >= 0);
    ink_assert((size_t)idx < m_size);
    return m_buffer[idx];
  }

  int
  switch_state(LB_State &old_state, LB_State &new_state)
  {
    INK_WRITE_MEMORY_BARRIER;
    return (ink_atomic_cas(&m_state.ival, old_state.ival, new_state.ival));
  }

  // 'fast' mode only, not thread-safe
  LB_ResultCode fast_write(size_t *write_offset, size_t write_size);
  LB_ResultCode checkout_write(size_t *write_offset, size_t write_size);
  LB_ResultCode checkin_write(size_t write_offset);
  void          force_full();

  LogBufferHeader *
  header() const
  {
    return m_header;
  }

  long
  expiration_time() const
  {
    return m_expiration_time;
  }

  // this should only be called when buffer is ready to be flushed
  void update_header_data();

  uint32_t
  get_id() const
  {
    return m_id;
  }

  LogObject *
  get_owner() const
  {
    return m_owner;
  }

  LINK(LogBuffer, link);

  // static variables
  static int32_t M_ID;

  // static functions
  static size_t max_entry_bytes();
  static int to_ascii(LogEntryHeader *entry, LogFormatType type, char *buf, int max_len, const char *symbol_str, char *printf_str,
                      unsigned buffer_version, const char *alt_format = nullptr, LogEscapeType escape_type = LOG_ESCAPE_NONE);

  static int resolve_custom_entry(LogFieldList *fieldlist, char *printf_str, char *read_from, char *write_to, int write_to_len,
                                  long timestamp, long timestamp_us, unsigned buffer_version, LogFieldList *alt_fieldlist = nullptr,
                                  char *alt_printf_str = nullptr, LogEscapeType escape_type = LOG_ESCAPE_NONE);

  static void
  destroy(LogBuffer *&lb)
  {
    // ink_atomic_increment() returns the previous value, so when it was 1, we are
    // the thread that decremented to zero and should delete ...
    int refcnt = ink_atomic_increment(&lb->m_references, -1);

    if (refcnt == 1) {
      delete lb;
      lb = nullptr;
    }

    ink_release_assert(refcnt >= 0);
  }

private:
  char  *m_unaligned_buffer;           // the unaligned buffer
  char  *m_buffer;                     // the buffer
  size_t m_size;                       // the buffer size
  size_t m_buf_align;                  // the buffer alignment
  size_t m_write_align;                // the write alignment mask
  int    m_buffer_fast_allocator_size; // indicates whether the logbuffer is allocated from ioBuf

  long m_expiration_time; // buffer expiration time

  LogObject       *m_owner; // the LogObject that owns this buf.
  LogBufferHeader *m_header;

  uint32_t m_id; // unique buffer id (for debugging)
public:
  LB_State m_state;      // buffer state
  int      m_references; // outstanding checkout_write references.

  // noncopyable
  // -- member functions that are not allowed --
  LogBuffer(const LogBuffer &rhs)            = delete;
  LogBuffer &operator=(const LogBuffer &rhs) = delete;

private:
  // private functions
  size_t   _add_buffer_header(const LogConfig *cfg);
  unsigned add_header_str(const char *str, char *buf_ptr, unsigned buf_len);
  unsigned add_field_type_schema(const LogFieldList *fieldlist, char *buf_ptr, unsigned buf_len);
  void     freeLogBuffer();

  // -- member functions that are not allowed --
  LogBuffer();

  friend class LogBufferIterator;
};

class LogFile;

/*-------------------------------------------------------------------------
  LogBufferList

  Support atomic operations on a list of LogBuffer objects.
  -------------------------------------------------------------------------*/

class LogBufferList
{
private:
  Queue<LogBuffer> m_buffer_list;
  ink_mutex        m_mutex;
  int              m_size;

public:
  LogBufferList();
  ~LogBufferList();

  void       add(LogBuffer *lb);
  LogBuffer *get();
  int
  get_size()
  {
    return m_size;
  }
};

/*-------------------------------------------------------------------------
  LogBufferIterator

  This class will iterate over the entries in a LogBuffer.
  -------------------------------------------------------------------------*/

class LogBufferIterator
{
public:
  LogBufferIterator(LogBufferHeader *header);
  ~LogBufferIterator();

  LogEntryHeader *next();

  // noncopyable
  // -- member functions not allowed --
  LogBufferIterator(const LogBufferIterator &)            = delete;
  LogBufferIterator &operator=(const LogBufferIterator &) = delete;

private:
  char    *m_next;
  char    *m_buffer_end; // one past the last readable byte of the segment
  unsigned m_iter_entry_count;
  unsigned m_buffer_entry_count;

  // -- member functions not allowed --
  LogBufferIterator();
};

/*-------------------------------------------------------------------------
  LogBufferIterator

  This class provides the ability to iterate over the LogEntries stored
  within a given LogBuffer.
  -------------------------------------------------------------------------*/

inline LogBufferIterator::LogBufferIterator(LogBufferHeader *header)
  : m_next(nullptr), m_buffer_end(nullptr), m_iter_entry_count(0), m_buffer_entry_count(0)
{
  ink_assert(header);

  // Entry iteration is identical across v2/v3 (v3 only appended a header field
  // after data_offset). Accept the whole supported range.
  if (log_segment_version_supported(header->version)) {
    // Bound the data section against byte_count so a corrupt/hostile data_offset
    // (a .blog read by logcat/logstats may be untrusted) can't make next()
    // dereference outside the buffer. data_offset sits past the header, and must
    // be 8-byte aligned so the int64 reads in each entry are well-aligned.
    size_t header_size = log_buffer_header_size(header->version);
    if (header->data_offset >= header_size && header->data_offset <= header->byte_count &&
        header->data_offset % INK_MIN_ALIGN == 0) {
      m_next               = reinterpret_cast<char *>(header) + header->data_offset;
      m_buffer_end         = reinterpret_cast<char *>(header) + header->byte_count;
      m_buffer_entry_count = header->entry_count;
    }
    // else: bad data_offset -- leave m_next null (next() yields nothing). The
    // file readers report it; the iterator stays silent to avoid log spam.
  } else {
    Note("Invalid LogBuffer version %d in LogBufferIterator; "
         "supported versions are %d-%d",
         header->version, LOG_SEGMENT_VERSION_MIN_SUPPORTED, LOG_SEGMENT_VERSION);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline LogBufferIterator::~LogBufferIterator() {}
