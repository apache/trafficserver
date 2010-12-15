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


#if !defined (INK_NO_LOG)
#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include "libts.h"
#include "LogFormatType.h"
#include "LogLimits.h"

#include "LogBufferV1.h"
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

struct LogEntryHeader
{
//    unsigned timestamp;
  long timestamp;               // the seconds portion of the timestamp
  long timestamp_usec;          // the microseconds portion of the timestamp
  unsigned entry_len;
};

/*-------------------------------------------------------------------------
  LogBufferHeader

  This struct is automatically laid down at the head of each buffer.
  -------------------------------------------------------------------------*/

struct LogBufferHeader
{

  unsigned cookie;              // so we can find it on disk
  unsigned version;             // in case we want to change it later
  unsigned format_type;         // SQUID_LOG, COMMON_LOG, ...
  unsigned byte_count;          // acutal # of bytes for the segment
  unsigned entry_count;         // actual number of entries stored
  unsigned low_timestamp;       // lowest timestamp value of entries
  unsigned high_timestamp;      // highest timestamp value of entries
  unsigned int log_object_flags;        // log object flags
  uint64_t log_object_signature;  // log object signature
#if defined(LOG_BUFFER_TRACKING)
  unsigned int id;
#endif                          // defined(LOG_BUFFER_TRACKING)

  // all offsets are computed from the start of the buffer (ie, "this"),
  // and so any valid offset will be at least sizeof(LogBufferHeader).

  unsigned fmt_name_offset;     // offset to format name string
  unsigned fmt_fieldlist_offset;        // offset to format fieldlist string
  unsigned fmt_printf_offset;   // offset to format printf string
  unsigned src_hostname_offset; // offset to source (client) hostname
  unsigned log_filename_offset; // offset to log filename
  unsigned data_offset;         // offset to start of data entry
  // section

  // some helper functions to return the header strings

  char *fmt_name();             // not used
  char *fmt_fieldlist();
  char *fmt_printf();
  char *src_hostname();
  char *log_filename();
};


union LB_State
{
  LB_State():ival(0)
  {
  };

  LB_State(volatile LB_State & vs)
  {
    ival = vs.ival;
  };

  LB_State & operator =(volatile LB_State & vs)
  {
    ival = vs.ival;
    return *this;
  }

  uint64_t ival;
  struct
  {
    uint16_t offset;              // buffer should be <= 64KB
    uint16_t num_entries;         // number of entries in buffer
    uint16_t byte_count;          // bytes in buffer
    uint16_t full:1;              // not accepting more checkouts
    uint16_t num_writers:15;      // number of writers
  } s;
};


/* ---------------------------------- iObject ------------------------------ */
class iObjectActivator;
class iObject
{
private:
  static iObject *free_heap;    /* list of free blocks */
  static ink_mutex iObjectMutex;        /* mutex for access to iObject class global variables */

  size_t class_size;            /* real class size */
  iObject *next_object;


protected:
  iObject(const iObject &);   /* declared; not implemented - block copying and assignment */
  iObject & operator=(const iObject &);       /* ditto */

public:
  static void Init(void);
  void *operator  new(size_t size);
  void operator  delete(void *p);

 iObject()
 {                             /* nop */
 }

 virtual ~iObject()
 {                             /* nop */
 }

 friend class iObjectActivator;
};

/* ------------------------------ iLogBufferBuffer ------------------------- */
class iLogBufferBuffer
{
private:
  static iLogBufferBuffer *free_heap;   /* list of free blocks */
  static ink_mutex iLogBufferBufferMutex;       /* mutex for access iLogBufferBuffer class global variables */

  iLogBufferBuffer *next;
  size_t real_buf_size;


  iLogBufferBuffer()
  {
    next = 0;
    buf = 0;
    real_buf_size = (size = 0);
  }

  ~iLogBufferBuffer()
  {
    if (buf)
      xfree(buf);
    real_buf_size = (size = 0);
  }

protected:
  iLogBufferBuffer(const iLogBufferBuffer &);   /* declared; not implemented - block copying and assignment */
  iLogBufferBuffer & operator=(const iLogBufferBuffer &);       /* ditto */

public:
  char *buf;
  size_t size;

  static void Init(void);
  static iLogBufferBuffer *New_iLogBufferBuffer(size_t _buf_size);
  static iLogBufferBuffer *Delete_iLogBufferBuffer(iLogBufferBuffer * _b);

  friend class iObjectActivator;
};

/* ---------------------------- iObjectActivator --------------------------- */
class iObjectActivator
{
public:
  iObjectActivator()
  {
    iObject::Init();
    iLogBufferBuffer::Init();
  }

  ~iObjectActivator()
  {                             /* nop */
  }
};

/*-------------------------------------------------------------------------
  LogBuffer
  -------------------------------------------------------------------------*/
#define CLASS_SIGN_LOGBUFFER 0xFACE5370 /* LogBuffer class signature */

class LogBuffer:public iObject
{
public:
  unsigned long sign;           /* class signature (must be CLASS_SIGN_LOGBUFFER) */
  LogBuffer *next_flush;        /* next in flush list */
  LogBuffer *next_list;         /* next in list */

  enum LB_ResultCode
  {
    LB_OK = 0,
    LB_FULL_NO_WRITERS,
    LB_FULL_ACTIVE_WRITERS,
    LB_RETRY,
    LB_ALL_WRITERS_DONE,
    LB_BUSY,
    LB_BUFFER_TOO_SMALL
  };

    LogBuffer(LogObject * owner, size_t size,
              size_t buf_align = LB_DEFAULT_ALIGN, size_t write_align = INK_MIN_ALIGN);
    LogBuffer(LogObject * owner, LogBufferHeader * header);
   ~LogBuffer();
  char &operator [] (int idx)
  {
    ink_debug_assert(idx >= 0);
    ink_debug_assert((size_t) idx < m_size);
    return m_buffer[idx];
  };

  int switch_state(LB_State & old_state, LB_State & new_state)
  {
    INK_WRITE_MEMORY_BARRIER;
    return (ink_atomic_cas64((int64_t *) & m_state.ival, old_state.ival, new_state.ival));
  };

  LB_ResultCode checkout_write(size_t * write_offset, size_t write_size);
  LB_ResultCode checkin_write(size_t write_offset);
  void force_full();

  LogBufferHeader *header()
  {
    return m_header;
  }
  long expiration_time()
  {
    return m_expiration_time;
  }

  // this should only be called when buffer is ready to be flushed
  void update_header_data();
  void convert_to_network_order();
  void convert_to_host_order();
  uint32_t get_id()
  {
    return m_id;
  };
  LogObject *get_owner() const
  {
    return m_owner;
  };

  Link<LogBuffer> link;

  // static variables
  static vint32 M_ID;

  // static functions
  static size_t max_entry_bytes();
  static int to_ascii(LogEntryHeader * entry, LogFormatType type,
                      char *buf, int max_len, char *symbol_str, char *printf_str,
                      unsigned buffer_version, char *alt_format = NULL);
  static int resolve_custom_entry(LogFieldList * fieldlist,
                                  char *printf_str, char *read_from, char *write_to,
                                  int write_to_len, long timestamp, long timestamp_us,
                                  unsigned buffer_version, LogFieldList * alt_fieldlist = NULL,
                                  char *alt_printf_str = NULL);
  static void convert_to_network_order(LogBufferHeader * header);
  static void convert_to_host_order(LogBufferHeader * header);

private:
  iLogBufferBuffer * m_bb;      // real buffer
  char *m_new_buffer;           // new buffer (must be free)
  char *m_unaligned_buffer;     // the unaligned buffer
  char *m_buffer;               // the buffer
  size_t m_size;                // the buffer size
  size_t m_buf_align;           // the buffer alignment
  size_t m_write_align;         // the write alignment mask

  volatile LB_State m_state;    // buffer state

  int m_max_entries;            // max number of entries allowed
  long m_expiration_time;       // buffer expiration time

  LogObject *m_owner;           // the LogObject that owns this buf.
  LogBufferHeader *m_header;

  uint32_t m_id;                  // unique buffer id (for debugging)

  // private functions
  size_t _add_buffer_header();
  unsigned add_header_str(char *str, char *buf_ptr, unsigned buf_len);

  // -- member functions that are not allowed --
  LogBuffer();
  LogBuffer(const LogBuffer & rhs);
  LogBuffer & operator=(const LogBuffer & rhs);

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
  LogBuffer * m_list;
  LogBuffer *m_list_last_ptr;
  ink_mutex m_mutex;
  int m_size;

public:
    LogBufferList();
   ~LogBufferList();

  void add(LogBuffer * lb);
  LogBuffer *get(void);
  int get_size(void)
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
  LogBufferIterator(LogBufferHeader * header, bool in_network_order = false);
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
    LogBufferIterator & operator=(const LogBufferIterator &);
};


/*-------------------------------------------------------------------------
  LogBufferIterator

  This class provides the ability to iterate over the LogEntries stored
  within a given LogBuffer.
  -------------------------------------------------------------------------*/

inline
LogBufferIterator::LogBufferIterator(LogBufferHeader * header, bool in_network_order)
                  : m_in_network_order(in_network_order),
                  m_next(0),
                  m_iter_entry_count(0),
                  m_buffer_entry_count(0)
{
  ink_debug_assert(header);

  switch (header->version) {
  case LOG_SEGMENT_VERSION:
    m_next = (char *) header + header->data_offset;
    m_buffer_entry_count = header->entry_count;
    break;

  case 1:
    m_next = (char *) header + ((LogBufferHeaderV1 *) header)->data_offset;
    m_buffer_entry_count = ((LogBufferHeaderV1 *) header)->entry_count;
    break;

  default:
    Note("Invalid LogBuffer version %d in LogBufferIterator; "
         "current version is %d", header->version, LOG_SEGMENT_VERSION);
    break;
  }
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

inline
LogBufferIterator::~
LogBufferIterator()
{
}


#endif
#endif //INK_NO_LOG
