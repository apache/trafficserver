/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <map>

#include "I_EventSystem.h"
#include "I_Event.h"
#include "I_IOBuffer.h"
#include "tscore/Arena.h"
#include "tscpp/util/IntrusiveDList.h"
#include "MIME.h"
#include "HTTP.h"
#include "QUICApplication.h"
#include "QUICConnection.h"

class HTTPHdr;

enum {
  QPACK_EVENT_DECODE_COMPLETE = QPACK_EVENT_EVENTS_START,
  QPACK_EVENT_DECODE_FAILED,
};

class QPACK : public QUICApplication
{
public:
  QPACK(QUICConnection *qc, uint32_t max_header_list_size, uint16_t max_table_size, uint16_t max_blocking_streams);
  virtual ~QPACK();

  int event_handler(int event, Event *data);

  /*
   * header_block must have enough size to store all headers in header_set.
   * The maximum size can be estimated with QPACK::estimate_header_block_size().
   */
  int encode(uint64_t stream_id, HTTPHdr &header_set, MIOBuffer *header_block, uint64_t &header_block_len);

  /*
   * This will emit either of two events below:
   * - QPACK_EVENT_DECODE_COMPLETE (Data: *HTTPHdr)
   * - QPACK_EVENT_DECODE_FAILED (Data: nullptr)
   */
  int decode(uint64_t stream_id, const uint8_t *header_block, size_t header_block_len, HTTPHdr &hdr, Continuation *cont,
             EThread *thread = this_ethread());

  int cancel(uint64_t stream_id);

  void set_encoder_stream(QUICStreamIO *stream_io);
  void set_decoder_stream(QUICStreamIO *stream_io);

  void update_max_header_list_size(uint32_t max_header_list_size);
  void update_max_table_size(uint16_t max_table_size);
  void update_max_blocking_streams(uint16_t max_blocking_streams);

  static size_t estimate_header_block_size(const HTTPHdr &header_set);

private:
  struct LookupResult {
    uint16_t index                                  = 0;
    enum MatchType { NONE, NAME, EXACT } match_type = MatchType::NONE;
  };

  struct Header {
    Header(const char *n, const char *v) : name(n), value(v), name_len(strlen(name)), value_len(strlen(value)) {}
    const char *name;
    const char *value;
    const int name_len;
    const int value_len;
  };

  class StaticTable
  {
  public:
    static const LookupResult lookup(uint16_t index, const char **name, int *name_len, const char **value, int *value_len);
    static const LookupResult lookup(const char *name, int name_len, const char *value, int value_len);

  private:
    static const Header STATIC_HEADER_FIELDS[];
  };

  struct DynamicTableEntry {
    uint16_t index     = 0;
    uint16_t offset    = 0;
    uint16_t name_len  = 0;
    uint16_t value_len = 0;
    uint16_t ref_count = 0;
  };

  class DynamicTableStorage
  {
  public:
    DynamicTableStorage(uint16_t size);
    ~DynamicTableStorage();
    void read(uint16_t offset, const char **name, uint16_t name_len, const char **value, uint16_t value_len) const;
    uint16_t write(const char *name, uint16_t name_len, const char *value, uint16_t value_len);
    void erase(uint16_t name_len, uint16_t value_len);

  private:
    uint16_t _overwrite_threshold = 0;
    uint8_t *_data                = nullptr;
    uint16_t _data_size           = 0;
    uint16_t _head                = 0;
    uint16_t _tail                = 0;
  };

  class DynamicTable
  {
  public:
    DynamicTable(uint16_t size);
    ~DynamicTable();

    const LookupResult lookup(uint16_t index, const char **name, int *name_len, const char **value, int *value_len);
    const LookupResult lookup(const char *name, int name_len, const char *value, int value_len);
    const LookupResult insert_entry(bool is_static, uint16_t index, const char *value, uint16_t value_len);
    const LookupResult insert_entry(const char *name, uint16_t name_len, const char *value, uint16_t value_len);
    const LookupResult duplicate_entry(uint16_t current_index);
    bool should_duplicate(uint16_t index);
    void update_size(uint16_t max_size);
    void ref_entry(uint16_t index);
    void unref_entry(uint16_t index);
    uint16_t largest_index() const;

  private:
    uint16_t _available        = 0;
    uint16_t _entries_inserted = 0;

    // FIXME It may be better to split this array into small arrays to reduce memory footprint
    struct DynamicTableEntry *_entries = nullptr;
    uint16_t _max_entries              = 0;
    uint16_t _entries_head             = 0;
    uint16_t _entries_tail             = 0;
    DynamicTableStorage *_storage      = nullptr;
  };

  class DecodeRequest
  {
  public:
    DecodeRequest(uint16_t largest_reference, EThread *thread, Continuation *continuation, uint64_t stream_id,
                  const uint8_t *header_block, size_t header_block_len, HTTPHdr &hdr)
      : _largest_reference(largest_reference),
        _thread(thread),
        _continuation(continuation),
        _stream_id(stream_id),
        _header_block(header_block),
        _header_block_len(header_block_len),
        _hdr(hdr)
    {
    }

    uint16_t
    largest_reference() const
    {
      return this->_largest_reference;
    }

    EThread *
    thread()
    {
      return this->_thread;
    }

    Continuation *
    continuation()
    {
      return this->_continuation;
    }

    uint64_t
    stream_id() const
    {
      return this->_stream_id;
    }

    const uint8_t *
    header_block() const
    {
      return this->_header_block;
    }

    size_t
    header_block_len() const
    {
      return this->_header_block_len;
    }

    HTTPHdr &
    hdr()
    {
      return this->_hdr;
    }

    class Linkage
    {
    public:
      static DecodeRequest *&
      next_ptr(DecodeRequest *t)
      {
        return *reinterpret_cast<DecodeRequest **>(&t->_next);
      }
      static DecodeRequest *&
      prev_ptr(DecodeRequest *t)
      {
        return *reinterpret_cast<DecodeRequest **>(&t->_prev);
      }
    };

  private:
    uint16_t _largest_reference;
    EThread *_thread;
    Continuation *_continuation;
    uint64_t _stream_id;
    const uint8_t *_header_block;
    size_t _header_block_len;
    HTTPHdr &_hdr;

    // For IntrusiveDList support
    DecodeRequest *_next = nullptr;
    DecodeRequest *_prev = nullptr;
  };

  struct EntryReference {
    uint16_t smallest;
    uint16_t largest;
  };

  DynamicTable _dynamic_table;
  std::map<uint64_t, struct EntryReference> _references;
  uint32_t _max_header_list_size = 0;
  uint16_t _max_table_size       = 0;
  uint16_t _max_blocking_streams = 0;

  Continuation *_event_handler = nullptr;
  void _resume_decode();
  void _abort_decode();

  bool _invalid = false;

  ts::IntrusiveDList<DecodeRequest::Linkage> _blocked_list;
  bool _add_to_blocked_list(DecodeRequest *decode_request);

  uint16_t _largest_known_received_index = 0;
  void _update_largest_known_received_index_by_insert_count(uint16_t insert_count);
  void _update_largest_known_received_index_by_stream_id(uint64_t stream_id);

  void _update_reference_counts(uint64_t stream_id);

  // Encoder Stream
  int _read_insert_with_name_ref(QUICStreamIO &stream_io, bool &is_static, uint16_t &index, Arena &arena, char **value,
                                 uint16_t &value_len);
  int _read_insert_without_name_ref(QUICStreamIO &stream_io, Arena &arena, char **name, uint16_t &name_len, char **value,
                                    uint16_t &value_len);
  int _read_duplicate(QUICStreamIO &stream_io, uint16_t &index);
  int _read_dynamic_table_size_update(QUICStreamIO &stream_io, uint16_t &max_size);
  int _write_insert_with_name_ref(uint16_t index, bool dynamic, const char *value, uint16_t value_len);
  int _write_insert_without_name_ref(const char *name, int name_len, const char *value, uint16_t value_len);
  int _write_duplicate(uint16_t index);
  int _write_dynamic_table_size_update(uint16_t max_size);

  // Decoder Stream
  int _read_table_state_synchronize(QUICStreamIO &stream_io, uint16_t &insert_count);
  int _read_header_acknowledgement(QUICStreamIO &stream_io, uint64_t &stream_id);
  int _read_stream_cancellation(QUICStreamIO &stream_io, uint64_t &stream_id);
  int _write_table_state_synchronize(uint16_t insert_count);
  int _write_header_acknowledgement(uint64_t stream_id);
  int _write_stream_cancellation(uint64_t stream_id);

  // Request and Push Streams
  int _encode_prefix(uint16_t largest_reference, uint16_t base_index, IOBufferBlock *prefix);
  int _encode_header(const MIMEField &field, uint16_t base_index, IOBufferBlock *compressed_header, uint16_t &referred_index);
  int _encode_indexed_header_field(uint16_t index, uint16_t base_index, bool dynamic_table, IOBufferBlock *compressed_header);
  int _encode_indexed_header_field_with_postbase_index(uint16_t index, uint16_t base_index, bool never_index,
                                                       IOBufferBlock *compressed_header);
  int _encode_literal_header_field_with_name_ref(uint16_t index, bool dynamic_table, uint16_t base_index, const char *value,
                                                 int value_len, bool never_index, IOBufferBlock *compressed_header);
  int _encode_literal_header_field_without_name_ref(const char *name, int name_len, const char *value, int value_len,
                                                    bool never_index, IOBufferBlock *compressed_header);
  int _encode_literal_header_field_with_postbase_name_ref(uint16_t index, uint16_t base_index, const char *value, int value_len,
                                                          bool never_index, IOBufferBlock *compressed_header);

  void _decode(EThread *ethread, Continuation *cont, uint64_t stream_id, const uint8_t *header_block, size_t header_block_len,
               HTTPHdr &hdr);
  int _decode_header(const uint8_t *header_block, size_t header_block_len, HTTPHdr &hdr);
  int _decode_indexed_header_field(int16_t base_index, const uint8_t *buf, size_t buf_len, HTTPHdr &hdr, uint32_t &header_len);
  int _decode_indexed_header_field_with_postbase_index(int16_t base_index, const uint8_t *buf, size_t buf_len, HTTPHdr &hdr,
                                                       uint32_t &header_len);
  int _decode_literal_header_field_with_name_ref(int16_t base_index, const uint8_t *buf, size_t buf_len, HTTPHdr &hdr,
                                                 uint32_t &header_len);
  int _decode_literal_header_field_without_name_ref(const uint8_t *buf, size_t buf_len, HTTPHdr &hdr, uint32_t &header_len);
  int _decode_literal_header_field_with_postbase_name_ref(int16_t base_index, const uint8_t *buf, size_t buf_len, HTTPHdr &hdr,
                                                          uint32_t &header_len);

  // Utilities
  uint16_t _calc_absolute_index_from_relative_index(uint16_t base_index, uint16_t relative_index);
  uint16_t _calc_absolute_index_from_postbase_index(uint16_t base_index, uint16_t postbase_index);
  uint16_t _calc_relative_index_from_absolute_index(uint16_t base_index, uint16_t absolute_index);
  uint16_t _calc_postbase_index_from_absolute_index(uint16_t base_index, uint16_t absolute_index);
  void _attach_header(HTTPHdr &hdr, const char *name, int name_len, const char *value, int value_len, bool never_index);

  int _on_read_ready(QUICStreamIO &stream_io);
  int _on_decoder_stream_read_ready(QUICStreamIO &stream_io);
  int _on_encoder_stream_read_ready(QUICStreamIO &stream_io);

  int _on_write_ready(QUICStreamIO &stream_io);
  int _on_decoder_write_ready(QUICStreamIO &stream_io);
  int _on_encoder_write_ready(QUICStreamIO &stream_io);

  // Stream numbers
  // FIXME How are these stream ids negotiated? In interop, encoder stream id have to be 0 and decoder stream id must not be used.
  uint64_t _encoder_stream_id = 0;
  uint64_t _decoder_stream_id = 9999;

  // Chain of sending instructions
  MIOBuffer *_encoder_stream_sending_instructions;
  MIOBuffer *_decoder_stream_sending_instructions;
  IOBufferReader *_encoder_stream_sending_instructions_reader;
  IOBufferReader *_decoder_stream_sending_instructions_reader;
};
