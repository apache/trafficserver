// Copyright 2013 We-Amp B.V.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: oschaaf@we-amp.com (Otto van der Schaaf)

#include "ats_base_fetch.h"

#include <ts/ts.h>

#include "ats_server_context.h"

#include "net/instaweb/util/public/string_util.h"
#include "net/instaweb/util/public/string_writer.h"
#include "net/instaweb/util/public/google_message_handler.h"


using namespace net_instaweb;

// TODO(oschaaf): rename is_resource_fetch -> write_raw_response_headers
AtsBaseFetch::AtsBaseFetch(AtsServerContext* server_context,
                           const net_instaweb::RequestContextPtr& request_ctx,
                           TSVIO downstream_vio, TSIOBuffer downstream_buffer, bool is_resource_fetch) :
  AsyncFetch(request_ctx),
  server_context_(server_context),
  done_called_(false),
  last_buf_sent_(false),
  references_(2),
  downstream_vio_(downstream_vio),
  downstream_buffer_(downstream_buffer),
  is_resource_fetch_(is_resource_fetch),
  downstream_length_(0),
  txn_mutex_(TSVIOMutexGet(downstream_vio)) {
  buffer_.reserve(1024 * 32);
}

AtsBaseFetch::~AtsBaseFetch() {
  CHECK(references_ == 0);
}

// Should be called from the event loop,
// and thus with the txn mutex held by ATS
void AtsBaseFetch::Release() {
  DecrefAndDeleteIfUnreferenced();
}

void AtsBaseFetch::Lock(){
  TSMutexLock(txn_mutex_);
}

void AtsBaseFetch::Unlock() {
  TSMutexUnlock(txn_mutex_);
}

bool AtsBaseFetch::HandleWrite(const StringPiece& sp, net_instaweb::MessageHandler* handler) {
  ForwardData(sp, false, false);
  return true;
}

bool AtsBaseFetch::HandleFlush( net_instaweb::MessageHandler* handler ) {
  ForwardData("", true, false);
  return true;
}

void AtsBaseFetch::HandleHeadersComplete() {
  // oschaaf: ATS will currently send its response headers
  // earlier than this will fire. So this has become a no-op.
  // This implies that we can't support convert_meta_tags
  TSDebug("ats-speed", "HeadersComplete()!");
  // For resource fetches, we need to output the headers in raw HTTP format.
  if (is_resource_fetch_) {
    GoogleMessageHandler mh;
    GoogleString s;
    StringWriter string_writer(&s);
    response_headers()->Add("Connection", "Close");
    response_headers()->WriteAsHttp(&string_writer, &mh);
    ForwardData(StringPiece(s.data(),s.size()), true, false);
  }
}

void AtsBaseFetch::ForwardData(const StringPiece& sp, bool reenable, bool last) {
  TSIOBufferBlock downstream_blkp;
  char *downstream_buffer;
  int64_t downstream_length;
  int64_t to_write = sp.size();

  Lock();
  if (references_ == 2) {
    while (to_write > 0) {
      downstream_blkp = TSIOBufferStart(downstream_buffer_);
      downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);
      int64_t bytes_written = to_write > downstream_length ? downstream_length : to_write;
      memcpy(downstream_buffer, sp.data() + (sp.size() - to_write), bytes_written);
      to_write -= bytes_written;
      downstream_length_ += bytes_written;
      TSIOBufferProduce(downstream_buffer_, bytes_written);
    }
    CHECK(to_write == 0) << "to_write failure";
    if (last) {
      TSVIONBytesSet(downstream_vio_, downstream_length_);
    }
    if (reenable) { 
      TSVIOReenable(downstream_vio_);
    }
  }
  Unlock();
}

void AtsBaseFetch::HandleDone(bool success) {
  CHECK(!done_called_);
  CHECK(downstream_vio_);
  TSDebug("ats-speed", "Done()!");

  Lock();
  done_called_ = true;
  ForwardData("", true, true);
  DecrefAndDeleteIfUnreferenced();
  Unlock();
}

void AtsBaseFetch::DecrefAndDeleteIfUnreferenced() {
  if (__sync_add_and_fetch(&references_, -1) == 0) {
    delete this;
  }
}
