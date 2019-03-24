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

#pragma once

#include <cassert>
#include <limits>
#include <list>
#include <memory>
#include <ts/ts.h>

#ifdef NDEBUG
#define CHECK(X) X
#else
#define CHECK(X)                                         \
  {                                                      \
    const TSReturnCode r = static_cast<TSReturnCode>(X); \
    assert(r == TS_SUCCESS);                             \
  }
#endif

namespace ats
{
namespace io
{
  // TODO(dmorilha): dislike this
  struct IO {
    TSIOBuffer buffer;
    TSIOBufferReader reader;
    TSVIO vio;

    ~IO()
    {
      consume();
      assert(reader != nullptr);
      TSIOBufferReaderFree(reader);
      assert(buffer != nullptr);
      TSIOBufferDestroy(buffer);
    }

    IO() : buffer(TSIOBufferCreate()), reader(TSIOBufferReaderAlloc(buffer)), vio(nullptr) {}
    IO(const TSIOBuffer &b) : buffer(b), reader(TSIOBufferReaderAlloc(buffer)), vio(nullptr) { assert(buffer != nullptr); }
    static IO *read(TSVConn, TSCont, const int64_t);

    static IO *
    read(TSVConn v, TSCont c)
    {
      return IO::read(v, c, std::numeric_limits<int64_t>::max());
    }

    static IO *write(TSVConn, TSCont, const int64_t);

    static IO *
    write(TSVConn v, TSCont c)
    {
      return IO::write(v, c, std::numeric_limits<int64_t>::max());
    }

    uint64_t copy(const std::string &) const;

    int64_t consume() const;

    int64_t done() const;
  };

  struct ReaderSize {
    const TSIOBufferReader reader;
    const size_t offset;
    const size_t size;

    ReaderSize(const TSIOBufferReader r, const size_t s, const size_t o = 0) : reader(r), offset(o), size(s)
    {
      assert(reader != nullptr);
    }

    ReaderSize(const ReaderSize &) = delete;
    ReaderSize &operator=(const ReaderSize &) = delete;
    void *operator new(const std::size_t)     = delete;
  };

  struct ReaderOffset {
    const TSIOBufferReader reader;
    const size_t offset;

    ReaderOffset(const TSIOBufferReader r, const size_t o) : reader(r), offset(o) { assert(reader != nullptr); }
    ReaderOffset(const ReaderOffset &) = delete;
    ReaderOffset &operator=(const ReaderOffset &) = delete;
    void *operator new(const std::size_t)         = delete;
  };

  struct WriteOperation;

  typedef std::shared_ptr<WriteOperation> WriteOperationPointer;
  typedef std::weak_ptr<WriteOperation> WriteOperationWeakPointer;

  struct Lock {
    const TSMutex mutex_;

    ~Lock()
    {
      if (mutex_ != nullptr) {
        TSMutexUnlock(mutex_);
      }
    }

    Lock(const TSMutex m) : mutex_(m)
    {
      if (mutex_ != nullptr) {
        TSMutexLock(mutex_);
      }
    }

    // noncopyable
    Lock() : mutex_(nullptr) {}
    Lock(const Lock &) = delete;

    Lock(Lock &&l) : mutex_(l.mutex_) { const_cast<TSMutex &>(l.mutex_) = nullptr; }
    Lock &operator=(const Lock &) = delete;
  };

  struct WriteOperation : std::enable_shared_from_this<WriteOperation> {
    TSVConn vconnection_;
    TSIOBuffer buffer_;
    TSIOBufferReader reader_;
    TSMutex mutex_;
    TSCont continuation_;
    TSVIO vio_;
    TSAction action_;
    const size_t timeout_;
    size_t bytes_;
    bool reenable_;

    static int Handle(TSCont, TSEvent, void *);
    static WriteOperationWeakPointer Create(const TSVConn, const TSMutex mutex = nullptr, const size_t timeout = 0);

    ~WriteOperation();

    // noncopyable
    WriteOperation(const WriteOperation &) = delete;
    WriteOperation &operator=(const WriteOperation &) = delete;

    WriteOperation &operator<<(const TSIOBufferReader);
    WriteOperation &operator<<(const ReaderSize &);
    WriteOperation &operator<<(const ReaderOffset &);
    WriteOperation &operator<<(const char *const);
    WriteOperation &operator<<(const std::string &);

    void process(const size_t b = 0);
    void close();
    void abort();

  private:
    WriteOperation(const TSVConn, const TSMutex, const size_t);
  };

  struct Node;
  typedef std::shared_ptr<Node> NodePointer;
  typedef std::list<NodePointer> Nodes;

  struct IOSink;
  typedef std::shared_ptr<IOSink> IOSinkPointer;

  struct Sink;
  typedef std::shared_ptr<Sink> SinkPointer;

  struct Data;
  typedef std::shared_ptr<Data> DataPointer;

  struct IOSink : std::enable_shared_from_this<IOSink> {
    WriteOperationWeakPointer operation_;
    DataPointer data_;

    ~IOSink();

    // noncopyable
    IOSink(const IOSink &) = delete;
    IOSink &operator=(const IOSink &) = delete;

    template <class T>
    IOSink &
    operator<<(T &&t)
    {
      const WriteOperationPointer operation = operation_.lock();
      if (operation) {
        const Lock lock(operation->mutex_);
        *operation << std::forward<T>(t);
      }
      return *this;
    }

    template <class... A>
    static IOSinkPointer
    Create(A &&... a)
    {
      return IOSinkPointer(new IOSink(WriteOperation::Create(std::forward<A>(a)...)));
    }

    void process();
    SinkPointer branch();
    Lock lock();
    void abort();

  private:
    IOSink(WriteOperationWeakPointer &&p) : operation_(std::move(p)) {}
  };

  struct Node {
    typedef std::pair<size_t, bool> Result;
    IOSinkPointer ioSink_;
    virtual ~Node() {}
    virtual Node::Result process(const TSIOBuffer) = 0;
  };

  struct StringNode : Node {
    std::string string_;
    explicit StringNode(std::string &&s) : string_(std::move(s)) {}
    Node::Result process(const TSIOBuffer) override;
  };

  struct BufferNode : Node {
    const TSIOBuffer buffer_;
    const TSIOBufferReader reader_;

    ~BufferNode() override
    {
      assert(reader_ != nullptr);
      TSIOBufferReaderFree(reader_);
      assert(buffer_ != nullptr);
      TSIOBufferDestroy(buffer_);
    }

    BufferNode() : buffer_(TSIOBufferCreate()), reader_(TSIOBufferReaderAlloc(buffer_))
    {
      assert(buffer_ != nullptr);
      assert(reader_ != nullptr);
    }

    // noncopyable
    BufferNode(const BufferNode &) = delete;
    BufferNode &operator=(const BufferNode &) = delete;
    BufferNode &operator<<(const TSIOBufferReader);
    BufferNode &operator<<(const ReaderSize &);
    BufferNode &operator<<(const ReaderOffset &);
    BufferNode &operator<<(const char *const);
    BufferNode &operator<<(const std::string &);
    Node::Result process(const TSIOBuffer) override;
  };

  struct Data : Node {
    Nodes nodes_;
    IOSinkPointer root_;
    bool first_;

    template <class T> Data(T &&t) : root_(std::forward<T>(t)), first_(false) {}
    // noncopyable
    Data(const Data &) = delete;
    Data &operator=(const Data &) = delete;

    Node::Result process(const TSIOBuffer) override;
  };

  struct Sink {
    DataPointer data_;

    ~Sink();

    template <class... A> Sink(A &&... a) : data_(std::forward<A>(a)...) {}
    // noncopyable
    Sink(const Sink &) = delete;
    Sink &operator=(const Sink &) = delete;

    SinkPointer branch();

    Sink &operator<<(std::string &&);

    template <class T>
    Sink &
    operator<<(T &&t)
    {
      if (data_) {
        const Lock lock = data_->root_->lock();
        assert(data_->root_ != nullptr);
        const bool empty = data_->nodes_.empty();
        if (data_->first_ && empty) {
          // TSDebug(PLUGIN_TAG, "flushing");
          assert(data_->root_);
          *data_->root_ << std::forward<T>(t);
        } else {
          // TSDebug(PLUGIN_TAG, "buffering");
          BufferNode *buffer = nullptr;
          if (!empty) {
            buffer = dynamic_cast<BufferNode *>(data_->nodes_.back().get());
          }
          if (buffer == nullptr) {
            data_->nodes_.emplace_back(new BufferNode());
            buffer = reinterpret_cast<BufferNode *>(data_->nodes_.back().get());
          }
          assert(buffer != nullptr);
          *buffer << std::forward<T>(t);
        }
      }
      return *this;
    }
  };

} // namespace io
} // namespace ats
