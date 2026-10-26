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

#include "iocore/eventsystem/EventSystem.h"
#include "proxy/hdrs/HTTP.h"
#include "records/RecordsConfig.h"
#include "tscore/Layout.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define kMinInputLength 3
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
  // Disable the per-thread free lists once, before anything starts: the
  // flag is read by THREAD_FREE during event cleanup on the event thread,
  // so a per-input write from the fuzz thread would race that read.
  cmd_disable_pfreelist = true;

  Layout::create();
  RecProcessInit();
  LibRecordsConfigInit();

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(TEST_THREADS);

  // The thread libFuzzer calls in on is not an event thread; register it so
  // that the header allocators see a valid thread instead of dereferencing a
  // null one, exactly like src/proxy/http3/test/main_qpack.cc does.
  Thread *main_thread = new EThread;
  main_thread->set_specific();

  // Set up the well-known header token tables; without them a recognized
  // field name decodes to a null pointer with a non-zero length.
  url_init();
  mime_init();
  http_init();

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

// QPACK delivers a decode's result on the thread decode() started on, and
// keeps undelivered results on lists only that thread touches.  Dispatch
// calls onto a started event thread and block the caller on a condition
// variable, the same way run_on_event_thread() in
// src/proxy/http3/test/test_QPACK.cc does.
struct DecodeCallResult {
  std::mutex              m;
  std::condition_variable cv;
  int                     value = 0;
  bool                    done  = false;

  void
  complete(int v)
  {
    {
      std::lock_guard<std::mutex> lock(m);
      value = v;
      done  = true;
    }
    cv.notify_one();
  }

  // Notification-based wait: returns false when the event thread does not
  // complete the call within @a timeout, without polling at a fixed interval.
  template <typename Rep, typename Period>
  bool
  wait_for_completion(const std::chrono::duration<Rep, Period> &timeout)
  {
    std::unique_lock<std::mutex> lock(m);
    return cv.wait_for(lock, timeout, [this] { return done; });
  }
};

class DecodeCall : public Continuation
{
public:
  DecodeCall(std::function<int()> call, std::shared_ptr<DecodeCallResult> result)
    : Continuation(new_ProxyMutex()), _call(std::move(call)), _result(std::move(result))
  {
    SET_HANDLER(&DecodeCall::run);
  }

  int
  run(int /* event ATS_UNUSED */, Event * /* event ATS_UNUSED */)
  {
    _result->complete(_call());
    delete this;
    return 0;
  }

private:
  std::function<int()>              _call;
  std::shared_ptr<DecodeCallResult> _result;
};

int
run_on_event_thread(std::function<int()> call)
{
  auto result = std::make_shared<DecodeCallResult>();

  eventProcessor.all_ethreads[0]->schedule_imm(new DecodeCall(std::move(call), result));
  if (!result->wait_for_completion(std::chrono::seconds(5))) {
    // The queued call still refers to the caller's locals, so unwinding past
    // them would let it run on freed stack.
    std::fprintf(stderr, "event thread stalled\n");
    std::abort();
  }
  return result->value;
}

// Run a decode and, in the same event-thread handoff, cancel the stream.
// QPACK reports a decode's result on the thread decode() started on, and
// cancel() drops anything still queued or blocked for the stream, so once
// this handoff returns no event can still point at the input's HTTPHdr when
// the input is destroyed.  One handoff per input instead of two keeps the
// event-thread round trip off the fast path once instead of twice.
int
decode_and_cancel_on_event_thread(QPACK &qpack, uint64_t stream_id, const uint8_t *header_block, size_t header_block_len,
                                  HTTPHdr &hdr, Continuation *cont)
{
  return run_on_event_thread([&] {
    int const result = qpack.decode(stream_id, header_block, header_block_len, hdr, cont);
    qpack.cancel(stream_id);
    return result;
  });
}

// The outcome of a decode whose result is delivered as an event.  decode()
// returns 0 both for decodes that fully succeed and for decodes that a size
// limit fails, so the delivered event is the only full-success signal.  done
// is set once the result has been delivered and the stream cleaned up on the
// event thread.
struct DecodeOutcome {
  std::mutex              m;
  std::condition_variable cv;
  int                     result   = 0;
  bool                    complete = false;
  bool                    done     = false;

  void
  finish(int decode_result, bool decode_complete)
  {
    {
      std::lock_guard<std::mutex> lock(m);
      result   = decode_result;
      complete = decode_complete;
      done     = true;
    }
    cv.notify_one();
  }

  // Notification-based wait: returns false when the event thread does not
  // finish within @a timeout, without polling at a fixed interval.
  template <typename Rep, typename Period>
  bool
  wait_for_completion(const std::chrono::duration<Rep, Period> &timeout)
  {
    std::unique_lock<std::mutex> lock(m);
    return cv.wait_for(lock, timeout, [this] { return done; });
  }
};

// The continuation a round trip's decode result is delivered to.  QPACK
// schedules the result on the thread decode() ran on, so this runs there:
// it records which event arrived, applies the stream cleanup on that same
// thread, as QPACK requires for cancel(), and only then releases the waiting
// fuzz thread.  Delivery and cleanup therefore happen inside the same
// event-thread handoff, and nothing stays queued when the input's objects
// are destroyed.
class RecordingDecodeCont : public Continuation
{
public:
  RecordingDecodeCont(QPACK *qpack, uint64_t stream_id, std::shared_ptr<DecodeOutcome> outcome)
    : Continuation(new_ProxyMutex()), _qpack(qpack), _stream_id(stream_id), _outcome(std::move(outcome))
  {
    SET_HANDLER(&RecordingDecodeCont::on_event);
  }

  // decode() queues no result for a nonzero return, so the caller records
  // the return value here; the report that carries the event can only run
  // after this handoff returns, on this same thread.
  void
  set_result(int result)
  {
    _result = result;
  }

  int
  on_event(int event, Event * /* event ATS_UNUSED */)
  {
    // A round trip can never be blocked, so cancel() has nothing left to
    // drop here; it only guarantees the stream is left clean.
    _qpack->cancel(_stream_id);
    _outcome->finish(_result, event == QPACK_EVENT_DECODE_COMPLETE);
    delete this;
    return EVENT_DONE;
  }

private:
  QPACK                               *_qpack;
  uint64_t const                       _stream_id;
  std::shared_ptr<DecodeOutcome> const _outcome;
  int                                  _result = 0;
};

// Run a decode on the event thread and capture its delivered result: decode()
// returns 0 even when decoding fails, with the failure arriving as
// QPACK_EVENT_DECODE_FAILED, so a round trip needs the event to tell full
// success from failure.  Returns decode()'s return value and sets @a
// decode_complete to whether the decode reported success.  The result is
// delivered, and the stream cleaned up, inside the same event-thread
// handoff.
int
decode_and_capture_on_event_thread(QPACK &qpack, uint64_t stream_id, const uint8_t *header_block, size_t header_block_len,
                                   HTTPHdr &hdr, bool &decode_complete)
{
  auto outcome = std::make_shared<DecodeOutcome>();

  eventProcessor.all_ethreads[0]->schedule_imm(new DecodeCall(
    [&] {
      auto     *cont   = new RecordingDecodeCont(&qpack, stream_id, outcome);
      int const result = qpack.decode(stream_id, header_block, header_block_len, hdr, cont);
      if (result != 0) {
        // decode() queues no completion report for a nonzero return, so the
        // recording continuation would never run; release the caller here.
        // Should a report still be queued (an invalid instance), its handler
        // cleans the stream up later and finds nothing left to drop.
        outcome->finish(result, false);
      } else {
        cont->set_result(result);
      }
      return result;
    },
    std::make_shared<DecodeCallResult>()));
  if (!outcome->wait_for_completion(std::chrono::seconds(5))) {
    // The queued calls still refer to the caller's locals, so unwinding past
    // them would let it run on freed stack.
    std::fprintf(stderr, "event thread stalled\n");
    std::abort();
  }
  decode_complete = outcome->complete;
  return outcome->result;
}

} // namespace

// QPACK.h grants its friend QPACKTestAccess the encoder-stream read path, the
// only place a Set Dynamic Table Capacity instruction is applied.  The fuzz
// harness defines its own so it can apply the instruction a peer's encoder
// stream would carry: without a live QUIC connection there is no other way to
// give the dynamic table capacity.  This mirrors read_encoder_stream() in
// src/proxy/http3/test/test_QPACK.cc.
struct QPACKTestAccess {
  static void
  apply_dynamic_table_capacity(QPACK &qpack, uint16_t capacity)
  {
    uint8_t instruction[16];
    instruction[0] = 0x20; // Set Dynamic Table Capacity

    int64_t const len = xpack_encode_integer(instruction, instruction + sizeof(instruction), capacity, 5);
    if (len < 0) {
      // Unreachable for a 16-bit capacity and a 16 byte buffer.
      std::fprintf(stderr, "encoding the capacity instruction failed\n");
      std::abort();
    }

    MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = buffer->alloc_reader();

    buffer->write(instruction, len);
    qpack._on_encoder_stream_read_ready(*reader);
    free_MIOBuffer(buffer);
  }
};

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 0;
  }

  static bool          Initialized = DoInitialization();
  static QPACKTestCont test_cont;

  // The first byte drives the QPACK configuration, mirroring the local
  // settings a HTTP/3 endpoint advertises through SETTINGS rather than any
  // negotiated value.
  uint8_t const  settings  = input_data[0];
  uint64_t const stream_id = (settings >> 7) & 0x1;
  // {0, 256, 512, 1024}, including the default dynamic table size of zero.
  constexpr uint32_t kSizes[]       = {0, 256, 512, 1024};
  uint16_t const     max_table_size = static_cast<uint16_t>(kSizes[(settings >> 5) & 0x3]);
  // A limit of zero would refuse every header set before an encode round
  // trip could compare anything, so the two size limits are drawn from
  // non-zero values only; the small value keeps the limit-refusal paths
  // within reach of ordinary fields.
  constexpr uint32_t kLimitSizes[]          = {64, 256, 512, 1024};
  uint32_t const     header_field_max_size  = kLimitSizes[(settings >> 3) & 0x3];
  uint32_t const     max_field_section_size = kLimitSizes[(settings >> 1) & 0x3];
  bool const         encode_mode            = (settings & 0x1) != 0;

  HTTPHdr headers;
  headers.create(HTTPType::REQUEST);

  // The harness never services QPACK's encoder and decoder streams (that
  // needs a live QUIC connection), so max blocking streams is zero by
  // harness choice: a decode that would wait on a peer is refused instead of
  // stalling, and every per-input QPACK instance stays free of blocked
  // DecodeRequests.
  QPACK qpack(nullptr, max_field_section_size, max_table_size, 0, header_field_max_size);

  if (encode_mode) {
    // The encoder and decoder share this instance's dynamic table, so one
    // Set Dynamic Table Capacity instruction, applied the way a peer's
    // encoder stream would deliver it, turns the table on for both sides.
    // Without it the table never grows: QPACK starts it at capacity zero and
    // changes it only through that instruction (RFC 9204 section 3.2.3),
    // which would leave max_table_size doing nothing and encode mode never
    // touching the table.
    QPACKTestAccess::apply_dynamic_table_capacity(qpack, max_table_size);

    // Compress a header set built from the input and decode the produced
    // header block again on the same instance (round trip).
    std::string payload(reinterpret_cast<char const *>(input_data) + 1, size_data - 1);
    FillHeaders(payload, headers);

    if (headers.fields_count() > 0) {
      MIOBuffer *header_block     = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
      uint64_t   header_block_len = 0;
      // Allocate the reader before encoding so that it starts at the two byte
      // stream header prefix; allocated afterwards it would only see the tail
      // of the block and decode() would never run.
      IOBufferReader *reader = header_block->alloc_reader();
      if (qpack.encode(stream_id, headers, header_block, header_block_len) == 0 && header_block_len > 0) {
        std::vector<uint8_t> block(header_block_len, 0);
        if (reader->read_avail() >= static_cast<int64_t>(header_block_len)) {
          reader->memcpy(block.data(), header_block_len);

          HTTPHdr decoded;
          decoded.create(HTTPType::REQUEST);

          // Run the decode on a started event thread: QPACK delivers its
          // result on the thread the decode started on and keeps undelivered
          // results on per-thread lists, so decode() and the result delivery
          // have to run there, as decode_on_event_thread() in
          // src/proxy/http3/test/test_QPACK.cc does.  The caller blocks on a
          // mutex and a condition variable until the result has been
          // delivered and the stream cleaned up on that thread.
          bool decode_complete = false;
          int  decode_result =
            decode_and_capture_on_event_thread(qpack, stream_id, block.data(), block.size(), decoded, decode_complete);

          // This instance produced the block itself, and every dynamic table
          // entry the block refers to was inserted into the table before
          // encode() returned, so its prefix cannot name entries the decoder
          // lacks and the decode can never legitimately block.  A nonzero
          // decode_result (a prefix decode() cannot parse, a blocked decode,
          // or an invalid instance) is therefore a codec failure.  Size
          // limits are the one legitimate refusal decode() does not report
          // through its return value: a field or the section exceeding the
          // receiver's own local limits fails the decode through
          // QPACK_EVENT_DECODE_FAILED while decode() still returns 0, and
          // those limits apply to decoded bytes, which Huffman coding can
          // grow beyond the encoded block, so the encoded size does not
          // bound them.  The gate below skips the compare for headers the
          // limits cannot cover, and inside the gate the delivered event must
          // be success, so a failure that arrives only as an event cannot
          // pass the compare unnoticed either.
          if (decode_result != 0) {
            fprintf(stderr, "QPACK encode round trip: decode() rejected its own block, decode_result=%d\n", decode_result);
            abort();
          }
          uint32_t max_field_decoded = 0;
          uint64_t total_decoded     = 0;
          for (auto &field : headers) {
            uint32_t const field_size  = static_cast<uint32_t>(field.name_get().size() + field.value_get().size());
            max_field_decoded          = std::max(max_field_decoded, field_size);
            total_decoded             += field_size;
          }
          if (max_field_decoded <= header_field_max_size && total_decoded <= max_field_section_size) {
            // Within these limits decoding cannot legitimately fail, and a
            // failure would only be visible as QPACK_EVENT_DECODE_FAILED --
            // decode() itself still returns 0 -- so require the success
            // event before comparing; otherwise a codec failure that attaches
            // every field before failing would slip past the compare.
            if (!decode_complete) {
              fprintf(stderr, "QPACK encode round trip: decode reported failure, decode_result=%d\n", decode_result);
              abort();
            }
            // The encoder lowercases field names, so compare names without
            // case; values must match exactly.
            auto ieq = [](std::string_view a, std::string_view b) {
              return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                       return std::tolower(x) == std::tolower(y);
                     });
            };
            bool match = decoded.fields_count() == headers.fields_count();
            auto e     = headers.begin();
            auto d     = decoded.begin();
            for (; match && e != headers.end() && d != decoded.end(); ++e, ++d) {
              match = ieq(e->name_get(), d->name_get()) && e->value_get() == d->value_get();
            }
            if (!match) {
              // Print enough context for an OSS-Fuzz report to be triaged
              // from the log alone.
              fprintf(stderr, "QPACK round trip lost fields: settings=0x%02x decode_result=%d, %zu of %zu fields decoded\n",
                      settings, decode_result, static_cast<size_t>(decoded.fields_count()),
                      static_cast<size_t>(headers.fields_count()));
              auto   e2  = headers.begin();
              auto   d2  = decoded.begin();
              size_t pos = 0;
              for (; e2 != headers.end() && d2 != decoded.end(); ++e2, ++d2, ++pos) {
                if (!(ieq(e2->name_get(), d2->name_get()) && e2->value_get() == d2->value_get())) {
                  fprintf(stderr, "first differing field at position %zu: expected \"%.*s: %.*s\", got \"%.*s: %.*s\"\n", pos,
                          static_cast<int>(e2->name_get().size()), e2->name_get().data(), static_cast<int>(e2->value_get().size()),
                          e2->value_get().data(), static_cast<int>(d2->name_get().size()), d2->name_get().data(),
                          static_cast<int>(d2->value_get().size()), d2->value_get().data());
                  break;
                }
              }
              abort();
            }
          }
          decoded.destroy();
        }
        header_block->dealloc_reader(reader);
      }
      free_MIOBuffer(header_block);
    }
  } else {
    // Feed the payload directly to the decoder: this is the compressed
    // header block a malicious HTTP/3 peer controls on the wire.  Run and
    // cancel it on the event thread as in encode mode, before this input's
    // HTTPHdr goes away.
    decode_and_cancel_on_event_thread(qpack, stream_id, input_data + 1, size_data - 1, headers, &test_cont);

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
