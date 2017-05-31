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

#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include "ts/ink_platform.h"
#include "ts/Diags.h"
#include "LogFormat.h"
#include "LogLimits.h"
#include "LogAccess.h"

class LogObject;
class LogBufferIterator;

#define LOG_SEGMENT_COOKIE 0xaceface
#define LOG_SEGMENT_VERSION 2

#if defined(linux)
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
  int64_t timestamp;      // the seconds portion of the timestamp
  int32_t timestamp_usec; // the microseconds portion of the timestamp
  uint32_t entry_len;
};

/*-------------------------------------------------------------------------
  LogBufferHeader

  This struct is automatically laid down at the head of each buffer.
  -------------------------------------------------------------------------*/

struct LogBufferHeader {
  uint32_t cookie;               // so we can find it on disk
  uint32_t version;              // in case we want to change it later
  uint32_t format_type;          // SQUID_LOG, COMMON_LOG, ...
  uint32_t byte_count;           // acutal # of bytes for the segment
  uint32_t entry_count;          // actual number of entries stored
  uint32_t low_timestamp;        // lowest timestamp value of entries
  uint32_t high_timestamp;       // highest timestamp value of entries
  uint32_t log_object_flags;     // log object flags
  uint64_t log_object_signature; // log object signature
#if defined(LOG_BUFFER_TRACKING)
  uint32_t int id;
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

  // some helper functions to return the header strings

  char *fmt_name(); // not used
  char *fmt_fieldlist();
  char *fmt_printf();
  char *src_hostname();
  char *log_filename();
};

union LB_State {
  LB_State() : ival(0) {}
  LB_State(volatile LB_State &vs) { ival = vs.ival; }
  LB_State &
  operator=(volatile LB_State &vs)
  {
    ival = vs.ival;
    return *this;
  }

  int64_t ival; // ival is used to help do an atomic CAS for struct s
  struct {
    uint32_t offset;           // buffer offset(bytes in buffer)
    uint16_t num_entries;      // number of entries in buffer
    uint16_t full : 1;         // not accepting more checkouts
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

  LogBuffer(LogObject *owner, size_t size, size_t buf_align = LB_DEFAULT_ALIGN, size_t write_align = INK_MIN_ALIGN);
  LogBuffer(LogObject *owner, LogBufferHeader *header);
  ~LogBuffer();

  char &operator[](int idx)
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

  LB_ResultCode checkout_write(size_t *write_offset, size_t write_size);
  LB_ResultCode checkin_write(size_t write_offset);
  void force_full();

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
  static vint32 M_ID;

  // static functions
  static size_t max_entry_bytes();
  static int to_ascii(LogEntryHeader *entry, LogFormatType type, char *buf, int max_len, const char *symbol_str, char *printf_str,
                      unsigned buffer_version, const char *alt_format = nullptr);
  static int resolve_custom_entry(LogFieldList *fieldlist, char *printf_str, char *read_from, char *write_to, int write_to_len,
                                  long timestamp, long timestamp_us, unsigned buffer_version, LogFieldList *alt_fieldlist = nullptr,
                                  char *alt_printf_str = nullptr);

  static void
  destroy(LogBuffer *lb)
  {
    // ink_atomic_increment() returns the previous value, so when it was 1, we are
    // the thread that decremented to zero and should delete ...
    int refcnt = ink_atomic_increment(&lb->m_references, -1);

    if (refcnt == 1) {
      delete lb;
    }

    ink_release_assert(refcnt >= 0);
  }

private:
  char *m_unaligned_buffer;         // the unaligned buffer
  char *m_buffer;                   // the buffer
  size_t m_size;                    // the buffer size
  size_t m_buf_align;               // the buffer alignment
  size_t m_write_align;             // the write alignment mask
  int m_buffer_fast_allocator_size; // indicates whether the logbuffer is allocated from ioBuf

  long m_expiration_time; // buffer expiration time

  LogObject *m_owner; // the LogObject that owns this buf.
  LogBufferHeader *m_header;

  uint32_t m_id; // unique buffer id (for debugging)
public:
  volatile LB_State m_state; // buffer state
  volatile int m_references; // oustanding checkout_write references.
private:
  // private functions
  size_t _add_buffer_header();
  unsigned add_header_str(const char *str, char *buf_ptr, unsigned buf_len);
  void freeLogBuffer();

  // -- member functions that are not allowed --
  LogBuffer();
  LogBuffer(const LogBuffer &rhs);
  LogBuffer &operator=(const LogBuffer &rhs);

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
  ink_mutex m_mutex;
  int m_size;

public:
  LogBufferList();
  ~LogBufferList();

  void add(LogBuffer *lb);
  LogBuffer *get(void);
  int
  get_size(void)
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
  LogBufferIterator(LogBufferHeader *header, bool in_network_order = false);
  ~LogBufferIterator();

  LogEntryHeader *next();

private:
  bool m_in_network_order;
  char *m_next;
  unsigned m_iter_entry_count;
  unsigned m_buffer_entry_count;

  // -- member functions not allowed --
  LogBufferIterator();
  LogBufferIterator(const LogBufferIterator &);
  LogBufferIterator &operator=(const LogBufferIterator &);
};

/*-------------------------------------------------------------------------
  LogBufferIterator

  This class provides the ability to iterate over the LogEntries stored
  within a given LogBuffer.
  -------------------------------------------------------------------------*/

inline LogBufferIterator::LogBufferIterator(LogBufferHeader *header, bool in_network_order)
  : m_in_network_order(in_network_order), m_next(0), m_iter_entry_count(0), m_buffer_entry_count(0)
{
  ink_assert(header);

  switch (header->version) {
  case LOG_SEGMENT_VERSION:
    m_next               = (char *)header + header->data_offset;
    m_buffer_entry_count = header->entry_count;
    break;

  default:
    Note("Invalid LogBuffer version %d in LogBufferIterator; "
         "current version is %d",
         header->version, LOG_SEGMENT_VERSION);
    break;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline LogBufferIterator::~LogBufferIterator()
{
}
#endif
