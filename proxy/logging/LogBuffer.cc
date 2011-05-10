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

/***************************************************************************
 LogBuffer.cc

 This file implements the LogBuffer class, which is a thread-safe buffer
 for recording log entries.  See the header file LogBuffer.h for more
 information on the structure of a LogBuffer.


 ***************************************************************************/

#include "libts.h"
#include "ink_unused.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Error.h"
#include "P_EventSystem.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogUtils.h"
#include "LogFile.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogAccess.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "LogFormatType.h"
#include "Log.h"


struct FieldListCacheElement
{
  LogFieldList *fieldlist;
  char *symbol_str;
};

enum
{
  FIELDLIST_CACHE_SIZE = 256
};

FieldListCacheElement fieldlist_cache[FIELDLIST_CACHE_SIZE];
int fieldlist_cache_entries = 0;
vint32 LogBuffer::M_ID = 0;

//iObjectActivator  iObjectActivatorInstance;     /* just to do ::Init() before main() */

iObject *iObject::free_heap = 0;       /* list of free blocks */
ink_mutex iObject::iObjectMutex;        /* mutex for access iObject class global variables */

iLogBufferBuffer *iLogBufferBuffer::free_heap = 0;      /* list of free blocks */
ink_mutex iLogBufferBuffer::iLogBufferBufferMutex;      /* mutex for access iLogBufferBuffer class global variables */


/* --------------------- iStaticBuf_LogBuffer::Init ------------------------ */
void
iLogBufferBuffer::Init(void)
{
  ink_mutex_init(&iLogBufferBufferMutex, "iLogBufferBufferMutex");
}

/* ------------------ iLogBufferBuffer::New_iLogBufferBuffer --------------- */
iLogBufferBuffer *
iLogBufferBuffer::New_iLogBufferBuffer(size_t _buf_size)
{
  iLogBufferBuffer **objj, **objj_best = 0;
  iLogBufferBuffer *ob = 0, *ob_best = 0;

  if (_buf_size > 0) {
    ink_mutex_acquire(&iLogBufferBufferMutex);
    for (objj = &free_heap; (ob = *objj) != 0; objj = &(ob->next)) {
      if (ob->real_buf_size == _buf_size) {
        *objj = ob->next;
        break;
      } else if (ob->real_buf_size > _buf_size) {
        if (!ob_best || ob_best->real_buf_size > ob->real_buf_size) {
          ob_best = ob;
          objj_best = objj;
        }
      }
    }
    if (!ob && ob_best && objj_best) {
      *objj_best = (ob = ob_best)->next;
    }
    ink_mutex_release(&iLogBufferBufferMutex);

    if (!ob) {
      ob = new iLogBufferBuffer();
      if (ob) {
        if ((ob->buf = (char *) xmalloc(_buf_size)) == 0) {
          delete ob;
          ob = 0;
        } else {
          ob->real_buf_size = _buf_size;
        }
      }
    }

    if (likely(ob)) {           /* we need to touch it in order to be sure that this
                                   page in the physical memory, plus, we zero it! */
      memset(ob->buf, 0, ob->real_buf_size);
      ob->size = _buf_size;
    }
  }
  return ob;
}

/* ----------------- iLogBufferBuffer::Delete_iLogBufferBuffer ------------- */
iLogBufferBuffer *
iLogBufferBuffer::Delete_iLogBufferBuffer(iLogBufferBuffer * _b)
{
  if (likely(_b)) {
    ink_mutex_acquire(&iLogBufferBufferMutex);
    _b->next = free_heap;
    free_heap = _b;
    ink_mutex_release(&iLogBufferBufferMutex);
  }
  return (iLogBufferBuffer *) 0;
}

/* --------------------------- iObject::Init ------------------------------- */
void
iObject::Init(void)
{
  ink_mutex_init(&iObjectMutex, "iObjectMutex");
}

/* ---------------------------- iObject::new ------------------------------- */
void *
iObject::operator new(size_t _size)
{
  iObject **objj, **objj_best = NULL;
  iObject *ob = NULL, *ob_best = NULL;
  size_t real_size = _size;

  ink_mutex_acquire(&iObjectMutex);
  for (objj = &free_heap; (ob = *objj) != NULL; objj = &(ob->next_object)) {
    if (ob->class_size == _size) {
      *objj = ob->next_object;
      break;
    } else if (ob->class_size > _size) {
      if (!ob_best || ob_best->class_size > ob->class_size) {
        ob_best = ob;
        objj_best = objj;
      }
    }
  }
  if (!ob && ob_best && objj_best) {
    *objj_best = (ob = ob_best)->next_object;
    real_size = ob->class_size;
  }
  ink_mutex_release(&iObjectMutex);

  if (!ob)
    ob = (iObject *) xmalloc(_size);

  if (likely(ob)) {
    memset(ob, 0, _size);
    ob->class_size = real_size;
  }
  return (void *) ob;
}

/* --------------------------- iObject::delete ----------------------------- */
void
iObject::operator delete(void *p)
{
  iObject *ob = (iObject *) p;

  ink_mutex_acquire(&iObjectMutex);
  ob->next_object = free_heap;
  free_heap = ob;
  ink_mutex_release(&iObjectMutex);
}

/*-------------------------------------------------------------------------
  The following LogBufferHeader routines are used to grab strings out from
  the data section using the offsets held in the buffer header.
  -------------------------------------------------------------------------*/

char *
LogBufferHeader::fmt_name()
{
  char *addr = NULL;
  if (fmt_name_offset) {
    addr = (char *) this + fmt_name_offset;
  }
  return addr;
}

char *
LogBufferHeader::fmt_fieldlist()
{
  char *addr = NULL;
  if (fmt_fieldlist_offset) {
    addr = (char *) this + fmt_fieldlist_offset;
  }
  return addr;
}

char *
LogBufferHeader::fmt_printf()
{
  char *addr = NULL;
  if (fmt_printf_offset) {
    addr = (char *) this + fmt_printf_offset;
  }
  return addr;
}

char *
LogBufferHeader::src_hostname()
{
  char *addr = NULL;
  if (src_hostname_offset) {
    addr = (char *) this + src_hostname_offset;
  }
  return addr;
}

char *
LogBufferHeader::log_filename()
{
  char *addr = NULL;
  if (log_filename_offset) {
    addr = (char *) this + log_filename_offset;
  }
  return addr;
}

/*-------------------------------------------------------------------------
  LogBuffer::LogBuffer

  Initialize a LogBuffer object, which is just an AbstractBuffer object
  with the addition of a pointer for keeping track of the LogObject object
  that is allocating this buffer.
  Note: You don't need to 'zero' any memebers in the class instance since iObject
  zero it inside 'operator new', Save CPU resources! :)
  -------------------------------------------------------------------------*/

LogBuffer::LogBuffer(LogObject * owner, size_t size, size_t buf_align, size_t write_align):
  sign(CLASS_SIGN_LOGBUFFER),
  next_flush(NULL),
  next_list(NULL),
  m_new_buffer(NULL),
  m_size(size),
  m_buf_align(buf_align),
  m_write_align(write_align), m_max_entries(Log::config->max_entries_per_buffer), m_owner(owner)
{
  size_t hdr_size;

//    Debug("log-logbuffer","LogBuffer::LogBuffer(owner,size=%ld, buf_align=%ld,write_align=%ld)",
//          size,buf_align,write_align);

  // create the buffer: size + LB_DEFAULT_ALIGN(512)
  m_bb = iLogBufferBuffer::New_iLogBufferBuffer(size + buf_align);
  ink_assert(m_bb != NULL);

  m_unaligned_buffer = m_bb->buf;
  m_buffer = (char *) align_pointer_forward(m_unaligned_buffer, buf_align);

  // add the header
  hdr_size = _add_buffer_header();

  // initialize buffer state
  m_state.s.offset = hdr_size;
  m_state.s.byte_count = hdr_size;

  // update the buffer id (m_id gets the old value)
  m_id = (uint32_t) ink_atomic_increment((pvint32) & M_ID, 1);

  m_expiration_time = LogUtils::timestamp() + Log::config->max_secs_per_buffer;

//    Debug("log-logbuffer","[%p] Created buffer %u for %s at address %p, size %d",
//        this_ethread(), m_id, m_owner->get_base_filename(), m_buffer, (int)size);
}

LogBuffer::LogBuffer(LogObject * owner, LogBufferHeader * header):
  sign(CLASS_SIGN_LOGBUFFER),
  next_flush(NULL),
  next_list(NULL),
  m_bb(NULL),
  m_unaligned_buffer(NULL),
  m_buffer((char *) header),
  m_size(0),
  m_buf_align(LB_DEFAULT_ALIGN),
  m_write_align(INK_MIN_ALIGN), m_max_entries(0), m_expiration_time(0), m_owner(owner), m_header(header)
{
  // This constructor does not allocate a buffer because it gets it as
  // an argument. We set m_unaligned_buffer to NULL, which means that
  // no checkout writes or checkin writes are allowed. This is enforced
  // by the asserts in checkout_write and checkin_write

//    Debug("log-logbuffer","LogBuffer::LogBuffer(owner,header)");

  m_new_buffer = (char *) header;       /* must be deleted inside destructor */

  // update the buffer id (m_id gets the old value)
  //
  m_id = (uint32_t) ink_atomic_increment((pvint32) & M_ID, 1);

//    Debug("log-logbuffer","[%p] Created buffer %u for %s at address %p",
//        this_ethread(), m_id, m_owner->get_base_filename(), m_buffer);
}

LogBuffer::~LogBuffer()
{
  ink_assert(sign == CLASS_SIGN_LOGBUFFER);     /* vl: FIXME remove it later */
  if (sign == CLASS_SIGN_LOGBUFFER) {
    sign = 0;
    m_unaligned_buffer = (m_buffer = 0);
    m_size = 0;
    if (m_new_buffer) {
      delete m_new_buffer;
      m_new_buffer = 0;
    }
    m_bb = iLogBufferBuffer::Delete_iLogBufferBuffer(m_bb);
//      Debug("log-logbuffer", "[%p] Deleted buffer %u", this_ethread(), m_id);
  }
//    else
//      Debug("log-logbuffer", "Incorrect signature 0x%08lX inside LogBuffer::~LogBuffer()", sign);
}

/*-------------------------------------------------------------------------
  LogBuffer::checkout_write
  -------------------------------------------------------------------------*/

LogBuffer::LB_ResultCode LogBuffer::checkout_write(size_t * write_offset, size_t write_size)
{
  // checkout_write should not be called if m_unaligned_buffer was
  // not allocated, which means that the actual buffer data was given
  // to the buffer upon construction. Or, in other words, that
  // LogBuffer::LogBuffer(LogObject *owner, LogBufferHeader *header)
  // was used to construct the object
  //
  //ink_debug_assert(m_unaligned_buffer);

  ink_assert(sign == CLASS_SIGN_LOGBUFFER);
  ink_assert(m_unaligned_buffer != NULL);

  LB_ResultCode
    ret_val = LB_BUSY;
  LB_State
    old_s,
    new_s;
  size_t
    offset = 0;
  size_t actual_write_size = INK_ALIGN(write_size + sizeof(LogEntryHeader), m_write_align);

  uint64_t
    retries = (uint64_t) - 1;
  do {
    old_s = m_state;
    new_s = old_s;

    if (old_s.s.full) {
      // the buffer has already been set to full by somebody else
      // just tell the caller to retry
      ret_val = LB_RETRY;
      break;
    } else {
      // determine what the state would be if nobody changes it
      // before we do

      if (write_offset) {
        if (old_s.s.num_entries < m_max_entries && old_s.s.offset + actual_write_size <= m_size) {
          // there is room for this entry, update the state

          offset = old_s.s.offset;

          ++new_s.s.num_writers;
          new_s.s.offset += actual_write_size;
          ++new_s.s.num_entries;
          new_s.s.byte_count += actual_write_size;

          ret_val = LB_OK;
        } else {
          // there is no room for this entry

          if (old_s.s.num_entries == 0) {
            ret_val = LB_BUFFER_TOO_SMALL;
          } else {
            new_s.s.full = 1;
            ret_val = old_s.s.num_writers ? LB_FULL_ACTIVE_WRITERS : LB_FULL_NO_WRITERS;
          }
        }
      } else {
        // this is a request to set the buffer as full
        // (write_offset == NULL)

        if (old_s.s.num_entries) {
          new_s.s.full = 1;

          ret_val = (old_s.s.num_writers ? LB_FULL_ACTIVE_WRITERS : LB_FULL_NO_WRITERS);
        } else {
          // the buffer has no entries, do nothing

          ret_val = LB_OK;
          break;
        }
      }

      if (switch_state(old_s, new_s)) {
        // we succeded in setting the new state

        break;
      }
    }
    ret_val = LB_BUSY;
  } while (--retries);

  // add the entry header to the buffer if this was a real checkout and
  // the checkout was successful
  //
  if (write_offset && ret_val == LB_OK) {
    // disable statistics for now
    //ProxyMutex *mutex = this_ethread()->mutex;
    //ink_release_assert(mutex->thread_holding == this_ethread());
    //SUM_DYN_STAT(log_stat_bytes_buffered_stat, actual_write_size);

    LogEntryHeader *
      entry_header = (LogEntryHeader *) & m_buffer[offset];
    //entry_header->timestamp = LogUtils::timestamp();
    struct timeval
      tp;
    ink_gethrtimeofday(&tp, 0);
    entry_header->timestamp = tp.tv_sec;
    entry_header->timestamp_usec = tp.tv_usec;
    entry_header->entry_len = actual_write_size;

    *write_offset = offset + sizeof(LogEntryHeader);
  }
//    Debug("log-logbuffer","[%p] %s for buffer %u (%s) returning %d",
//        this_ethread(),
//        (write_offset ? "checkout_write" : "force_new_buffer"),
//        m_id, m_owner->get_base_filename(), ret_val);

  return ret_val;
}

/*-------------------------------------------------------------------------
  LogBuffer::checkin_write
  -------------------------------------------------------------------------*/

LogBuffer::LB_ResultCode LogBuffer::checkin_write(size_t write_offset)
{
  // checkin_write should not be called if m_unaligned_buffer was
  // not allocated, which means that the actual buffer data was given
  // to the buffer upon construction. Or, in other words, that
  // LogBuffer::LogBuffer(LogObject *owner, LogBufferHeader *header)
  // was used to construct the object
  //
  //ink_debug_assert(m_unaligned_buffer);
  ink_assert(sign == CLASS_SIGN_LOGBUFFER);
  ink_assert(m_unaligned_buffer != NULL);

  LB_ResultCode ret_val = LB_OK;
  LB_State old_s, new_s;

  do {
    old_s = m_state;
    new_s = old_s;

    ink_assert(write_offset < old_s.s.offset);
    ink_assert(old_s.s.num_writers > 0);

    if (--new_s.s.num_writers == 0) {
      ret_val = (old_s.s.full ? LB_ALL_WRITERS_DONE : LB_OK);
    }

  } while (!switch_state(old_s, new_s));

//    Debug("log-logbuffer","[%p] checkin_write for buffer %u (%s) "
//        "returning %d (%u writers left)", this_ethread(),
//        m_id, m_owner->get_base_filename(), ret_val, writers_left);

  return ret_val;
}


unsigned
LogBuffer::add_header_str(char *str, char *buf_ptr, unsigned buf_len)
{
  unsigned len = 0;
  if (likely(str && (len = (unsigned) (strlen(str) + 1)) < buf_len)) {
    ink_strncpy(buf_ptr, str, buf_len);
    buf_ptr[len] = '\0';
  }
  return len;
}


size_t
LogBuffer::_add_buffer_header()
{
  size_t header_len;

  ink_assert(sign == CLASS_SIGN_LOGBUFFER);
  //
  // initialize the header
  //
  LogFormat *fmt = m_owner->m_format;
  m_header = (LogBufferHeader *) m_buffer;
  m_header->cookie = LOG_SEGMENT_COOKIE;
  m_header->version = LOG_SEGMENT_VERSION;
  m_header->format_type = fmt->type();
  m_header->entry_count = 0;
  m_header->low_timestamp = LogUtils::timestamp();
  m_header->high_timestamp = 0;
  m_header->log_object_signature = m_owner->get_signature();
  m_header->log_object_flags = m_owner->get_flags();
#if defined(LOG_BUFFER_TRACKING)
  m_header->id = lrand48();
#endif // defined(LOG_BUFFER_TRACKING)

  //
  // The remaining header fields actually point into the data section of
  // the buffer.  Write the data into the buffer and update the total
  // size of the buffer header.
  //

  header_len = sizeof(LogBufferHeader); // at least ...

  m_header->fmt_name_offset = 0;
  m_header->fmt_fieldlist_offset = 0;
  m_header->fmt_printf_offset = 0;
  m_header->src_hostname_offset = 0;
  m_header->log_filename_offset = 0;

  if (fmt->name()) {
    m_header->fmt_name_offset = header_len;
    header_len += add_header_str(fmt->name(), &m_buffer[header_len], m_size - header_len);
  }
  if (fmt->fieldlist()) {
    m_header->fmt_fieldlist_offset = header_len;
    header_len += add_header_str(fmt->fieldlist(), &m_buffer[header_len], m_size - header_len);
  }
  if (fmt->printf_str()) {
    m_header->fmt_printf_offset = header_len;
    header_len += add_header_str(fmt->printf_str(), &m_buffer[header_len], m_size - header_len);
  }
  if (Log::config->hostname) {
    m_header->src_hostname_offset = header_len;
    header_len += add_header_str(Log::config->hostname, &m_buffer[header_len], m_size - header_len);
  }
  if (m_owner->get_base_filename()) {
    m_header->log_filename_offset = header_len;
    header_len += add_header_str(m_owner->get_base_filename(), &m_buffer[header_len], m_size - header_len);
  }
  // update the rest of the header fields; make sure the header_len is
  // correctly aligned, so that the first record will start on a legal
  // alignment mark.
  //

  header_len = INK_ALIGN_DEFAULT(header_len);

  m_header->byte_count = header_len;
  m_header->data_offset = header_len;

  return header_len;
}

void
LogBuffer::update_header_data()
{
  // only update the header if the LogBuffer did not receive its data
  // upon construction (i.e., if m_unaligned_buffer was allocated)
  //
  ink_assert(sign == CLASS_SIGN_LOGBUFFER);

  if (m_unaligned_buffer) {
    m_header->entry_count = m_state.s.num_entries;
    m_header->byte_count = m_state.s.byte_count;
    m_header->high_timestamp = LogUtils::timestamp();
  }
}

/*-------------------------------------------------------------------------
  LogBuffer::max_entry_bytes

  This static function simply returns the greatest number of bytes than an
  entry can be and fit into a LogBuffer.
  -------------------------------------------------------------------------*/
size_t LogBuffer::max_entry_bytes()
{
  return (Log::config->log_buffer_size - sizeof(LogBufferHeader));
}

/*-------------------------------------------------------------------------
  LogBuffer::resolve_custom_entry
  -------------------------------------------------------------------------*/
int
LogBuffer::resolve_custom_entry(LogFieldList * fieldlist,
                                char *printf_str, char *read_from, char *write_to,
                                int write_to_len, long timestamp, long timestamp_usec,
                                unsigned buffer_version, LogFieldList * alt_fieldlist, char *alt_printf_str)
{
  if (fieldlist == NULL || printf_str == NULL)
    return 0;

  int *readfrom_map = NULL;

  if (alt_fieldlist && alt_printf_str) {
    LogField *f, *g;
    int n_alt_fields = alt_fieldlist->count();
    if (unlikely((readfrom_map = (int *) xmalloc(n_alt_fields * sizeof(int))) == NULL))
      return 0;
    int i = 0;

    for (f = alt_fieldlist->first(); f; f = alt_fieldlist->next(f)) {
      int readfrom_pos = 0;
      bool found_match = false;
      for (g = fieldlist->first(); g; g = fieldlist->next(g)) {
        if (strcmp(f->symbol(), g->symbol()) == 0) {
          found_match = true;
          readfrom_map[i++] = readfrom_pos;
          break;
        }
        // TODO handle readfrom_pos properly
        // readfrom_pos += g->size();
      }
      if (!found_match) {
        Note("Alternate format contains a field (%s) not in the " "format logged", f->symbol());
        break;
      }
    }
  }
  //
  // Loop over the printf_str, copying everything to the write_to buffer
  // except the LOG_FIELD_MARKER characters.  When we reach those, we
  // substitute the string from the unmarshal routine of the current
  // LogField object, obtained from the fieldlist.
  //

  LogField *field = fieldlist->first();
  int printf_len = (int)::strlen(printf_str);   // OPTIMIZE
  int bytes_written = 0;
  int res, i;

  const char *buffer_size_exceeded_msg =
    "Traffic Server is skipping the current log entry because its size "
    "exceeds the maximum line (entry) size for an ascii log buffer";

  for (i = 0; i < printf_len; i++) {
    if (printf_str[i] == LOG_FIELD_MARKER) {
      if (field != NULL) {
        char *to = &write_to[bytes_written];

        // for timestamps that are not aggregates, we take the
        // value from the function argument;  otherwise we use the
        // unmarshaling function
        bool non_aggregate_timestamp = false;

        if (field->aggregate() == LogField::NO_AGGREGATE) {
          char *sym = field->symbol();

          if (strcmp(sym, "cqts") == 0) {
            // unmarshal_int_to_str expects data in
            // network order
            timestamp = htonl(timestamp);
            char *ptr = (char *) &timestamp;
            res = LogAccess::unmarshal_int_to_str(&ptr, to, write_to_len - bytes_written);
            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;

          } else if (strcmp(sym, "cqth") == 0) {
            // unmarshal_int_to_str_hex expects data in
            // network order
            timestamp = htonl(timestamp);
            char *ptr = (char *) &timestamp;
            res = LogAccess::unmarshal_int_to_str_hex(&ptr, to, write_to_len - bytes_written);
            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;

          } else if (strcmp(sym, "cqtq") == 0) {
            // From lib/ts
            res = squid_timestamp_to_buf(to, write_to_len - bytes_written, timestamp, timestamp_usec);
            if (res < 0)
              res = -1;

            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;

          } else if (strcmp(sym, "cqtn") == 0) {
            char *str = LogUtils::timestamp_to_netscape_str(timestamp);
            res = (int)::strlen(str);
            if (res < write_to_len - bytes_written) {
              memcpy(to, str, res);
            } else {
              res = -1;
            }
            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;

          } else if (strcmp(sym, "cqtd") == 0) {
            char *str = LogUtils::timestamp_to_date_str(timestamp);
            res = (int)::strlen(str);
            if (res < write_to_len - bytes_written) {
              memcpy(to, str, res);
            } else {
              res = -1;
            }
            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;

          } else if (strcmp(sym, "cqtt") == 0) {
            char *str = LogUtils::timestamp_to_time_str(timestamp);
            res = (int)::strlen(str);
            if (res < write_to_len - bytes_written) {
              memcpy(to, str, res);
            } else {
              res = -1;
            }
            if (buffer_version > 1) {
              // space was reserved in read buffer; remove it
              read_from += INK_MIN_ALIGN;
            }

            non_aggregate_timestamp = true;
          }
        }

        if (!non_aggregate_timestamp) {
          res = field->unmarshal(&read_from, to, write_to_len - bytes_written);
        }

        if (res < 0) {
          Note(buffer_size_exceeded_msg);
          bytes_written = 0;
          break;
        }

        bytes_written += res;
        field = fieldlist->next(field);
      } else {
        Note("There are more field markers than fields;" " cannot process log entry");
        bytes_written = 0;
        break;
      }
    } else {
      if (1 + bytes_written < write_to_len) {
        write_to[bytes_written++] = printf_str[i];
      } else {
        Note(buffer_size_exceeded_msg);
        bytes_written = 0;
        break;
      }
    }
  }

  if (readfrom_map)
    xfree(readfrom_map);
  return bytes_written;
}

/*-------------------------------------------------------------------------
  LogBuffer::to_ascii

  This routine converts a log entry into an ascii string in the buffer
  space provided, and returns the length of the new string (not including
  trailing null, like strlen).
  -------------------------------------------------------------------------*/
int
LogBuffer::to_ascii(LogEntryHeader * entry, LogFormatType type,
                    char *buf, int buf_len, char *symbol_str, char *printf_str,
                    unsigned buffer_version, char *alt_format)
{
  ink_assert(entry != NULL);
  ink_assert(type >= 0 && type < N_LOG_TYPES);
  ink_assert(buf != NULL);

  char *read_from;              // keeps track of where we're reading from entry
  char *write_to;               // keeps track of where we're writing into buf

  read_from = (char *) entry + sizeof(LogEntryHeader);
  write_to = buf;

  if (type == TEXT_LOG) {
    //
    // text log entries are just strings, so simply move it into the
    // format buffer.
    //
    ink_string_copy(write_to, read_from, buf_len);
    return (int)::strlen(write_to);     // OPTIMIZE, should not need strlen
  }
  //
  // We no longer make the distinction between custom vs pre-defined
  // logging formats in converting to ASCII.  This way we're sure to
  // always be using the correct printf string and symbols for this
  // buffer since we get it from the buffer header.
  //
  // We want to cache the unmarshaling "plans" so that we don't have to
  // re-create them each time.  We can use the symbol string as a key to
  // these stored plans.
  //

  int i;
  LogFieldList *fieldlist = NULL;

  for (i = 0; i < fieldlist_cache_entries; i++) {
    if (strcmp(symbol_str, fieldlist_cache[i].symbol_str) == 0) {
      Debug("log-fieldlist", "Fieldlist for %s found in cache, #%d", symbol_str, i);
      fieldlist = fieldlist_cache[i].fieldlist;
      break;
    }
  }

  if (!fieldlist) {
    Debug("log-fieldlist", "Fieldlist for %s not found; creating ...", symbol_str);
    fieldlist = NEW(new LogFieldList);
    ink_assert(fieldlist != NULL);
    bool contains_aggregates = false;
    LogFormat::parse_symbol_string(symbol_str, fieldlist, &contains_aggregates);

    if (fieldlist_cache_entries < FIELDLIST_CACHE_SIZE) {
      Debug("log-fieldlist", "Fieldlist cached as entry %d", fieldlist_cache_entries);
      fieldlist_cache[fieldlist_cache_entries].fieldlist = fieldlist;
      fieldlist_cache[fieldlist_cache_entries].symbol_str = xstrdup(symbol_str);
      fieldlist_cache_entries++;
    }
  }

  LogFieldList *alt_fieldlist = NULL;
  char *alt_printf_str = NULL;
  char *alt_symbol_str = NULL;
  bool bad_alt_format = false;

  if (alt_format) {
    int n_alt_fields = LogFormat::parse_format_string(alt_format,
                                                      &alt_printf_str, &alt_symbol_str);
    if (n_alt_fields < 0) {
      Note("Error parsing alternate format string: %s", alt_format);
      bad_alt_format = true;
    }

    if (!bad_alt_format) {
      alt_fieldlist = NEW(new LogFieldList);
      bool contains_aggs = false;
      int n_alt_fields2 = LogFormat::parse_symbol_string(alt_symbol_str,
                                                         alt_fieldlist, &contains_aggs);
      if (n_alt_fields2 > 0 && contains_aggs) {
        Note("Alternative formats not allowed to contain aggregates");
        bad_alt_format = true;;
      }
    }
  }

  if (bad_alt_format) {
    if (alt_fieldlist)
      delete alt_fieldlist;
    if (alt_printf_str)
      xfree(alt_printf_str);
    if (alt_symbol_str)
      xfree(alt_symbol_str);
    alt_fieldlist = NULL;
    alt_printf_str = NULL;
    alt_symbol_str = NULL;
  }

  int ret = resolve_custom_entry(fieldlist, printf_str,
                                 read_from, write_to, buf_len, entry->timestamp,
                                 entry->timestamp_usec, buffer_version,
                                 alt_fieldlist, alt_printf_str);

  if (alt_fieldlist)
    delete alt_fieldlist;
  if (alt_printf_str)
    xfree(alt_printf_str);
  if (alt_symbol_str)
    xfree(alt_symbol_str);

  return ret;
}

/*-------------------------------------------------------------------------
  LogBuffer::convert_to_network_order

  This routine will convert all of the integer fields in the buffer header
  and the entry headers to network byte order.  This is necessary for the
  buffer to be able to move from a machine with one byte order to a
  collation host with a different byte order.  On the other end, we'll
  convert back to host order.
  -------------------------------------------------------------------------*/

void
LogBuffer::convert_to_network_order()
{
  convert_to_network_order(m_header);
}

void
LogBuffer::convert_to_network_order(LogBufferHeader * header)
{
  Debug("log-sock", "Converting buffer to network byte order");
  ink_assert(header != NULL);
  //
  // First, change each of the entry headers.  Order is important,
  // because if we convert the header fields first, the iterator will
  // be screwed up.
  //
  LogBufferIterator iter(header);
  LogEntryHeader *entry_header;
  while ((entry_header = iter.next())) {
    entry_header->timestamp = htonl(entry_header->timestamp);
    entry_header->timestamp_usec = htonl(entry_header->timestamp_usec);
    entry_header->entry_len = htonl(entry_header->entry_len);
  }

  header->cookie = htonl(header->cookie);
  header->version = htonl(header->version);
  header->format_type = htonl(header->format_type);
  header->byte_count = htonl(header->byte_count);
  header->entry_count = htonl(header->entry_count);
  header->low_timestamp = htonl(header->low_timestamp);
  header->high_timestamp = htonl(header->high_timestamp);
  header->log_object_flags = htonl(header->log_object_flags);
  header->fmt_name_offset = htonl(header->fmt_name_offset);
  header->fmt_fieldlist_offset = htonl(header->fmt_fieldlist_offset);
  header->fmt_printf_offset = htonl(header->fmt_printf_offset);
  header->src_hostname_offset = htonl(header->src_hostname_offset);
  header->log_filename_offset = htonl(header->log_filename_offset);
  header->data_offset = htonl(header->data_offset);
#if defined(LOG_BUFFER_TRACKING)
  header->id = htonl(header->id);
#endif // defined(LOG_BUFFER_TRACKING)

  // signature 64 bits long, convert each 32 bit part separately
  //
  uint32_t sig[2];
  sig[0] = htonl((uint32_t) header->log_object_signature);
  sig[1] = htonl((uint32_t) (header->log_object_signature >> 32));
  header->log_object_signature = ((uint64_t) sig[1] << 32) | sig[0];
}

/*-------------------------------------------------------------------------
  LogBuffer::convert_to_host_order

  This routine will convert all of the integer fields in the buffer header
  and the entry headers to host byte order.

  Broken out into a static function so that we can call it on data
  that we've received over the network but haven't associated with
  a LogBuffer just yet.
  -------------------------------------------------------------------------*/

void
LogBuffer::convert_to_host_order()
{
  convert_to_host_order(m_header);
}


// TODO: This is probably broken with the migration to 64-bit log ints.
void
LogBuffer::convert_to_host_order(LogBufferHeader * header)
{
  Debug("log-sock", "Converting buffer to host byte order");
  ink_assert(header != NULL);

  header->cookie = ntohl(header->cookie);
  header->version = ntohl(header->version);
  header->format_type = ntohl(header->format_type);
  header->byte_count = ntohl(header->byte_count);
  header->entry_count = ntohl(header->entry_count);
  header->low_timestamp = ntohl(header->low_timestamp);
  header->high_timestamp = ntohl(header->high_timestamp);
  header->log_object_flags = ntohl(header->log_object_flags);
  header->fmt_name_offset = ntohl(header->fmt_name_offset);
  header->fmt_fieldlist_offset = ntohl(header->fmt_fieldlist_offset);
  header->fmt_printf_offset = ntohl(header->fmt_printf_offset);
  header->src_hostname_offset = ntohl(header->src_hostname_offset);
  header->log_filename_offset = ntohl(header->log_filename_offset);
  header->data_offset = ntohl(header->data_offset);
#if defined(LOG_BUFFER_TRACKING)
  header->id = ntohl(header->id);
#endif // defined(LOG_BUFFER_TRACKING)

  // signature 64 bits long, convert each 32 bit part separately
  //
  uint32_t sig[2];
  sig[0] = ntohl((uint32_t) header->log_object_signature);
  sig[1] = ntohl((uint32_t) (header->log_object_signature >> 32));
  header->log_object_signature = ((uint64_t) sig[1] << 32) | sig[0];

  //
  // Next, convert the entry headers
  //
  LogBufferIterator iter(header, true);
  LogEntryHeader *entry_header;

  while ((entry_header = iter.next())) {
    entry_header->timestamp = ntohl(entry_header->timestamp);
    entry_header->timestamp_usec = ntohl(entry_header->timestamp_usec);
    entry_header->entry_len = ntohl(entry_header->entry_len);
  }
}

/*-------------------------------------------------------------------------
  LogBufferList

  The operations on this list need to be atomic because the buffers are
  added by client threads and removed by the logging thread.  This is
  accomplished by protecting the operations with a mutex.

  Also, the list must offer FIFO semantics so that a buffers are removed
  from the list in the same order that they are added, so that timestamp
  ordering in the log file is preserved.
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  LogBufferList::LogBufferList
  -------------------------------------------------------------------------*/

LogBufferList::LogBufferList()
{
  m_size = 0;
  m_list_last_ptr = (m_list = 0);
  ink_mutex_init(&m_mutex, "LogBufferList");
}

/*-------------------------------------------------------------------------
  LogBufferList::~LogBufferList
  -------------------------------------------------------------------------*/

LogBufferList::~LogBufferList()
{
  LogBuffer *lb, *_list;
  ink_mutex_acquire(&m_mutex);
  _list = m_list;
  m_list_last_ptr = (m_list = 0);
  m_size = 0;
  ink_mutex_release(&m_mutex);

  while ((lb = _list) != NULL) {
    _list = lb->next_list;
    delete lb;
  }
  ink_mutex_acquire(&m_mutex);
  ink_mutex_release(&m_mutex);
  ink_mutex_destroy(&m_mutex);
}

/*-------------------------------------------------------------------------
  LogBufferList::add (or enqueue)
  -------------------------------------------------------------------------*/

void
LogBufferList::add(LogBuffer * lb)
{
  ink_assert(lb != NULL);
  lb->next_list = 0;
  ink_mutex_acquire(&m_mutex);
  if (m_list && m_list_last_ptr) {
    m_list_last_ptr->next_list = lb;
  } else
    m_list = lb;
  m_list_last_ptr = lb;
  m_size++;
  ink_mutex_release(&m_mutex);
}

/*-------------------------------------------------------------------------
  LogBufferList::get (or dequeue)
  -------------------------------------------------------------------------*/

LogBuffer *
LogBufferList::get()
{
  LogBuffer *lb;

  ink_mutex_acquire(&m_mutex);

  if ((lb = m_list) != 0) {
    if ((m_list = lb->next_list) == 0)
      m_list_last_ptr = 0;
    m_size--;
  }
  ink_assert(m_size >= 0);
  ink_mutex_release(&m_mutex);
  return lb;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogEntryHeader *
LogBufferIterator::next()
{
  LogEntryHeader *ret_val = NULL;
  LogEntryHeader *entry = (LogEntryHeader *) m_next;

  if (entry) {
    if (m_iter_entry_count < m_buffer_entry_count &&
        m_iter_entry_count < (unsigned) Log::config->max_entries_per_buffer) {
      m_next += m_in_network_order ? ntohl(entry->entry_len) : entry->entry_len;
      ++m_iter_entry_count;
      ret_val = entry;
    }
  }

  return ret_val;
}
