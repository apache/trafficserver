/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <cstdlib>
#include <atomic>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <ts/ts.h>
#include <tscpp/api/Cleanup.h>

using atscppapi::TSContUniqPtr;
using atscppapi::TSDbgCtlUniqPtr;

/*
Plugin for testing TSVConnFdCreate().
*/

#define PINAME "TSVConnFd"

namespace
{
char PIName[] = PINAME;

template <typename T>
T *
nonNullPtrRel(T *ptr)
{
  TSReleaseAssert(ptr != nullptr);

  return ptr;
}

template <typename T>
T *
nonNullPtrDbg(T *ptr)
{
  TSAssert(ptr != nullptr);

  return ptr;
}

// C++ wrapper class for TSIOBufferReader.  Note that I/O buffers are not thread-safe.  The user code must
// ensure that there is mutual exclusion of access to an I/O buffer and its readers.
//
class Io_buffer_consume
{
public:
  Io_buffer_consume() {}

  // Note: user code must destroy all instances referring to a TSIOBuffer before destroying the TSIOBuffer.
  //
  explicit Io_buffer_consume(TSIOBuffer io_buffer)
  {
    _io_buffer_reader.reset(nonNullPtrDbg(TSIOBufferReaderAlloc(nonNullPtrDbg(io_buffer))));
  }

  Io_buffer_consume(Io_buffer_consume &&) = default;
  Io_buffer_consume &operator=(Io_buffer_consume &&) = default;

  // Returns number of bytes available to consume.
  //
  std::int64_t avail();

  // Consume the data in the returned buffer, of (positive) length "amount".  "amount" must not be greater than
  // avail().  The buffer remains valid until the next call to avail() for this object, or this object is
  // destroyed.  Returns null if avail() is zero (because no data is currently available).  Must not be called if
  // attached returns false.  If amount is zero, returns the same value as the last call to consume().
  //
  char const * consume(std::int64_t amount);

private:
  atscppapi::TSIOBufferReaderUniqPtr _io_buffer_reader;

  // If there is no current block (_io_block == nullptr), _block_size and _block_avail are both 0.  If there is a
  // current block, _block_size is it's size, and _block_avail is the number of bytes at the end of the block
  // not yet consumed (less than or equal to _block_size, may be 0).
  //
  std::int64_t _block_size{0}, _block_avail{0};
  TSIOBufferBlock _io_block{nullptr}; // Initialize to shut up compiler warning.
  char const *_block_data{nullptr};   // Initialize to shut up compiler warning.
};

std::int64_t
Io_buffer_consume::avail()
{
  if (!_block_avail) {
    if (_block_size) {
      // There is a current block, but it has been used up.  Consume the current block, and get the next one
      // if it's available.

      auto a = TSIOBufferReaderAvail(_io_buffer_reader.get()) - _block_size;
      TSIOBufferReaderConsume(_io_buffer_reader.get(), _block_size);
      TSAssert(TSIOBufferReaderAvail(_io_buffer_reader.get()) >= a);
      TSAssert(TSIOBufferReaderStart(_io_buffer_reader.get()) == _io_block);
      _block_size = 0;

      _io_block = TSIOBufferBlockNext(_io_block);
    } else {
      // No current block.  See if one is available.
      //
      _io_block = TSIOBufferReaderStart(_io_buffer_reader.get());
    }
    if (_io_block) {
      // There is a new current block.
      //
      _block_data  = TSIOBufferBlockReadStart(_io_block, _io_buffer_reader.get(), &_block_size);
      _block_avail = _block_size;
    } else {
      // There is no current block.
      //
      _block_size = 0;
    }
  }
  return _block_avail;
}

char const *
Io_buffer_consume::consume(std::int64_t amount)
{
  TSAssert(amount >= 0);
  TSAssert(amount <= _block_avail);

  if (!_block_avail) {
    return nullptr;
  }
  char const *result = _block_data;
  _block_data += amount;
  _block_avail -= amount;

  return result;
}

// Receive data coming from a VConnection until end of stream.  There can only be one instance of this class associated
// with a VConnection.
//
class Recv_from_vconn
{
public:
  explicit Recv_from_vconn(TSVConn vconn) : _vconn{vconn}
  {
    TSContDataSet(_cont.get(), this);

    // Note that the VConn implementor is required to lock the mutex of the given continuation when writing data
    // to the given I/O buffer reader.  The same mutex must be locked when consuming data from a reader associated
    // with the I/O buffer.
    //
    nonNullPtrDbg(TSVConnRead(vconn, _cont.get(), _io_buffer.get(), INT64_MAX));
  }

  TSVConn vconn() const { return _vconn; }

  virtual ~Recv_from_vconn() {}

protected:
  enum _Status
  {
    IN_PROGRESS,
    VCONN_SHUTDOWN_FOR_RECEIVING,
    ERROR
  };

  _Status _status() const { return _status_.load(std::memory_order_acquire); }

  // Event that caused _status() to be ERROR.
  //
  TSEvent _error_event() const
  {
    TSAssert(_status() == ERROR);

    return _error_event_;
  }

  // Returns number of bytes available to consume.
  //
  std::int64_t
  _avail()
  {
    return _bc.avail();
  }

  // Consume the data in the returned buffer, of (positive) length "amount".  "amount" must not be greater than
  // _avail().  The buffer remains valid until the next call to _avail() for this object, or this object is
  // destroyed.  Returns null if _avail() is zero (because no data is currently available).  If amount is zero,
  // returns the same value as the last call to _consume().
  //
  char const *
  _consume(std::int64_t amount)
  {
    return _bc.consume(amount);
  }

  TSMutex _mtx() const { return _mtx_; }

  Recv_from_vconn(Recv_from_vconn const &) = delete;
  Recv_from_vconn & operator = (Recv_from_vconn const &) = delete;

private:
  TSVConn _vconn;
  TSMutex _mtx_{nonNullPtrDbg(TSMutexCreate())};
  atscppapi::TSContUniqPtr _cont{nonNullPtrDbg(TSContCreate(_cont_func, _mtx_))};
  atscppapi::TSIOBufferUniqPtr _io_buffer{nonNullPtrDbg(TSIOBufferCreate())};
  Io_buffer_consume _bc{nonNullPtrDbg(_io_buffer.get())};  // Order is important here, _bc must be destroyed before _io_buffer.
  std::atomic<_Status> _status_{IN_PROGRESS};
  TSEvent _error_event_{TS_EVENT_NONE};

  static int _cont_func(TSCont cont, TSEvent event, void *edata);

  // This is called to indicate that data may be available, or a change in status of the VConnection.
  //
  virtual void _notify_recv_from_vconn() = 0;
};

int
Recv_from_vconn::_cont_func(TSCont cont, TSEvent event, void *edata)
{
  auto rfv = static_cast<Recv_from_vconn *>(nonNullPtrDbg(TSContDataGet(nonNullPtrDbg(cont))));

  TSAssert(IN_PROGRESS == rfv->_status());
  nonNullPtrDbg(edata);

  switch (event) {
  // My best guess is that this event is triggered when the source of data feeding into the I/O buffer has
  // closed without any known error, but before the end of the active read VIO.
  //
  case TS_EVENT_VCONN_EOS:
    rfv->_status_.store(VCONN_SHUTDOWN_FOR_RECEIVING, std::memory_order_relaxed);
    break;

  // My best guess is that this event is triggered when the I/O buffer (that is, the one associated with _bc)
  // makes a transition from being empty to being non-empty.
  //
  case TS_EVENT_VCONN_READ_READY:
    break;

  // My best guess is that this event is triggered when all the bytes requested in the read VIO have been received.
  // This should not happen for this class because the number bytes for the read VIO is INT64_MAX, effectively
  // infinite.
  //
  // case TS_EVENT_VCONN_READ_COMPLETE:

  default:
    TSError(PINAME ": VConnection read error event=%d", event);
    rfv->_error_event_ = event;
    rfv->_status_.store(ERROR, std::memory_order_release);
    break;
  }
  rfv->_notify_recv_from_vconn();

  return 0;
}

// Send data to a VConnection.  Not thread-safe.
//
class Send_to_vconn
{
public:
  Send_to_vconn(TSVConn vconn, int64_t bytes_to_send) : _vconn{nonNullPtrDbg(vconn)}, _bytes_to_send{bytes_to_send}
  {
    _status.store(bytes_to_send ? IN_PROGRESS : VIO_DONE, std::memory_order_relaxed);
  }

  TSVConn vconn() const { return _vconn; }

  // Send some bytes.  If an override of _notify_send_to_vconn() calls _send(), you must lock the mutex returned by mtx()
  // for all calls to send() after the first one.
  //
  void send(void const *data, int64_t n_bytes);

  enum Status
  {
    IN_PROGRESS,
    VIO_DONE,
    VCONN_SHUTDOWN_FOR_SENDING,
    ERROR
  };

  Status status() const { return _status.load(std::memory_order_acquire); }

  // Event that caused status() to be ERROR.
  //
  TSEvent error_event() const
  {
    TSAssert(status() == ERROR);

    return _error_event;
  }

  TSMutex mtx() const { return _mtx; }

  virtual ~Send_to_vconn() {}

  Send_to_vconn(Send_to_vconn const &) = delete;
  Send_to_vconn & operator = (Send_to_vconn const &) = delete;

protected:
  // If _notify_send_to_vconn() needs to send, it should call this function (not send()).
  //
  void _send(void const *data, int64_t n_bytes);

private:
  TSVConn _vconn;
  int64_t _bytes_to_send;
  TSMutex _mtx{nullptr};
  TSContUniqPtr _cont; // Destroying this destroys _mtx.
  atscppapi::TSIOBufferUniqPtr _io_buf;
  atscppapi::TSIOBufferReaderUniqPtr _io_buf_reader; // Order matters, this must be destroyed before _io_buf.
  std::atomic<Status> _status{IN_PROGRESS};
  TSEvent _error_event{TS_EVENT_NONE};

  static int _cont_func(TSCont cont, TSEvent event, void *edata);

  // This is called when the IOBuffer referred to by _io_buf is empty, or the status is no longer IN_PROGRESS.
  // _send() can be called from within this function.
  //
  virtual void _notify_send_to_vconn() {}
};

void
Send_to_vconn::_send(void const *data, int64_t n_bytes)
{
  if (0 == n_bytes) {
    return;
  }

  TSAssert(n_bytes > 0);

  TSAssert(n_bytes <= _bytes_to_send);

  nonNullPtrDbg(data);

  TSAssert(IN_PROGRESS == status());

  int64_t size      = n_bytes;
  char const *data_ = static_cast<char const *>(data);

  do {
    auto size_written = TSIOBufferWrite(_io_buf.get(), data_, size);
    TSAssert(size_written > 0);
    TSAssert(size_written <= size);
    size -= size_written;
    data_ += size_written;
  } while (size);

  _bytes_to_send -= n_bytes;
}

void
Send_to_vconn::send(void const *data, int64_t n_bytes)
{
  if (n_bytes <= 0) {
    return;
  }
  bool start_vio{false};

  if (!_cont) {
    start_vio = true;
    _mtx = nonNullPtrDbg(TSMutexCreate());
    _cont.reset(nonNullPtrDbg(TSContCreate(_cont_func, nonNullPtrDbg(_mtx))));
    TSContDataSet(_cont.get(), this);
    _io_buf.reset(nonNullPtrDbg(TSIOBufferCreate()));
    _io_buf_reader.reset(nonNullPtrDbg(TSIOBufferReaderAlloc(_io_buf.get())));
  }

  // Save this here (for TSVConnWrite()) if needed, because _send() will subtract n_bytes from it.
  //
  auto bs = _bytes_to_send;

  _send(data, n_bytes);

  if (start_vio) {
    // Note that the VConn implementor is required to lock the mutex of the given continuation when reading data
    // with the given I/O buffer reader.  The same mutex must be locked when writing into the I/O buffer being
    // read.
    //
    nonNullPtrDbg(TSVConnWrite(_vconn, _cont.get(), _io_buf_reader.get(), bs));
  }
}

int
Send_to_vconn::_cont_func(TSCont cont, TSEvent event, void *edata)
{
  auto stv = static_cast<Send_to_vconn *>(nonNullPtrDbg(TSContDataGet(nonNullPtrDbg(cont))));

  TSAssert(IN_PROGRESS == stv->status());

  switch (event)
  {
  case TS_EVENT_VCONN_WRITE_READY:
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    stv->_status.store(VIO_DONE, std::memory_order_relaxed);
    break;

  case TS_EVENT_VCONN_EOS:
    stv->_status.store(VCONN_SHUTDOWN_FOR_SENDING, std::memory_order_relaxed);
    break;

  default:
    stv->_error_event = event;
    stv->_status.store(ERROR, std::memory_order_release);
    break;
  }
  stv->_notify_send_to_vconn();

  return 0;
}

TSDbgCtlUniqPtr dbg_ctl_guard{TSDbgCtlCreate(PIName)};
TSDbgCtl const * const dbg_ctl{dbg_ctl_guard.get()};

// Delete file whose path is specified in the constructor when the instance is destroyed.
//
class File_deleter
{
public:
  File_deleter(std::string &&pathspec) : _pathspec(std::move(pathspec)) {}

  ~File_deleter() { unlink(_pathspec.c_str()); }

private:
  std::string _pathspec;
};

// Listen at a (cleartext) socket, then connect to it, and send data with a ramping pattern in both directions over the
// resulting connections.
//
class Ramp_test : private Send_to_vconn
{
public:

  struct Test_params
  {
    struct Half
    {
      int n_groups_send, n_group_bytes, n_bytes_recv;
    };

    Half connect_to_accept, accept_to_connect;
  };

  // Execute test.
  //
  static void x(Test_params const &p);

private:
  // Send and receive ramping pattern to VCONN.  Instances must be dynamically allocated.
  //
  class _Send_recv : private Send_to_vconn, private Recv_from_vconn
  {
  public:
    _Send_recv(TSVConn vconn_, std::shared_ptr<File_deleter> f_del, int n_groups_send, int n_group_bytes,
               bool allow_send_error, int n_bytes_recv)
    : Send_to_vconn{vconn_, n_groups_send * n_group_bytes}, Recv_from_vconn(vconn_), _f_del{f_del}
    {
      TSDbg(dbg_ctl, "n_groups_send=%d n_group_bytes=%d allow_send_error=%c, n_bytes_recv=%d inst=%p", n_groups_send,
            n_group_bytes, allow_send_error ? 'T' : 'F', n_bytes_recv, this);

      TSReleaseAssert(n_groups_send >= 0);
      TSReleaseAssert(n_group_bytes >= 0);
      TSReleaseAssert(n_bytes_recv >= 0);

      if (!n_group_bytes || !n_groups_send) {
        n_group_bytes = 0;
        n_groups_send = 0;
      }

      _s.n_groups_remaining = n_groups_send;
      _s.n_group_bytes = n_group_bytes;
      _s.allow_error = allow_send_error;

      if (_s.n_groups_remaining) {
        --_s.n_groups_remaining;

        _s.buf.resize(n_group_bytes);

        for (int i{0}; i < _s.n_group_bytes; ++i) {
          _s.buf[i] = _s.ramp_val++;
        }
        send(_s.buf.data(), _s.n_group_bytes);

      } else {
        ++_done_count;
      }

      TSReleaseAssert(n_bytes_recv >= 0);

      _r.n_bytes_remaining = n_bytes_recv;

      if (!n_bytes_recv) {
        _done();
      }
    }

    void _notify_send_to_vconn() override;

    void _notify_recv_from_vconn() override;

    ~_Send_recv() override {}

  private:
    std::shared_ptr<File_deleter> _f_del;

    struct _Send_fields
    {
      int n_groups_remaining, n_group_bytes;
      bool allow_error;
      std::vector<unsigned char> buf;
      unsigned char ramp_val{0};
    };

    _Send_fields _s;

    struct _Recv_fields
    {
      int n_bytes_remaining;
      unsigned char ramp_val{0};
    };

    _Recv_fields _r;

    std::atomic<int> _done_count{0};

    // Send and receive each call this when done.
    //
    void
    _done(TSMutex cont_mtx_ = nullptr)
    {
      ++_done_count;
      if (2 == _done_count) {
        TSVConnClose(Send_to_vconn::vconn());
        if (cont_mtx_) {
          // Presumably, closing the VConnection will mean it generates no more events.  So the continuation mutex can
          // be unlocked without causeing a race condition.
          //
          TSMutexUnlock(cont_mtx_);
        }
        // This will destroy the continuation, which would fail if the continuation mutex was locked.
        //
        delete this;
      }
    }
  };
};

void
Ramp_test::_Send_recv::_notify_send_to_vconn()
{
  auto st = status();
  if (TSIsDbgCtlSet(dbg_ctl) && (st != Send_to_vconn::IN_PROGRESS)) {
    TSDbg(dbg_ctl, "Ramp_test::_Send_recv::_notify_send_to_vconn: status=%d inst=%p", int(st), this);
  }
  switch (st)
  {
  case Send_to_vconn::IN_PROGRESS: {
    if (_s.n_groups_remaining) {

      --_s.n_groups_remaining;

      for (int i{0}; i < _s.n_group_bytes; ++i) {
        _s.buf[i] = _s.ramp_val++;
      }
      _send(_s.buf.data(), _s.n_group_bytes);

    } else {
      TSDbg(dbg_ctl, "Ramp_test::_Send_recv::_notify_send_to_vconn: done inst=%p", this);
      _done(mtx());
    }
    }
    break;
  case Send_to_vconn::VIO_DONE:
  case Send_to_vconn::VCONN_SHUTDOWN_FOR_SENDING:
    _done(mtx());
    break;

  case Send_to_vconn::ERROR:
    if (_s.allow_error) {
      TSDbg(dbg_ctl, "Ramp_test::_Send_recv::_notify_send_to_vconn: error event: %d, inst=%p (error expected)",
            int(error_event()), this);

    } else {
      TSFatal(PINAME ": Ramp_test::_Send_recv::_notify_send_to_vconn: error event: %d, inst=%p", int(error_event()), this);
    }
    break;

  default:
    TSReleaseAssert(false);
  }
}

void
Ramp_test::_Send_recv::_notify_recv_from_vconn()
{
  auto st = Recv_from_vconn::_status();
  if (TSIsDbgCtlSet(dbg_ctl) && (st != Recv_from_vconn::IN_PROGRESS)) {
    TSDbg(dbg_ctl, "Ramp_test::_Send_recv::_notify_recv_from_vconn: status=%d inst=%p", int(st), this);
  }
  switch (st)
  {
  case Recv_from_vconn::IN_PROGRESS: {
    while (_r.n_bytes_remaining) {
      int64_t avail = _avail();
      if (avail <= 0) {
        break;
      }
      auto cp = reinterpret_cast<unsigned char const *>(_consume(avail));

      _r.n_bytes_remaining -= avail;

      while (avail) {
        if (*cp != _r.ramp_val) {
          TSFatal(PINAME ": Ramp_test::_Send_recv::_notify_recv_from_vconn: recv ramp val=%u expected ramp val=%u",
                  unsigned(*cp), unsigned(_r.ramp_val));
        }
        ++cp;
        ++_r.ramp_val;
        --avail;
      }
    }
    if (!_r.n_bytes_remaining) {
      TSDbg(dbg_ctl, "Ramp_test::_Send_recv::_notify_recv_from_vconn: done inst=%p", this);
      _done(mtx());
    }
    }
    break;

  case Recv_from_vconn::VCONN_SHUTDOWN_FOR_RECEIVING:
    TSVConnShutdown(Recv_from_vconn::vconn(), true, false);
    _done(mtx());
    break;

  case Recv_from_vconn::ERROR:
    TSFatal(PINAME ": Ramp_test::_Send_recv::_notify_recv_from_vconn: error event: %d", int(error_event()));
    break;

  default:
    TSReleaseAssert(false);
  }
}

auto const Loopback_addr{inet_addr("127.0.0.1")};

int listen_fd, loopback_port;

struct Tcp_loopback
{
  int connect_fd, accept_fd;
};

Tcp_loopback
make_loopback()
{
  Tcp_loopback lp;

  sockaddr_in connect_addr;

  connect_addr.sin_addr.s_addr = Loopback_addr;
  connect_addr.sin_port        = htons(loopback_port);
  connect_addr.sin_family      = AF_INET;

  lp.connect_fd = socket(AF_INET, SOCK_STREAM, 0);

  TSReleaseAssert(lp.connect_fd >= 0);

  TSReleaseAssert(connect(lp.connect_fd, reinterpret_cast<sockaddr *>(&connect_addr), sizeof(connect_addr)) == 0);

  lp.accept_fd = accept(listen_fd, nullptr, 0);

  TSReleaseAssert(lp.accept_fd >= 0);

  return lp;
}

std::shared_ptr<File_deleter> global_file_deleter;

void
Ramp_test::x(Test_params const &p)
{
  TSReleaseAssert((p.connect_to_accept.n_groups_send * p.connect_to_accept.n_group_bytes) >=
                  p.connect_to_accept.n_bytes_recv);
  TSReleaseAssert((p.accept_to_connect.n_groups_send * p.accept_to_connect.n_group_bytes) >=
                  p.accept_to_connect.n_bytes_recv);

  auto lp{make_loopback()};

  TSVConn vconn_connect{nonNullPtrRel(TSVConnFdCreate(lp.connect_fd))};
  TSVConn vconn_accept{nonNullPtrRel(TSVConnFdCreate(lp.accept_fd))};

  // If the receiver does not read even one byte of data, the sender will get an error.

  new _Send_recv{vconn_connect, global_file_deleter, p.connect_to_accept.n_groups_send, p.connect_to_accept.n_group_bytes,
                 0 == p.connect_to_accept.n_bytes_recv, p.accept_to_connect.n_bytes_recv};
  new _Send_recv{vconn_accept, global_file_deleter, p.accept_to_connect.n_groups_send, p.accept_to_connect.n_group_bytes,
                 0 == p.accept_to_connect.n_bytes_recv, p.connect_to_accept.n_bytes_recv};
}

int global_cont_func(TSCont cont, TSEvent event, void *event_data)
{
  nonNullPtrRel(cont);
  TSReleaseAssert(TS_EVENT_HTTP_READ_REQUEST_HDR == event);

  Ramp_test::Test_params tp;

  tp.connect_to_accept.n_groups_send = 100;
  tp.connect_to_accept.n_group_bytes = 200;
  tp.connect_to_accept.n_bytes_recv = 100 * 200;

  tp.accept_to_connect.n_groups_send = 100;
  tp.accept_to_connect.n_group_bytes = 200;
  tp.accept_to_connect.n_bytes_recv = 1;

  Ramp_test::x(tp);

  tp.connect_to_accept.n_groups_send = 100;
  tp.connect_to_accept.n_group_bytes = 200;
  tp.connect_to_accept.n_bytes_recv = 0;

  tp.accept_to_connect.n_groups_send = 100;
  tp.accept_to_connect.n_group_bytes = 200;
  tp.accept_to_connect.n_bytes_recv = 100 * 200;

  Ramp_test::x(tp);

  tp.connect_to_accept.n_groups_send = 100;
  tp.connect_to_accept.n_group_bytes = 200;
  tp.connect_to_accept.n_bytes_recv = 100 * 200;

  tp.accept_to_connect.n_groups_send = 100;
  tp.accept_to_connect.n_group_bytes = 200;
  tp.accept_to_connect.n_bytes_recv = 100 * 200;

  Ramp_test::x(tp);

  tp.connect_to_accept.n_groups_send = 10;
  tp.connect_to_accept.n_group_bytes = 20;
  tp.connect_to_accept.n_bytes_recv = 10 * 20;

  tp.accept_to_connect.n_groups_send = 1000;
  tp.accept_to_connect.n_group_bytes = 2000;
  tp.accept_to_connect.n_bytes_recv = 1000 * 2000;

  Ramp_test::x(tp);

  tp.connect_to_accept.n_groups_send = 1000;
  tp.connect_to_accept.n_group_bytes = 2000;
  tp.connect_to_accept.n_bytes_recv = 1000 * 2000;

  tp.accept_to_connect.n_groups_send = 10;
  tp.accept_to_connect.n_group_bytes = 20;
  tp.accept_to_connect.n_bytes_recv = 10 * 20;

  Ramp_test::x(tp);

  tp.connect_to_accept.n_groups_send = 3000;
  tp.connect_to_accept.n_group_bytes = 20000;
  tp.connect_to_accept.n_bytes_recv = 3000 * 20000;

  tp.accept_to_connect.n_groups_send = 3000;
  tp.accept_to_connect.n_group_bytes = 20000;
  tp.accept_to_connect.n_bytes_recv = 3000 * 20000;

  Ramp_test::x(tp);

  global_file_deleter.reset();

  TSHttpTxnReenable(static_cast<TSHttpTxn>(nonNullPtrRel(event_data)), TS_EVENT_HTTP_CONTINUE);

  return 0;
}

TSContUniqPtr global_cont;

} // end anonymous namespace

void
TSPluginInit(int n_arg, char const *arg[])
{
  TSDbg(dbg_ctl, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name = const_cast<char*>(PIName);
  info.vendor_name = const_cast<char*>("apache");
  info.support_email = const_cast<char*>("edge@yahooinc.com");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": failure calling TSPluginRegister.");
    return;
  } else {
    TSDbg(dbg_ctl, "Plugin registration succeeded.");
  }

  global_cont.reset(nonNullPtrRel(TSContCreate(global_cont_func, nullptr)));

  TSReleaseAssert(3 == n_arg);

  global_file_deleter.reset(new File_deleter{std::string{nonNullPtrRel(arg[1])}});

  loopback_port = std::atoi(arg[2]);

  TSReleaseAssert(loopback_port > 0);

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  TSReleaseAssert(listen_fd >= 0);

  sockaddr_in listen_addr;

  listen_addr.sin_addr.s_addr = Loopback_addr;
  listen_addr.sin_port        = htons(loopback_port);
  listen_addr.sin_family      = AF_INET;

  for (int i{0}; ; ++i) {
    TSDbg(dbg_ctl, "bind() with TCP port %d", loopback_port);
    int ret = bind(listen_fd, reinterpret_cast<sockaddr *>(&listen_addr), sizeof(listen_addr));
    if (ret >= 0) {
      break;
    }
    TSDbg(dbg_ctl, "bind() failed: errno=%d", errno);
    TSReleaseAssert(i < 100);
    ++loopback_port;
    listen_addr.sin_port = htons(loopback_port);
  }

  TSReleaseAssert(listen(listen_fd, 1) == 0);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, global_cont.get());
}
