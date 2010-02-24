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

#include "NewCacheVC.h"
#include "InkAPIInternal.h"
#include "HdrUtils.h"
#include "HttpMessageBody.h"
#include "HttpTunnel.h"
#include <iostream>
#include <string>

using namespace std;

static char bound[] = "RANGE_SEPARATOR";
static char range_type[] = "multipart/byteranges; boundary=RANGE_SEPARATOR";
static char cont_type[] = "Content-type: ";
static char cont_range[] = "Content-range: bytes ";
static int sub_header_size = sizeof(cont_type) - 1 + 2 + sizeof(cont_range) - 1 + 4;
static int boundary_size = 2 + sizeof(bound) - 1 + 2;

extern int cache_config_http_max_alts;

inline static int
num_chars_for_int(int i)
{
  int k;

  if (i < 0)
    return 0;

  k = 1;

  while ((i /= 10) != 0)
    k++;

  return k;
}


//----------------------------------------------------------------------------
void
NewCacheVC::getCacheKey(void **key, int *length)
{
  switch (_state) {

  case NEW_CACHE_WRITE_HEADER:
  case NEW_CACHE_LOOKUP:
    // use the url as the key for the httpinfo vector
    *key = _url;
    *length = _url_length;
    break;

  case NEW_CACHE_WRITE_DATA:
  case NEW_CACHE_WRITE_DATA_APPEND:
    *key = (void *) (&_cacheKey);
    *length = sizeof(CacheKey);
    break;

  case NEW_CACHE_READ_DATA:
  case NEW_CACHE_READ_DATA_APPEND:
    CacheKey tmpKey = _readCacheHttpInfo.object_key_get();
    _cacheKey = tmpKey;         // XXX why can't I assign from the method
    *key = (void *) (&_cacheKey);
    *length = sizeof(CacheKey);
    break;
  }

}

//----------------------------------------------------------------------------
void
NewCacheVC::getCacheHeaderKey(void **key, int *length)
{
  // use the url as the key for the httpinfo vector
  *key = _url;
  *length = _url_length;
}


//----------------------------------------------------------------------------
void
NewCacheVC::reenable(VIO * vio)
{
  Debug("cache_plugin", "[NewCacheVC::reenable] this=%lX vio=%lX", (long) this, (long) vio);
  if (_vio.op == VIO::WRITE) {
    if (!_vio.buffer.reader()->read_avail()) {
      ink_assert(!"useless reenable of cache write");
    }
    SET_HANDLER(&NewCacheVC::handleWrite);
    if (!trigger) {
      trigger = vio->mutex->thread_holding->schedule_imm_local(this);
    }
  } else {
    SET_HANDLER(&NewCacheVC::handleRead);
    if (!trigger) {
      trigger = vio->mutex->thread_holding->schedule_imm_local(this);
    }
  }

}


//----------------------------------------------------------------------------
VIO *
NewCacheVC::do_io_read(Continuation * c, ink64 nbytes, MIOBuffer * buf)
{
  Debug("cache_plugin", "[NewCacheVC::do_io_read] this=%lX c=%lX nbytes=%d", (long) this, (long) c, nbytes);
  switch (_state) {

    //case NEW_CACHE_LOOKUP:
    //  _state = NEW_CACHE_READ_DATA;
    //  break;

  case NEW_CACHE_READ_DATA:
  case NEW_CACHE_READ_DATA_APPEND:
    _state = NEW_CACHE_READ_DATA_APPEND;
    //_offset = 0; //XXX in what cases does this happen?
    break;
  default:
    _state = NEW_CACHE_READ_DATA;
    break;
  }

  closed = false;
  _vio.op = VIO::READ;
  _httpTunnel = (HttpTunnel *) c;
  _vio.buffer.writer_for(buf);
  _vio.set_continuation(c);
  _vio.ndone = 0;
  _vio.nbytes = nbytes;
  _vio.vc_server = this;

  _setup_read();

  ink_assert(c->mutex->thread_holding);
  SET_HANDLER(&NewCacheVC::handleRead);
  if (!trigger) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }
  return &_vio;
}

//------------------------------------------------------------------------------
int
NewCacheVC::handleRead(int event, Event * e)
{
  Debug("cache_plugin", "[NewCacheVC::handleRead] this=%lX event=%d", (long) this, event);
  cancel_trigger();

  if (!closed)
    _cacheReadHook->invoke(INK_EVENT_CACHE_READ, this);

  return 1;
}


//----------------------------------------------------------------------------
VIO *
NewCacheVC::do_io_write(Continuation * c, ink64 nbytes, IOBufferReader * buf, bool owner)
{
  Debug("cache_plugin", "[NewCacheVC::do_io_write] this=%lX c=%lX", (long) this, (long) c);

  // change the state based on the prior state
  switch (_state) {

  case NEW_CACHE_WRITE_HEADER:
    _state = NEW_CACHE_WRITE_DATA;
    break;

  case NEW_CACHE_WRITE_DATA:
  case NEW_CACHE_WRITE_DATA_APPEND:
    _state = NEW_CACHE_WRITE_DATA_APPEND;
    break;
  default:
    break;
  }

  closed = false;

  // XXX create the cache key for the http_info for when we do a cache write
  // do this here instead of set_http_info because we should not create a new key
  //   for a header-only update
  // maybe it should be created somewhere else in the code
  // we assume that no reads happen between this and do_io_close
  Debug("cache_plugin", "creating new cache key");
  rand_CacheKey(&_cacheKey, mutex);

  ink_assert(_vio.op == VIO::WRITE);
  ink_assert(!owner);
  _httpTunnel = (HttpTunnel *) c;
  _vio.buffer.reader_for(buf);
  _vio.set_continuation(c);
  _vio.ndone = 0;
  _vio.nbytes = nbytes;
  _vio.vc_server = this;
  ink_assert(c->mutex->thread_holding);

  //_cacheWriteHook->invoke(INK_EVENT_CACHE_OPEN_WRITE, this);

  SET_HANDLER(&NewCacheVC::handleWrite);
  if (!trigger) {
    trigger = c->mutex->thread_holding->schedule_imm_local(this);
  }

  return &_vio;
}


//------------------------------------------------------------------------------
int
NewCacheVC::handleWrite(int event, Event * e)
{
  Debug("cache_plugin", "[NewCacheVC::handleWrite] event=%d", event);
  cancel_trigger();

  if (!closed)
    _cacheWriteHook->invoke(INK_EVENT_CACHE_WRITE, this);
  return 1;
}


//----------------------------------------------------------------------------
NewCacheVC *
NewCacheVC::alloc(Continuation * cont, URL * url, HttpCacheSM * sm)
{
  EThread *t = cont->mutex->thread_holding;

  //initializes to alloc prototype
  NewCacheVC *c = THREAD_ALLOC_INIT(newCacheVConnectionAllocator, t);
  //c->vector.data.data = &c->vector.data.fast_data[0];
  //c->_action = cont;
  new(&c->_httpInfoVector) CacheHTTPInfoVector;
  c->mutex = cont->mutex;
  //c->start_time = ink_get_hrtime();
  //ink_assert(c->trigger == NULL);
  Debug("cache_plugin", "[NewCacheVC::alloc] new %lX", (long) c);
  c->_vio.op = VIO::READ;
  c->_lookupUrl = url;
  c->_url = url->string_get_ref(&c->_url_length);
  c->_sm = sm;
  c->_cacheWriteHook = cache_global_hooks->get(INK_CACHE_PLUGIN_HOOK);
  c->_cacheReadHook = cache_global_hooks->get(INK_CACHE_PLUGIN_HOOK);

  return c;
}

//----------------------------------------------------------------------------
void
NewCacheVC::setWriteVC(CacheHTTPInfo * old_info)
{
  cancel_trigger();
  _vio.op = VIO::WRITE;
  closed = false;
}

//----------------------------------------------------------------------------
void
NewCacheVC::do_io_close(int lerrno)
{
  Debug("cache_plugin", "[NewCacheVC::do_io_close] %lX lerrno: %d state: %d", (long) this, lerrno, _state);

  if (!closed) {
    closed = true;
    if (lerrno == -1) {
      switch (_state) {

      case NEW_CACHE_WRITE_HEADER:
        //break;
      case NEW_CACHE_WRITE_DATA:
      case NEW_CACHE_WRITE_DATA_APPEND:
        _writeHttpInfo();
        break;
      default:

        if (_vio.op == VIO::WRITE) {
          // do_io_close without set_http_info is a delete
          _cacheWriteHook->invoke(INK_EVENT_CACHE_DELETE, this);
        }

        break;
      }
    }
  }

}


//----------------------------------------------------------------------------
void
NewCacheVC::reenable_re(VIO * vio)
{
  Debug("cache_plugin", "[NewCacheVC::reenable_re]");
}


//----------------------------------------------------------------------------
void
NewCacheVC::set_http_info(CacheHTTPInfo * ainfo)
{
  _state = NEW_CACHE_WRITE_HEADER;

  Debug("cache_plugin", "[NewCacheVC::set_http_info] this=%lX ainfo=%lX", (long) this, (long) ainfo);
  _writeCacheHttpInfo.copy_shallow((HTTPInfo *) ainfo);
  //set the key and size from the previously chosen alternate in case it is a header-only update
  //we assume that no reads happen between this and do_io_close
  if (m_alt_index >= 0) {
    CacheHTTPInfo *info = _httpInfoVector.get(m_alt_index);
    if (info) {
      CacheKey tmpKey = info->object_key_get();
      _cacheKey = tmpKey;
      _totalObjectSize = info->object_size_get();
    }
  }

  ainfo->clear();
}


//----------------------------------------------------------------------------
void
NewCacheVC::set_cache_http_hdr(HTTPHdr * request)
{
  Debug("cache_plugin", "[NewCacheVC::set_cache_http_hdr]");
  _state = NEW_CACHE_LOOKUP;
  _request.copy(request);
}


//----------------------------------------------------------------------------
void
NewCacheVC::get_http_info(CacheHTTPInfo ** info)
{
  *info = &_readCacheHttpInfo;
  Debug("cache_plugin", "[NewCacheVC::get_http_info] object_size=%d", (*info)->object_size_get());
}

void
NewCacheVC::_append_unmarshal_buf(const void *data, const inku64 size)
{
  if (data == NULL || size == 0)
    return;

  char *cur_buf = m_unmarshal_buf;
  size_t cur_buf_size = sizeof(m_unmarshal_buf);
  if (m_overflow_unmarshal_buf_size > 0) {
    cur_buf = m_overflow_unmarshal_buf;
    cur_buf_size = m_overflow_unmarshal_buf_size;
  }

  if (_offset + size > cur_buf_size) {
    while (cur_buf_size < _offset + size) {
      if ((cur_buf_size << 1) > cur_buf_size)
        cur_buf_size <<= 1;
      else
        cur_buf_size = (size_t) - 1;
    }
    char *new_buf = (char *) xmalloc(cur_buf_size);
    if (new_buf == NULL) {
      Error("Failed to alloc %lu bytes for unmarshal buffer");
      abort();
    }
    memcpy(new_buf, cur_buf, _offset);

    if (m_overflow_unmarshal_buf != NULL) {
      xfree(m_overflow_unmarshal_buf);
    }
    m_overflow_unmarshal_buf = cur_buf = new_buf;
    m_overflow_unmarshal_buf_size = cur_buf_size;
  }

  memcpy(cur_buf + _offset, data, size);
  _offset += size;
}

int
NewCacheVC::handleLookup(int event, Event * e)
{
  Debug("cache_plugin", "[NewCacheVC::handleLookup] event=%d", event);
  cancel_trigger();

  if (!closed)
    _cacheReadHook->invoke(INK_EVENT_CACHE_LOOKUP, this);
  return 1;
}

//----------------------------------------------------------------------------
bool
NewCacheVC::appendCacheHttpInfo(const void *data, const inku64 size)
{
  if (!data) {
    Debug("cache_plugin", "[NewCacheVC::appendCacheHttpInfo] data NULL");
    return false;
  }

  _append_unmarshal_buf(data, size);

  SET_HANDLER(&NewCacheVC::handleLookup);
  if (!trigger) {
    trigger = this->mutex->thread_holding->schedule_imm_local(this);
  }
  return true;
}

//----------------------------------------------------------------------------
bool
NewCacheVC::completeCacheHttpInfo(const void *data, const inku64 size)
{
  if (!data) {
    Error("[NewCacheVC::completeCacheHttpInfo] data NULL");
    return false;
  }

  _append_unmarshal_buf(data, size);

  char *ubuf = (m_overflow_unmarshal_buf ? m_overflow_unmarshal_buf : m_unmarshal_buf);
  if (_httpInfoVector.unmarshal(ubuf, _offset, _httpInfoVector.vector_buf) < 0) {
    Error("[NewCacheVC::setCacheHttpInfo] failed to unmarshal (buf=0x%x)", ubuf);
    return false;
  }

  m_alt_index = HttpTransactCache::SelectFromAlternates(&_httpInfoVector, &_request, _params);
  if (m_alt_index < 0) {
    Debug("cache_plugin", "[NewCacheVC::setCacheHttpInfo] no alternate index");
    return false;
  }

  _offset = 0;

  // we should get the Httpinfo for the corresponding alternate
  CacheHTTPInfo *obj = _httpInfoVector.get(m_alt_index);
  if (obj != NULL) {
    //marshaled httpinfo is not writable, 
    //need to deep copy since headers may be modified
    _readCacheHttpInfo.copy((HTTPInfo *) obj);

    m_content_length = _readCacheHttpInfo.object_size_get();
    m_num_chars_for_cl = num_chars_for_int(m_content_length);
    m_content_type =
      _readCacheHttpInfo.response_get()->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &m_content_type_len);
    doRangeSetup();
  }

  return true;
}


//----------------------------------------------------------------------------
bool
NewCacheVC::setRangeAndSize(INKU64 size)
{
  bool retVal = false;
  if (!m_range_present || m_num_range_fields == 1) {
    _vio.ndone += size;
    _offset += size;
    //cout<<"New Cache VC :"<<this<<"offset"<<_offset<<endl;
    //cout<<"New Cache VC :"<<this<<"_vio.ndone"<<_vio.ndone<<endl;
    if ((_vio.nbytes - _vio.ndone) < 32768) {
      _size = (_vio.nbytes - _vio.ndone);
      //cout<<"New Cache VC :"<<this<<"size to read next"<<_size<<endl;
    } else {
      _size = 32768;
      //cout<<"New Cache VC :"<<this<<"size to read next"<<_size<<endl;
    }
  } else {
    //cout<<"Mutli Range"<<endl;
    if (m_ranges[m_current_range_idx]._done_byte == 0) {
      if (m_current_range_idx)
        _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("\r\n", 2);
      add_boundary(false);
      add_sub_header(m_current_range_idx);
    }
    _vio.ndone += size;
    m_ranges[m_current_range_idx]._done_byte += size;
    if ((m_ranges[m_current_range_idx]._end - m_ranges[m_current_range_idx]._start) + 1 ==
        m_ranges[m_current_range_idx]._done_byte) {
      if (m_current_range_idx == m_num_range_fields - 1) {
        _vio.ndone += 2;
        //add_boundary(true);
        retVal = true;
      } else
        m_current_range_idx++;
    }
    _size =
      (m_ranges[m_current_range_idx]._end - m_ranges[m_current_range_idx]._start) + 1 -
      (m_ranges[m_current_range_idx]._done_byte);
    _offset = m_ranges[m_current_range_idx]._start;
  }
  return retVal;

}


//----------------------------------------------------------------------------
void
NewCacheVC::add_boundary(bool end)
{
  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("--", 2);
  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write(bound, sizeof(bound) - 1);

  if (end)
    _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("--", 2);

  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("\r\n", 2);
}

#define RANGE_NUMBERS_LENGTH 60


//----------------------------------------------------------------------------
void
NewCacheVC::add_sub_header(int index)
{
  // this should be large enough to hold three integers!
  char numbers[RANGE_NUMBERS_LENGTH];
  int len;

  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write(cont_type, sizeof(cont_type) - 1);
  if (m_content_type)
    _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write(m_content_type, m_content_type_len);
  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("\r\n", 2);
  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write(cont_range, sizeof(cont_range) - 1);

  ink_snprintf(numbers, sizeof(numbers), "%d-%d/%d", m_ranges[index]._start, m_ranges[index]._end, m_content_length);
  len = strlen(numbers);
  if (len < RANGE_NUMBERS_LENGTH)
    _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write(numbers, len);
  _vio.ndone += getTunnel()->get_producer(this)->read_buffer->write("\r\n\r\n", 4);
}


//----------------------------------------------------------------------------
void
NewCacheVC::doRangeSetup()
{
  if ((_readCacheHttpInfo.response_get()->status_get() == HTTP_STATUS_OK) && _request.presence(MIME_PRESENCE_RANGE) &&
      (_request.method_get_wksidx() == HTTP_WKSIDX_GET) && (_request.version_get() == HTTPVersion(1, 1))) {

    m_range_field = _request.field_find(MIME_FIELD_RANGE, MIME_LEN_RANGE);
    parseRange();
    if (!m_unsatisfiable_range && m_range_hdr_valid == true) {
      m_range_present = true;
      calculateCl();
      modifyRespHdr();
    }
  }
}


//----------------------------------------------------------------------------
void
NewCacheVC::parseRange()
{
  int prev_good_range, i;
  const char *value;
  int value_len;
  HdrCsvIter csv;
  const char *s, *e;

  if (m_content_length <= 0)
    return;

  ink_assert(m_range_field != NULL);

  m_num_range_fields = 0;
  value = csv.get_first(m_range_field, &value_len);

  while (value) {
    m_num_range_fields++;
    value = csv.get_next(&value_len);
  }

  if (m_num_range_fields <= 0)
    return;

  m_ranges = NEW(new RangeRecord[m_num_range_fields]);

  value = csv.get_first(m_range_field, &value_len);

  i = 0;
  prev_good_range = -1;
  // Currently HTTP/1.1 only defines bytes Range
  if (ptr_len_ncmp(value, value_len, "bytes=", 6) == 0) {
    m_range_hdr_valid = true;
    while (value) {
      // If delimiter '-' is missing
      if (!(e = (const char *) memchr(value, '-', value_len))) {
        value = csv.get_next(&value_len);
        i++;
        continue;
      }

      s = value;
      mime_parse_integer(s, e, &m_ranges[i]._start);

      e++;
      s = e;
      e = value + value_len;
      mime_parse_integer(s, e, &m_ranges[i]._end);

      // check and change if necessary whether this is a right entry
      // the last _end bytes are required
      if (m_ranges[i]._start == -1 && m_ranges[i]._end > 0) {
        if (m_ranges[i]._end > m_content_length)
          m_ranges[i]._end = m_content_length;

        m_ranges[i]._start = m_content_length - m_ranges[i]._end;
        m_ranges[i]._end = m_content_length - 1;
      }
      // open start
      else if (m_ranges[i]._start >= 0 && m_ranges[i]._end == -1) {
        if (m_ranges[i]._start >= m_content_length)
          m_ranges[i]._start = -1;
        else
          m_ranges[i]._end = m_content_length - 1;
      }
      // "normal" Range - could be wrong if _end<_start
      else if (m_ranges[i]._start >= 0 && m_ranges[i]._end >= 0) {
        if (m_ranges[i]._start > m_ranges[i]._end || m_ranges[i]._start >= m_content_length)
          m_ranges[i]._start = m_ranges[i]._end = -1;
        else if (m_ranges[i]._end >= m_content_length)
          m_ranges[i]._end = m_content_length - 1;
      }

      else
        m_ranges[i]._start = m_ranges[i]._end = -1;

      // this is a good Range entry
      if (m_ranges[i]._start != -1) {
        if (m_unsatisfiable_range) {
          m_unsatisfiable_range = false;
          // initialize m_current_range to the first good Range
          m_current_range = i;
        }
        // currently we don't handle out-of-order Range entry
        else if (prev_good_range >= 0 && m_ranges[i]._start <= m_ranges[prev_good_range]._end) {
          m_not_handle_range = true;
          break;
        }

        prev_good_range = i;
      }

      value = csv.get_next(&value_len);
      i++;
    }
  } else
    m_range_hdr_valid = false;
}


//----------------------------------------------------------------------------
void
NewCacheVC::calculateCl()
{
  int i;

  if (m_unsatisfiable_range)
    return;

  if (m_num_range_fields == 1) {
    m_output_cl = (m_ranges[0]._end - m_ranges[0]._start) + 1;
  }


  else {
    for (i = 0; i < m_num_range_fields; i++) {
      if (m_ranges[i]._start >= 0) {
        m_output_cl += boundary_size;
        m_output_cl += sub_header_size + m_content_type_len;
        m_output_cl += num_chars_for_int(m_ranges[i]._start)
          + num_chars_for_int(m_ranges[i]._end) + m_num_chars_for_cl + 2;
        m_output_cl += m_ranges[i]._end - m_ranges[i]._start + 1;
        m_output_cl += 2;
      }
    }

    m_output_cl += boundary_size + 2;
  }

  Debug("transform_range", "Pre-calculated Content-Length for Range response is %d", m_output_cl);
}


//----------------------------------------------------------------------------
void
NewCacheVC::modifyRespHdr()
{
  MIMEField *field, *contentLength;
  char *reason_phrase;
  HTTPStatus status_code = HTTP_STATUS_PARTIAL_CONTENT;

  HTTPHdr *response = _readCacheHttpInfo.response_get();
  ink_assert(response);

  response->status_set(status_code);
  reason_phrase = (char *) (HttpMessageBody::StatusCodeName(status_code));
  response->reason_set(reason_phrase, strlen(reason_phrase));
  contentLength = response->field_find(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
  if (contentLength != NULL) {
    response->field_value_set_int(contentLength, m_output_cl);
    Debug("cache_plugin", "setting content-length %d", m_output_cl);
  } else {
    Debug("cache_plugin", "did not set content-length %d", m_output_cl);
  }

  _readCacheHttpInfo.object_size_set(m_output_cl);

  if (m_num_range_fields > 1) {
    field = response->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);

    if (field != NULL)
      response->field_delete(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);


    field = response->field_create(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    field->value_append(response->m_heap, response->m_mime, range_type, sizeof(range_type) - 1);

    response->field_attach(field);
  } else {
    char numbers[60];

    field = response->field_create(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
    ink_snprintf(numbers, sizeof(numbers), "bytes %d-%d/%d", m_ranges[0]._start, m_ranges[0]._end, m_content_length);
    field->value_append(response->m_heap, response->m_mime, numbers, strlen(numbers));
    response->field_attach(field);
  }
}


//----------------------------------------------------------------------------
//void NewCacheVC::getCacheBufferInfo(void **buff,inku64 *size, inku64 *offset)
void
NewCacheVC::getCacheBufferInfo(inku64 * size, inku64 * offset)
{
  //*buff = _vio.get_writer();
  *size = _size;
  *offset = _offset;
}


//----------------------------------------------------------------------------
IOBufferReader *
NewCacheVC::getBufferReader()
{
  if (_state == NEW_CACHE_WRITE_HEADER) {
    return _httpInfoBufferReader;
  } else {
    return _vio.get_reader();
  }
}


//----------------------------------------------------------------------------
// PRIVATE Helper methods
//----------------------------------------------------------------------------

//------------------------------------------------------------------------------
void
NewCacheVC::_setup_read()
{
  _size = 32768;
  if (m_range_present) {
    _offset = m_ranges[m_current_range_idx]._start;
    //cout<<"New Cache VC :"<<this<<"_offset is "<<_offset<<"m_current_range_idx"<<m_current_range_idx<<endl;
    //cout<<"New Cache VC :"<<this<<"content length is "<<m_output_cl<<endl;

    if (m_num_range_fields == 1) {
      if (m_output_cl < 32768) {
        _size = m_output_cl;
      }
      //cout<<"New Cache VC :"<<this<<" and _size:"<<_size<<endl;
    } else {
      //cout<<"Mutli Range"<<endl;
      _size = (m_ranges[m_current_range_idx]._end - m_ranges[m_current_range_idx]._start) + 1;
    }

  } else {
    if (m_content_length < 32768) {
      _size = m_content_length;
    }
  }
}

//----------------------------------------------------------------------------
void
NewCacheVC::_writeHttpInfo()
{
  Debug("cache_plugin", "[NewCacheVC::_writeHttpInfo]");
  // since we are writing the header set the state
  _state = NEW_CACHE_WRITE_HEADER;

  if (m_alt_index >= 0) {
    //it's an update, remove the stale httpinfo 
    _httpInfoVector.remove(m_alt_index, false);
  }

  if (_writeCacheHttpInfo.valid()) {
    //remove if alts > max_alts
    if (cache_config_http_max_alts > 1 && _httpInfoVector.count() >= cache_config_http_max_alts) {
      _httpInfoVector.remove(0, true);
    }
    // set the size and key of the object
    _writeCacheHttpInfo.object_size_set(_totalObjectSize);
    _writeCacheHttpInfo.object_key_set(_cacheKey);

    _httpInfoVector.insert(&_writeCacheHttpInfo);

    _writeCacheHttpInfo.clear();        //prevent double destroy   


    // get the length of the marshaled vector and create a buffer for it
    _httpInfoBuffer = new_MIOBuffer();
    _httpInfoBufferReader = _httpInfoBuffer->alloc_reader();

    // tmp buffer since we can't marshal into a miobuffer
    if ((_size = _httpInfoVector.marshal_length()) > 0) {
      void *buffer[_size];

      _httpInfoVector.marshal((char *) buffer, _size);
      _httpInfoBuffer->write((const char *) buffer, (int) _size);
    }
    //setCtrlInPlugin(true);
    _cacheWriteHook->invoke(INK_EVENT_CACHE_WRITE_HEADER, this);
  } else {
    Debug("cache_plugin", "[NewCacheVC::_writeHttpInfo] httpinfo not valid");
  }
}


//----------------------------------------------------------------------------
void
NewCacheVC::_free()
{
  Debug("cache_plugin", "[NewCacheVC::_free] %lX", (long) this);
  if (this->freeCalled)
    return;

  //send close event to allow plugin to free buffers
  //setCtrlInPlugin(true);
  _cacheReadHook->invoke(INK_EVENT_CACHE_CLOSE, this);

//  if(_vio.op == VIO::READ)
//    _cacheWriteHook->invoke(INK_EVENT_CACHE_READ_UNLOCK, this);
//  else if(_vio.op == VIO::WRITE)
//         _cacheWriteHook->invoke(INK_EVENT_CACHE_WRITE_UNLOCK,this);
//      else if(_vio.op == VIO::SHUTDOWN_READWRITE)
  //             _cacheWriteHook->invoke(INK_EVENT_CACHE_DELETE_UNLOCK,this);

  //CACHE_DECREMENT_DYN_STAT(this->base_stat + CACHE_STAT_ACTIVE);
//   if (this->closed > 0) {
//     CACHE_INCREMENT_DYN_STAT(this->base_stat + CACHE_STAT_SUCCESS);
//   } // else abort,cancel
  EThread *t = mutex->thread_holding;

//  ink_assert(!this->is_io_in_progress());
//  ink_assert(!this->od);
  /* calling this->io.action = NULL causes compile problem on 2.6 solaris
     release build....wierd??? For now, null out thisinuation and mutex
     of the action separately */
//   this->io.action.thisinuation = NULL;
//   this->io.action.mutex = NULL;
//   this->io.mutex.clear();
//   this->io.aio_result = 0;
//   this->io.aiocb.aio_nbytes = 0;
  cancel_trigger();
  // if (this->_url)
  //   xfree(_url);
  this->_request.destroy();
  //this->vector.clear();
  this->_vio.buffer.clear();
  this->_vio.mutex.clear();
  this->_readCacheHttpInfo.destroy();
  this->_writeCacheHttpInfo.destroy();
  this->_httpInfoVector.clear(true);

  //this->_action.cancelled = 0;
  //this->_action.mutex.clear();
  this->mutex.clear();

  if (this->_httpInfoBuffer)
    free_MIOBuffer(this->_httpInfoBuffer);
  if (this->m_ranges)
    delete[]this->m_ranges;
  if (this->m_overflow_unmarshal_buf)
    xfree(m_overflow_unmarshal_buf);

  //this->buf.clear();
  //this->first_buf.clear();
  //this->blocks.clear();
  //this->writer_buf.clear();
  //this->alternate_index = CACHE_ALT_INDEX_DEFAULT;

  //memset((char *)&this->_vio, 0, this->size_to_init);

  SET_HANDLER(&NewCacheVC::dead);

  THREAD_FREE_TO(this, newCacheVConnectionAllocator, t, MAX_CACHE_VCS_PER_THREAD);
  this->freeCalled = true;
}


//----------------------------------------------------------------------------
int
NewCacheVC::dead(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);
  ink_assert(0);
  return EVENT_DONE;
}
