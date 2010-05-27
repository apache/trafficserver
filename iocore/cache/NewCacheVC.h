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

#ifndef __NEWCACHEVC_H__
#define __NEWCACHEVC_H__

#include "inktomi++.h"
//#include "api/include/ts.h"
//#include "I_Cache.h"
#include "P_Cache.h"
#ifdef HTTP_CACHE
#include "HTTP.h"
#include "P_CacheHttp.h"
#endif

class APIHook;
class HttpCacheSM;
class HttpTunnel;

//----------------------------------------------------------------------------

class NewCacheVC:public CacheVConnection
{
public:
  NewCacheVC():_offset(0), _size(0), _lookupUrl(NULL), _url(NULL), _url_length(0),
    _cacheWriteHook(NULL), _cacheReadHook(NULL), _sm(NULL), _httpTunnel(NULL),
    _state(NEW_CACHE_LOOKUP),
    _httpInfoBuffer(NULL), _httpInfoBufferReader(NULL), _totalObjectSize(0),
    m_unsatisfiable_range(false), m_not_handle_range(false),
    m_range_present(false), m_range_csv_present(false), m_content_length(0),
    m_num_chars_for_cl(0), m_num_range_fields(0), m_current_range(0),
    m_current_range_idx(0), m_content_type(NULL), m_content_type_len(0),
    m_ranges(NULL), m_output_cl(0), m_done(0), m_range_field(NULL),
    m_range_hdr_valid(false), closed(false), freeCalled(false),
    m_overflow_unmarshal_buf(NULL), m_overflow_unmarshal_buf_size(0), m_alt_index(-1), trigger(NULL)
  {
    m_unmarshal_buf[0] = '\0';
  }                             // should we hide this, since we have the alloc?

  enum NewCacheVcState
  {
    NEW_CACHE_LOOKUP,
    NEW_CACHE_READ_DATA,
    NEW_CACHE_READ_DATA_APPEND,
    NEW_CACHE_WRITE_HEADER,
    NEW_CACHE_WRITE_DATA,
    NEW_CACHE_WRITE_DATA_APPEND
  };

  // static method to allocate a new cache vc
  static NewCacheVC *alloc(Continuation * cont, URL * url, HttpCacheSM * sm);
  void setWriteVC(CacheHTTPInfo * old_info);
  VIO *do_io_read(Continuation * c, int64 nbytes, MIOBuffer * buf);
  VIO *do_io_write(Continuation * c, int64 nbytes, IOBufferReader * buf, bool owner = false);
  void do_io_close(int lerrno = -1);

  void reenable(VIO * vio);
  void reenable_re(VIO * vio);

  // http info setters and getters
  void set_http_info(CacheHTTPInfo * info);
  void set_cache_http_hdr(HTTPHdr * request);

  void get_http_info(CacheHTTPInfo ** info);

  bool is_ram_cache_hit()
  {
    return true;
  }
  Action *action()
  {
    return &_action;
  }
  bool set_pin_in_cache(time_t time_pin)
  {
    ink_assert(!"implemented");
    return false;
  }
  bool set_disk_io_priority(int priority)
  {
    ink_assert(!"implemented");
    return false;
  }
  time_t get_pin_in_cache()
  {
    ink_assert(!"implemented");
    return 0;
  }
  int
  get_disk_io_priority()
  {
    ink_assert(!"implemented");
    return 0;
  }
  int get_header(void **ptr, int *len)
  {
    ink_assert(!"implemented");
    return -1;
  }
  int set_header(void *ptr, int len)
  {
    ink_assert(!"implemented");
    return -1;
  }
  int get_single_data(void **ptr, int *len)
  {
    ink_assert(!"implemented");
    return -1;
  }
  int get_object_size()
  {
    ink_assert(!"implemented");
    return -1;
  }
  VIO *do_io_pread(Continuation *c, int64 nbytes, MIOBuffer *buf, int64 offset) {
    ink_assert(!"implemented");
    return 0;
  }

  bool appendCacheHttpInfo(const void *data, const uint64 size);
  bool completeCacheHttpInfo(const void *data, const uint64 size);
  // inialize the cache vc as a writer or a reader
  int writerInit();
  int readerInit();

  // getter for the URL
  URL *get_lookup_url()
  {
    return _lookupUrl;
  }
  VIO *getVio()
  {
    return &_vio;
  }

  // setter and getter for the cache state machine
  void setCacheSm(HttpCacheSM * sm)
  {
    _sm = sm;
  }
  void setConfigParams(CacheLookupHttpConfig * params)
  {
    _params = params;
  }
  HttpCacheSM *getCacheSm() const
  {
    return _sm;
  }
  HttpTunnel *getTunnel() const
  {
    return _httpTunnel;
  }

  // called from the ink api when getting the cache key
  void getCacheKey(void **key, int *length);
  void getCacheHeaderKey(void **key, int *length);

  // called from the ink api when getting the buffer informaiton
  //void getCacheBufferInfo( void ** buf ,uint64 *size, uint64 *offset);
  void getCacheBufferInfo(uint64 * size, uint64 * offset);
  IOBufferReader *getBufferReader();

  // set the total size of the data object in cache
  void setTotalObjectSize(const uint64 size)
  {
    _totalObjectSize = size;
  }

  // use to get the state of the cache vc
  NewCacheVcState getState() const
  {
    return _state;
  }

  // callback handlers
  int handleLookup(int event, Event * e);
  int handleRead(int event, Event * e);
  int handleWrite(int event, Event * e);
  int dead(int event, Event * e);
  bool setRangeAndSize(uint64 size);
  void doRangeSetup();
  void parseRange();
  void calculateCl();
  void modifyRespHdr();
  void add_boundary(bool end);
  void add_sub_header(int index);
  //bool getCtrlInPlugin() { return ctrlInPlugin; }
  //void setCtrlInPlugin(bool value) {  ctrlInPlugin= value; }
  bool isClosed()
  {
    return closed;
  }
  void free()
  {
    _free();
  }
private:

  void cancel_trigger()
  {
    if (trigger) {
      trigger->cancel_action();
      trigger = NULL;
    }
  }

  void _append_unmarshal_buf(const void *data, const uint64 size);
  void _setup_read();
  void _writeHttpInfo();
  void _free();

  VIO _vio;
  Action _action;
  CacheHTTPInfo _readCacheHttpInfo;
  CacheHTTPInfo _writeCacheHttpInfo;
  CacheHTTPInfoVector _httpInfoVector;

  HTTPHdr _request;

  // information for reading and writing to the cache plugin
  // XXX I don't think these are needed anymore since we are using MIOBuffer now
  uint64 _offset;
  uint64 _size;

  URL *_lookupUrl;
  char *_url;
  int _url_length;

  APIHook *_cacheWriteHook;
  APIHook *_cacheReadHook;
  CacheLookupHttpConfig *_params;
  HttpCacheSM *_sm;
  HttpTunnel *_httpTunnel;

  NewCacheVcState _state;
  CacheKey _cacheKey;
  MIOBuffer *_httpInfoBuffer;
  IOBufferReader *_httpInfoBufferReader;

  uint64 _totalObjectSize;

  // Range related....
  typedef struct _RangeRecord
  {
  _RangeRecord() :
    _start(-1), _end(-1), _done_byte(-1)
    { }

    int64 _start;
    int64 _end;
    int64 _done_byte;
  } RangeRecord;

  bool m_unsatisfiable_range;
  bool m_not_handle_range;
  bool m_range_present;
  bool m_range_csv_present;
  int64 m_content_length;
  int m_num_chars_for_cl;
  int m_num_range_fields;
  int m_current_range;
  int m_current_range_idx;
  const char *m_content_type;
  int m_content_type_len;
  RangeRecord *m_ranges;
  int64 m_output_cl;
  int64 m_done;
  MIMEField *m_range_field;
  bool m_range_hdr_valid;
  //bool ctrlInPlugin;
  bool closed;
  bool freeCalled;
  //Doc *_doc; do we need it?
  char m_unmarshal_buf[8192];
  char *m_overflow_unmarshal_buf;
  size_t m_overflow_unmarshal_buf_size;
  int m_alt_index;
  Event *trigger;
};

extern ClassAllocator<NewCacheVC> newCacheVConnectionAllocator;


#endif // __NEWCACHEVC_H__
