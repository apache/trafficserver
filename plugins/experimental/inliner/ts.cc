/** @file

  Inlines base64 images from the ATS cache

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
#include <cstring>
#include <iostream>
#include <limits>

#include "ts.h"

namespace ats
{
namespace io
{
  IO *
  IO::read(TSVConn v, TSCont c, const int64_t s)
  {
    assert(s > 0);
    IO *io  = new IO();
    io->vio = TSVConnRead(v, c, io->buffer, s);
    return io;
  }

  IO *
  IO::write(TSVConn v, TSCont c, const int64_t s)
  {
    assert(s > 0);
    IO *io  = new IO();
    io->vio = TSVConnWrite(v, c, io->reader, s);
    return io;
  }

  uint64_t
  IO::copy(const std::string &s) const
  {
    assert(buffer != nullptr);
    const uint64_t size = TSIOBufferWrite(buffer, s.data(), s.size());
    assert(size == s.size());
    return size;
  }

  int64_t
  IO::consume() const
  {
    assert(reader != nullptr);
    const int64_t available = TSIOBufferReaderAvail(reader);
    if (available > 0) {
      TSIOBufferReaderConsume(reader, available);
    }
    return available;
  }

  int64_t
  IO::done() const
  {
    assert(vio != nullptr);
    assert(reader != nullptr);
    const int64_t d = TSIOBufferReaderAvail(reader) + TSVIONDoneGet(vio);
    TSVIONDoneSet(vio, d);
    return d;
  }

  WriteOperation::~WriteOperation()
  {
    assert(mutex_ != nullptr);
    const Lock lock(mutex_);
    TSDebug(PLUGIN_TAG, "~WriteOperation");

    vio_ = nullptr;

    if (action_ != nullptr) {
      TSActionCancel(action_);
    }

    assert(reader_ != nullptr);
    TSIOBufferReaderFree(reader_);

    assert(buffer_ != nullptr);
    TSIOBufferDestroy(buffer_);

    assert(continuation_ != nullptr);
    TSContDestroy(continuation_);

    assert(vconnection_ != nullptr);
    TSVConnShutdown(vconnection_, 0, 1);
  }

  WriteOperation::WriteOperation(const TSVConn v, const TSMutex m, const size_t t)
    : vconnection_(v),
      buffer_(TSIOBufferCreate()),
      reader_(TSIOBufferReaderAlloc(buffer_)),
      mutex_(m != nullptr ? m : TSMutexCreate()),
      continuation_(TSContCreate(WriteOperation::Handle, mutex_)),
      vio_(TSVConnWrite(v, continuation_, reader_, std::numeric_limits<int64_t>::max())),
      action_(nullptr),
      timeout_(t),
      bytes_(0),
      reenable_(true)
  {
    assert(vconnection_ != nullptr);
    assert(buffer_ != nullptr);
    assert(reader_ != nullptr);
    assert(mutex_ != nullptr);
    assert(continuation_ != nullptr);
    assert(vio_ != nullptr);

    if (timeout_ > 0) {
      action_ = TSContScheduleOnPool(continuation_, timeout_, TS_THREAD_POOL_NET);
      assert(action_ != nullptr);
    }
  }

  void
  WriteOperation::process(const size_t b)
  {
    assert(mutex_);
    const Lock lock(mutex_);
    bytes_ += b;
    if (vio_ != nullptr && TSVIOContGet(vio_) != nullptr) {
      if (reenable_) {
        TSVIOReenable(vio_);
        reenable_ = false;
      }
    } else {
      vio_ = nullptr;
    }
  }

  int
  WriteOperation::Handle(const TSCont c, const TSEvent e, void *d)
  {
    assert(c != nullptr);
    WriteOperationPointer *const p = static_cast<WriteOperationPointer *>(TSContDataGet(c));

    if (TS_EVENT_VCONN_WRITE_COMPLETE == e) {
      TSDebug(PLUGIN_TAG, "TS_EVENT_VCONN_WRITE_COMPLETE");
      if (p != nullptr) {
        TSContDataSet(c, nullptr);
        delete p;
      }
      return TS_SUCCESS;
    }

    assert(p != nullptr);
    assert(*p);
    WriteOperation &operation = **p;
    assert(operation.continuation_ == c);
    assert(operation.vconnection_ != nullptr);
    assert(d != nullptr);
    assert(TS_EVENT_ERROR == e || TS_EVENT_TIMEOUT == e || TS_EVENT_VCONN_WRITE_READY == e);

    switch (e) {
    case TS_EVENT_ERROR:
      TSError("[" PLUGIN_TAG "] TS_EVENT_ERROR from producer");
      goto handle_error; // handle errors as timeouts

    case TS_EVENT_TIMEOUT:
      TSError("[" PLUGIN_TAG "] TS_EVENT_TIMEOUT from producer");

    handle_error:
      operation.close();
      assert(operation.action_ != nullptr);
      operation.action_ = nullptr;
      /*
      TSContDataSet(c, NULL);
      delete p;
      */
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      operation.reenable_ = true;
      break;

    default:
      TSError("[" PLUGIN_TAG "] Unknown event: %i", e);
      assert(false); // UNREACHEABLE
      break;
    }

    return TS_SUCCESS;
  }

  WriteOperationWeakPointer
  WriteOperation::Create(const TSVConn v, const TSMutex m, const size_t t)
  {
    WriteOperation *const operation = new WriteOperation(v, m, t);
    assert(operation != nullptr);
    WriteOperationPointer *const pointer = new WriteOperationPointer(operation);
    assert(pointer != nullptr);
    TSContDataSet(operation->continuation_, pointer);

#ifndef NDEBUG
    {
      WriteOperationPointer *const p = static_cast<WriteOperationPointer *>(TSContDataGet(operation->continuation_));
      assert(pointer == p);
      assert((*p).get() == operation);
    }
#endif

    return WriteOperationWeakPointer(*pointer);
  }

  WriteOperation &
  WriteOperation::operator<<(const TSIOBufferReader r)
  {
    assert(r != nullptr);
    process(TSIOBufferCopy(buffer_, r, TSIOBufferReaderAvail(r), 0));
    return *this;
  }

  WriteOperation &
  WriteOperation::operator<<(const ReaderSize &r)
  {
    assert(r.reader != nullptr);
    process(TSIOBufferCopy(buffer_, r.reader, r.size, r.offset));
    return *this;
  }

  WriteOperation &
  WriteOperation::operator<<(const ReaderOffset &r)
  {
    assert(r.reader != nullptr);
    process(TSIOBufferCopy(buffer_, r.reader, TSIOBufferReaderAvail(r.reader), r.offset));
    return *this;
  }

  WriteOperation &
  WriteOperation::operator<<(const char *const s)
  {
    assert(s != nullptr);
    process(TSIOBufferWrite(buffer_, s, strlen(s)));
    return *this;
  }

  WriteOperation &
  WriteOperation::operator<<(const std::string &s)
  {
    process(TSIOBufferWrite(buffer_, s.data(), s.size()));
    return *this;
  }

  void
  WriteOperation::close()
  {
    assert(mutex_ != nullptr);
    const Lock lock(mutex_);
    if (vio_ != nullptr && TSVIOContGet(vio_) != nullptr) {
      TSVIONBytesSet(vio_, bytes_);
      TSVIOReenable(vio_);
    }
    vio_ = nullptr;
  }

  void
  WriteOperation::abort()
  {
    assert(mutex_ != nullptr);
    const Lock lock(mutex_);
    vio_ = nullptr;
  }

  IOSink::~IOSink()
  {
    // TSDebug(PLUGIN_TAG, "~IOSink %p", this);
    const WriteOperationPointer operation = operation_.lock();
    if (operation) {
      operation_.reset();
      operation->close();
    }
  }

  void
  IOSink::process()
  {
    const WriteOperationPointer operation = operation_.lock();

    if (!data_ || !operation) {
      return;
    }

    assert(operation->mutex_ != nullptr);
    const Lock lock(operation->mutex_);

    assert(operation->buffer_ != nullptr);
    const Node::Result result = data_->process(operation->buffer_);
    operation->bytes_ += result.first;
    operation->process();
    if (result.second && data_.unique()) {
      data_.reset();
    }
  }

  Lock
  IOSink::lock()
  {
    const WriteOperationPointer operation = operation_.lock();

    if (!operation) {
      return Lock();
    }

    assert(operation != nullptr);
    assert(operation->mutex_ != nullptr);

    return Lock(operation->mutex_);
  }

  void
  IOSink::abort()
  {
    const WriteOperationPointer operation = operation_.lock();
    if (operation) {
      operation->abort();
    }
  }

  BufferNode &
  BufferNode::operator<<(const TSIOBufferReader r)
  {
    assert(r != nullptr);
    TSIOBufferCopy(buffer_, r, TSIOBufferReaderAvail(r), 0);
    return *this;
  }

  BufferNode &
  BufferNode::operator<<(const ReaderSize &r)
  {
    assert(r.reader != nullptr);
    TSIOBufferCopy(buffer_, r.reader, r.size, r.offset);
    return *this;
  }

  BufferNode &
  BufferNode::operator<<(const ReaderOffset &r)
  {
    assert(r.reader != nullptr);
    TSIOBufferCopy(buffer_, r.reader, TSIOBufferReaderAvail(r.reader), r.offset);
    return *this;
  }

  BufferNode &
  BufferNode::operator<<(const char *const s)
  {
    assert(s != nullptr);
    TSIOBufferWrite(buffer_, s, strlen(s));
    return *this;
  }

  BufferNode &
  BufferNode::operator<<(const std::string &s)
  {
    TSIOBufferWrite(buffer_, s.data(), s.size());
    return *this;
  }

  Node::Result
  BufferNode::process(const TSIOBuffer b)
  {
    assert(b != nullptr);
    assert(buffer_ != nullptr);
    assert(reader_ != nullptr);
    const size_t available = TSIOBufferReaderAvail(reader_);
    const size_t copied    = TSIOBufferCopy(b, reader_, available, 0);
    assert(copied == available);
    TSIOBufferReaderConsume(reader_, copied);
    // TSDebug(PLUGIN_TAG, "BufferNode::process %lu", copied);
    return Node::Result(copied, TSIOBufferReaderAvail(reader_) == 0);
  }

  Node::Result
  StringNode::process(const TSIOBuffer b)
  {
    assert(b != nullptr);
    const size_t copied = TSIOBufferWrite(b, string_.data(), string_.size());
    assert(copied == string_.size());
    return Node::Result(copied, true);
  }

  SinkPointer
  IOSink::branch()
  {
    if (!data_) {
      data_.reset(new Data(shared_from_this()));
      data_->first_ = true;
    }
    SinkPointer pointer(new Sink(data_));
    // TSDebug(PLUGIN_TAG, "IOSink branch %p", pointer.get());
    return pointer;
  }

  SinkPointer
  Sink::branch()
  {
    DataPointer data;
    if (data_) {
      const bool first = data_->nodes_.empty();
      data.reset(new Data(data_->root_));
      assert(data);
      data_->nodes_.push_back(data);
      assert(!data_->nodes_.empty());
      data->first_ = first;
    }
    SinkPointer pointer(new Sink(data));
    // TSDebug(PLUGIN_TAG, "Sink branch %p", pointer.get());
    return pointer;
  }

  Sink::~Sink()
  {
    // TSDebug(PLUGIN_TAG, "~Sink %p", this);
    assert(data_);
    assert(data_.use_count() >= 1);
    assert(data_->root_);
    const IOSinkPointer root(std::move(data_->root_));
    data_.reset();
    root->process();
  }

  Node::Result
  Data::process(const TSIOBuffer b)
  {
    assert(b != nullptr);
    int64_t length = 0;

    const Nodes::iterator begin = nodes_.begin(), end = nodes_.end();

    Nodes::iterator it = begin;

    for (; it != end; ++it) {
      assert(*it != nullptr);
      const Node::Result result = (*it)->process(b);
      length += result.first;
      if (!result.second || !it->unique()) {
        break;
      }
    }

    // TSDebug(PLUGIN_TAG, "Data::process %li", length);

    if (begin != it) {
      // TSDebug(PLUGIN_TAG, "Data::process::erase");
      nodes_.erase(begin, it);
      if (it != end) {
        Data *data = dynamic_cast<Data *>(it->get());
        while (data != nullptr) {
          // TSDebug(PLUGIN_TAG, "new first");
          data->first_ = true;
          if (data->nodes_.empty()) {
            break;
          }
          assert(data->nodes_.front());
          data = dynamic_cast<Data *>(data->nodes_.front().get());
        }
      }
    }

    return Node::Result(length, nodes_.empty());
  }

  Sink &
  Sink::operator<<(std::string &&s)
  {
    if (data_) {
      data_->nodes_.emplace_back(new StringNode(std::move(s)));
    }
    return *this;
  }

} // namespace io
} // namespace ats
