/** @file

   fuzzing proxy/http3 QPACK

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the
   License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
   express or implied. See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "proxy/http3/QPACK.h"
#include "proxy/http3/Http3Config.h"

#include "records/RecordsConfig.h"
#include "tscore/Layout.h"

#include <string>
#include <vector>

#define kMinInputLength 4
#define kMaxInputLength 1024

#define TEST_THREADS 1

extern int cmd_disable_pfreelist;

namespace
{
// QPACK::decode() hands its result to a Continuation through an event that is
// scheduled on an event thread.  The continuation therefore has to live for
// the entire process and must never touch the event payload, which points at
// per-input objects.
class QPACKTestCont : public Continuation
{
public:
  QPACKTestCont() : Continuation(new_ProxyMutex()) { SET_HANDLER(&QPACKTestCont::on_event); }

  int
  on_event(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    return EVENT_DONE;
  }
};

bool
DoInitialization()
{
  Layout::create();
  RecProcessInit();
  LibRecordsConfigInit();

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(TEST_THREADS);
  ts::Http3Config::startup();

  return true;
}

// Add every "name:value" line of a payload to a HTTPHdr.
void
FillHeaders(std::string_view payload, HTTPHdr &hdr)
{
  size_t pos = 0;
  while (pos < payload.size()) {
    size_t eol  = payload.find('\n', pos);
    size_t next = (eol == payload.npos) ? payload.size() : eol + 1;
    auto   line = payload.substr(pos, ((eol == payload.npos) ? payload.size() : eol) - pos);
    pos         = next;

    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    auto colon = line.find(':');
    if (colon == line.npos || colon == 0) {
      continue;
    }
    auto name  = line.substr(0, colon);
    auto value = line.substr(colon + 1);
    if (name.empty()) {
      continue;
    }

    MIMEField *field = hdr.field_create();
    field->name_set(hdr.m_heap, hdr.m_mime, name);
    field->value_set(hdr.m_heap, hdr.m_mime, value);
    hdr.field_attach(field);
  }
}

} // namespace

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 0;
  }

  cmd_disable_pfreelist = true;

  static bool          Initialized = DoInitialization();
  static QPACKTestCont test_cont;
  EThread             *ethread = eventProcessor.assign_thread(ET_CALL);

  // The first byte drives the QPACK configuration, mirroring the parameters a
  // HTTP/3 endpoint negotiates through SETTINGS.
  uint8_t const  settings               = input_data[0];
  uint64_t const stream_id              = (settings >> 7) & 0x1;
  uint16_t const max_table_size         = (1u << ((settings >> 5) & 0x3)) << 7; // {0, 256, 512, 1024}
  uint32_t const header_field_max_size  = (1u << ((settings >> 3) & 0x3)) << 7; // {0, 256, 512, 1024}
  uint32_t const max_field_section_size = (1u << ((settings >> 1) & 0x3)) << 7; // {0, 256, 512, 1024}
  bool const     encode_mode            = (settings & 0x1) != 0;

  HTTPHdr headers;
  headers.create(HTTPType::REQUEST);

  // A real peer can only grow the dynamic table through encoder stream
  // instructions, which need a live QUIC connection.  Blocked decoding is
  // therefore always rejected (max blocking streams == 0), which keeps every
  // per-input QPACK instance free of blocked DecodeRequests.
  QPACK qpack(nullptr, max_field_section_size, max_table_size, 0, header_field_max_size);

  if (encode_mode) {
    // Compress a header set built from the input and decode the produced
    // header block again on the same instance (round trip).
    std::string payload(reinterpret_cast<char const *>(input_data) + 1, size_data - 1);
    FillHeaders(payload, headers);

    if (headers.fields_count() > 0) {
      MIOBuffer *header_block     = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
      uint64_t   header_block_len = 0;
      if (qpack.encode(stream_id, headers, header_block, header_block_len) == 0 && header_block_len > 0) {
        IOBufferReader      *reader = header_block->alloc_reader();
        std::vector<uint8_t> block(header_block_len, 0);
        if (reader->read_avail() >= static_cast<int64_t>(header_block_len)) {
          reader->memcpy(block.data(), header_block_len);

          HTTPHdr decoded;
          decoded.create(HTTPType::REQUEST);
          qpack.decode(stream_id, block.data(), block.size(), decoded, &test_cont, ethread);

          // With both limits large enough for the input the round trip has
          // to reproduce every field; anything else is a codec bug.
          if (header_field_max_size >= size_data && max_field_section_size >= size_data &&
              decoded.fields_count() != headers.fields_count()) {
            fprintf(stderr, "QPACK round trip lost fields: %zu of %zu decoded\n", static_cast<size_t>(decoded.fields_count()),
                    static_cast<size_t>(headers.fields_count()));
            abort();
          }
          decoded.destroy();
        }
        header_block->dealloc_reader(reader);
      }
      free_MIOBuffer(header_block);
    }
  } else {
    // Feed the payload directly to the decoder: this is the compressed
    // header block a malicious HTTP/3 peer controls on the wire.
    qpack.decode(stream_id, input_data + 1, size_data - 1, headers, &test_cont, ethread);

    // Touch the decoded fields.
    size_t total = 0;
    for (auto &field : headers) {
      total += field.name_get().size() + field.value_get().size();
    }
    if (total == static_cast<size_t>(-1)) {
      abort(); // unreachable
    }
  }

  headers.destroy();

  return 0;
}
