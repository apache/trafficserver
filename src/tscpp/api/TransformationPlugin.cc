/**
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

/**
 * @file TransformationPlugin.cc
 */

#include "tscpp/api/TransformationPlugin.h"

#include "ts/ts.h"
#include <cstddef>
#include <cinttypes>
#include "utils_internal.h"
#include "logging_internal.h"
#include "tscpp/api/noncopyable.h"
#include "tscpp/api/Continuation.h"

#ifndef INT64_MAX
#define INT64_MAX (9223372036854775807LL)
#endif

namespace atscppapi
{
namespace detail
{
  class ResumeAfterPauseCont : public Continuation
  {
  public:
    ResumeAfterPauseCont() : Continuation() {}

    ResumeAfterPauseCont(Continuation::Mutex m) : Continuation(m) {}

  protected:
    int _run(TSEvent event, void *edata) override;
  };

} // end namespace detail

/**
 * @private
 */
struct TransformationPluginState : noncopyable, public detail::ResumeAfterPauseCont {
  TSVConn vconn_;
  Transaction &transaction_;
  TransformationPlugin &transformation_plugin_;
  TransformationPlugin::Type type_;
  TSVIO output_vio_; // this gets initialized on an output().
  TSHttpTxn txn_;
  TSIOBuffer output_buffer_;
  TSIOBufferReader output_buffer_reader_;
  int64_t bytes_written_;
  bool paused_;

  // We can only send a single WRITE_COMPLETE even though
  // we may receive an immediate event after we've sent a
  // write complete, so we'll keep track of whether or not we've
  // sent the input end our write complete.
  bool input_complete_dispatched_;

  std::string request_xform_output_; // in case of request xform, data produced is buffered here

  TransformationPluginState(atscppapi::Transaction &transaction, TransformationPlugin &transformation_plugin,
                            TransformationPlugin::Type type, TSHttpTxn txn)
    : detail::ResumeAfterPauseCont(TSMutexCreate()),
      vconn_(nullptr),
      transaction_(transaction),
      transformation_plugin_(transformation_plugin),
      type_(type),
      output_vio_(nullptr),
      txn_(txn),
      output_buffer_(nullptr),
      output_buffer_reader_(nullptr),
      bytes_written_(0),
      paused_(false),
      input_complete_dispatched_(false)
  {
    output_buffer_        = TSIOBufferCreate();
    output_buffer_reader_ = TSIOBufferReaderAlloc(output_buffer_);
  };

  ~TransformationPluginState() override
  {
    if (output_buffer_reader_) {
      TSIOBufferReaderFree(output_buffer_reader_);
      output_buffer_reader_ = nullptr;
    }

    if (output_buffer_) {
      TSIOBufferDestroy(output_buffer_);
      output_buffer_ = nullptr;
    }
  }
};

} // end namespace atscppapi

using namespace atscppapi;

namespace
{
using ResumeAfterPauseCont = atscppapi::detail::ResumeAfterPauseCont;

void
cleanupTransformation(TSCont contp)
{
  LOG_DEBUG("Destroying transformation contp=%p", contp);
  TSContDataSet(contp, reinterpret_cast<void *>(0xDEADDEAD));
  TSContDestroy(contp);
}

int
handleTransformationPluginRead(TSCont contp, TransformationPluginState *state)
{
  // Traffic Server naming is quite confusing, in this context the write_vio
  // is actually the vio we read from.
  TSVIO write_vio = TSVConnWriteVIOGet(contp);
  if (write_vio) {
    if (state->paused_) {
      LOG_DEBUG("Transformation contp=%p write_vio=%p, is paused", contp, write_vio);
      return 0;
    }

    int64_t to_read = TSVIONTodoGet(write_vio);
    LOG_DEBUG("Transformation contp=%p write_vio=%p, to_read=%" PRId64, contp, write_vio, to_read);

    if (to_read > 0) {
      /*
       * The amount of data left to read needs to be truncated by
       * the amount of data actually in the read buffer.
       **/
      int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
      LOG_DEBUG("Transformation contp=%p write_vio=%p, to_read=%" PRId64 ", buffer reader avail=%" PRId64, contp, write_vio,
                to_read, avail);

      if (to_read > avail) {
        to_read = avail;
        LOG_DEBUG("Transformation contp=%p write_vio=%p, to read > avail, fixing to_read to be equal to avail. to_read=%" PRId64
                  ", buffer reader avail=%" PRId64,
                  contp, write_vio, to_read, avail);
      }

      if (to_read > 0) {
        /* Create a buffer and a buffer reader */
        TSIOBuffer input_buffer       = TSIOBufferCreate();
        TSIOBufferReader input_reader = TSIOBufferReaderAlloc(input_buffer);

        /* Copy the data from the read buffer to the input buffer. */
        TSIOBufferCopy(input_buffer, TSVIOReaderGet(write_vio), to_read, 0);

        /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
        TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), to_read);

        /* Modify the read VIO to reflect how much data we've completed. */
        TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + to_read);

        std::string in_data = utils::internal::consumeFromTSIOBufferReader(input_reader);
        LOG_DEBUG("Transformation contp=%p write_vio=%p consumed %ld bytes from bufferreader", contp, write_vio, in_data.length());

        /* Clean up the buffer and reader */
        TSIOBufferReaderFree(input_reader);
        TSIOBufferDestroy(input_buffer);

        /* Now call the client to tell them about data */
        if (in_data.length() > 0) {
          state->transformation_plugin_.consume(in_data);
        }
      }

      /* now that we've finished reading we will check if there is anything left to read. */
      TSCont vio_cont = TSVIOContGet(write_vio); // for some reason this can occasionally be null

      if (TSVIONTodoGet(write_vio) > 0) {
        LOG_DEBUG("Transformation contp=%p write_vio=%p, vio_cont=%p still has bytes left to process, todo > 0.", contp, write_vio,
                  vio_cont);

        if (to_read > 0) {
          TSVIOReenable(write_vio);

          /* Call back the read VIO continuation to let it know that we are ready for more data. */
          if (vio_cont) {
            TSContCall(vio_cont, static_cast<TSEvent>(TS_EVENT_VCONN_WRITE_READY), write_vio);
          }
        }
      } else {
        LOG_DEBUG("Transformation contp=%p write_vio=%p, vio_cont=%p has no bytes left to process, will send WRITE_COMPLETE.",
                  contp, write_vio, vio_cont);

        /* Call back the write VIO continuation to let it know that we have completed the write operation. */
        if (!state->input_complete_dispatched_) {
          state->transformation_plugin_.handleInputComplete();
          state->input_complete_dispatched_ = true;
          if (vio_cont && nullptr != TSVIOBufferGet(write_vio)) {
            TSContCall(vio_cont, static_cast<TSEvent>(TS_EVENT_VCONN_WRITE_COMPLETE), write_vio);
          }
        }
      }
    } else {
      TSCont vio_cont = TSVIOContGet(write_vio); // for some reason this can occasionally be null?
      LOG_DEBUG("Transformation contp=%p write_vio=%p, vio_cont=%p has no bytes left to process.", contp, write_vio, vio_cont);

      /* Call back the write VIO continuation to let it know that we have completed the write operation. */
      if (!state->input_complete_dispatched_) {
        state->transformation_plugin_.handleInputComplete();
        state->input_complete_dispatched_ = true;
        if (vio_cont && nullptr != TSVIOBufferGet(write_vio)) {
          TSContCall(vio_cont, static_cast<TSEvent>(TS_EVENT_VCONN_WRITE_COMPLETE), write_vio);
        }
      }
    }
  } else {
    LOG_ERROR("Transformation contp=%p write_vio=%p was nullptr!", contp, write_vio);
  }
  return 0;
}

int
handleTransformationPluginEvents(TSCont contp, TSEvent event, void *edata)
{
  TransformationPluginState *state = static_cast<TransformationPluginState *>(TSContDataGet(contp));
  LOG_DEBUG("Transformation contp=%p event=%d edata=%p tshttptxn=%p", contp, event, edata, state->txn_);

  // The first thing you always do is check if the VConn is closed.
  int connection_closed = TSVConnClosedGet(state->vconn_);
  if (connection_closed) {
    LOG_DEBUG("Transformation contp=%p tshttptxn=%p is closed connection_closed=%d ", contp, state->txn_, connection_closed);
    // we'll do the cleanupTransformation in the TransformationPlugin destructor.
    return 0;
  }

  if (event == TS_EVENT_VCONN_WRITE_COMPLETE) {
    TSVConn output_vconn = TSTransformOutputVConnGet(state->vconn_);
    LOG_DEBUG("Transformation contp=%p tshttptxn=%p received WRITE_COMPLETE, shutting down outputvconn=%p ", contp, state->txn_,
              output_vconn);
    TSVConnShutdown(output_vconn, 0, 1); // The other end is done reading our output
    return 0;
  } else if (event == TS_EVENT_ERROR) {
    TSVIO write_vio;
    /* Get the write VIO for the write operation that was
     performed on ourself. This VIO contains the continuation of
     our parent transformation. */
    write_vio       = TSVConnWriteVIOGet(state->vconn_);
    TSCont vio_cont = TSVIOContGet(write_vio);
    LOG_ERROR("Transformation contp=%p tshttptxn=%p received EVENT_ERROR forwarding to write_vio=%p viocont=%p", contp, state->txn_,
              write_vio, vio_cont);
    if (vio_cont) {
      TSContCall(vio_cont, TS_EVENT_ERROR, write_vio);
    }
    return 0;
  }

  // All other events including WRITE_READY will just attempt to transform more data.
  return handleTransformationPluginRead(state->vconn_, state);
}

} /* anonymous namespace */

TransformationPlugin::TransformationPlugin(Transaction &transaction, TransformationPlugin::Type type)
  : TransactionPlugin(transaction), state_(nullptr)
{
  state_         = new TransformationPluginState(transaction, *this, type, static_cast<TSHttpTxn>(transaction.getAtsHandle()));
  state_->vconn_ = TSTransformCreate(handleTransformationPluginEvents, state_->txn_);
  TSContDataSet(state_->vconn_, static_cast<void *>(state_)); // edata in a TransformationHandler is NOT a TSHttpTxn.
  LOG_DEBUG("Creating TransformationPlugin=%p (vconn)contp=%p tshttptxn=%p transformation_type=%d", this, state_->vconn_,
            state_->txn_, type);
  TSHttpTxnHookAdd(state_->txn_, utils::internal::convertInternalTransformationTypeToTsHook(type), state_->vconn_);
}

TransformationPlugin::~TransformationPlugin()
{
  LOG_DEBUG("Destroying TransformationPlugin=%p", this);
  cleanupTransformation(state_->vconn_);
  delete state_;
}

void
TransformationPlugin::pause()
{
  if (state_->paused_) {
    LOG_ERROR("Can not pause transformation, already paused  TransformationPlugin=%p (vconn)contp=%p tshttptxn=%p", this,
              state_->vconn_, state_->txn_);
  } else if (state_->input_complete_dispatched_) {
    LOG_ERROR("Can not pause transformation (transformation completed) TransformationPlugin=%p (vconn)contp=%p tshttptxn=%p", this,
              state_->vconn_, state_->txn_);
  } else {
    state_->paused_ = true;
    if (!static_cast<bool>(static_cast<ResumeAfterPauseCont *>(state_))) {
      *static_cast<ResumeAfterPauseCont *>(state_) = ResumeAfterPauseCont(TSContMutexGet(reinterpret_cast<TSCont>(state_->txn_)));
    }
  }
}

bool
TransformationPlugin::isPaused() const
{
  return state_->paused_;
}

Continuation &
TransformationPlugin::resumeCont()
{
  TSReleaseAssert(state_->paused_);

  // The cast to a pointer to the intermediate base class ResumeAfterPauseCont is not strictly necessary.  It is
  // possible that the transform plugin might want to defer work to other continuations in the future.  This would
  // naturally result in TransactionPluginState having Continuation as an indirect base class multiple times, making
  // disambiguation necessary when converting.
  //
  return *static_cast<ResumeAfterPauseCont *>(state_);
}

int
ResumeAfterPauseCont::_run(TSEvent event, void *edata)
{
  auto state     = static_cast<TransformationPluginState *>(this);
  state->paused_ = false;
  handleTransformationPluginRead(state->vconn_, state);

  return TS_SUCCESS;
}

size_t
TransformationPlugin::doProduce(std::string_view data)
{
  LOG_DEBUG("TransformationPlugin=%p tshttptxn=%p producing output with length=%ld", this, state_->txn_, data.length());
  int64_t write_length = static_cast<int64_t>(data.length());
  if (!write_length) {
    return 0;
  }

  if (!state_->output_vio_) {
    TSVConn output_vconn = TSTransformOutputVConnGet(state_->vconn_);
    LOG_DEBUG("TransformationPlugin=%p tshttptxn=%p will issue a TSVConnWrite, output_vconn=%p.", this, state_->txn_, output_vconn);
    if (output_vconn) {
      // If you're confused about the following reference the traffic server transformation docs.
      // You always write INT64_MAX, this basically says you're not sure how much data you're going to write
      state_->output_vio_ = TSVConnWrite(output_vconn, state_->vconn_, state_->output_buffer_reader_, INT64_MAX);
    } else {
      LOG_ERROR("TransformationPlugin=%p tshttptxn=%p output_vconn=%p cannot issue TSVConnWrite due to null output vconn.", this,
                state_->txn_, output_vconn);
      return 0;
    }

    if (!state_->output_vio_) {
      LOG_ERROR("TransformationPlugin=%p tshttptxn=%p state_->output_vio=%p, TSVConnWrite failed.", this, state_->txn_,
                state_->output_vio_);
      return 0;
    }
  }

  // Finally we can copy this data into the output_buffer
  int64_t bytes_written = TSIOBufferWrite(state_->output_buffer_, data.data(), write_length);
  state_->bytes_written_ += bytes_written; // So we can set BytesDone on outputComplete().
  LOG_DEBUG("TransformationPlugin=%p tshttptxn=%p write to TSIOBuffer %" PRId64 " bytes total bytes written %" PRId64, this,
            state_->txn_, bytes_written, state_->bytes_written_);

  // Sanity Checks
  if (bytes_written != write_length) {
    LOG_ERROR("TransformationPlugin=%p tshttptxn=%p bytes written < expected. bytes_written=%" PRId64 " write_length=%" PRId64,
              this, state_->txn_, bytes_written, write_length);
  }

  int connection_closed = TSVConnClosedGet(state_->vconn_);
  LOG_DEBUG("TransformationPlugin=%p tshttptxn=%p vconn=%p connection_closed=%d", this, state_->txn_, state_->vconn_,
            connection_closed);

  if (!connection_closed) {
    TSVIOReenable(state_->output_vio_); // Wake up the downstream vio
  } else {
    LOG_ERROR(
      "TransformationPlugin=%p tshttptxn=%p output_vio=%p connection_closed=%d : Couldn't reenable output vio (connection closed).",
      this, state_->txn_, state_->output_vio_, connection_closed);
  }

  return static_cast<size_t>(bytes_written);
}

size_t
TransformationPlugin::produce(std::string_view data)
{
  if (state_->type_ == REQUEST_TRANSFORMATION) {
    state_->request_xform_output_.append(data.data(), data.length());
    return data.size();
  } else if (state_->type_ == SINK_TRANSFORMATION) {
    LOG_DEBUG("produce TransformationPlugin=%p tshttptxn=%p : This is a sink transform. Not producing any output", this,
              state_->txn_);
    return 0;
  } else {
    return doProduce(data);
  }
}

size_t
TransformationPlugin::setOutputComplete()
{
  if (state_->type_ == SINK_TRANSFORMATION) {
    // There's no output stream for a sink transform, so we do nothing
    //
    // Warning: don't try to shutdown the VConn, since the default implementation (DummyVConnection)
    // has a stubbed out shutdown/close implementation
    return 0;
  } else if (state_->type_ == REQUEST_TRANSFORMATION) {
    doProduce(state_->request_xform_output_);
  }

  int connection_closed = TSVConnClosedGet(state_->vconn_);
  LOG_DEBUG("OutputComplete TransformationPlugin=%p tshttptxn=%p vconn=%p connection_closed=%d, total bytes written=%" PRId64, this,
            state_->txn_, state_->vconn_, connection_closed, state_->bytes_written_);

  if (!connection_closed && !state_->output_vio_) {
    LOG_DEBUG("TransformationPlugin=%p tshttptxn=%p output complete without writing any data, initiating write of 0 bytes.", this,
              state_->txn_);

    // We're done without ever outputting anything, to correctly
    // clean up we'll initiate a write then immediately set it to 0 bytes done.
    state_->output_vio_ = TSVConnWrite(TSTransformOutputVConnGet(state_->vconn_), state_->vconn_, state_->output_buffer_reader_, 0);

    if (state_->output_vio_) {
      TSVIONDoneSet(state_->output_vio_, 0);
      TSVIOReenable(state_->output_vio_); // Wake up the downstream vio
    } else {
      LOG_ERROR("TransformationPlugin=%p tshttptxn=%p unable to reenable output_vio=%p because VConnWrite failed.", this,
                state_->txn_, state_->output_vio_);
    }

    return 0;
  }

  if (!connection_closed) {
    // So there is a possible race condition here, if we wake up a dead
    // VIO it can cause a segfault, so we must check that the VCONN is not dead.
    int connection_closed = TSVConnClosedGet(state_->vconn_);
    if (!connection_closed) {
      TSVIONBytesSet(state_->output_vio_, state_->bytes_written_);
      TSVIOReenable(state_->output_vio_); // Wake up the downstream vio
    } else {
      LOG_ERROR("TransformationPlugin=%p tshttptxn=%p unable to reenable output_vio=%p connection was closed=%d.", this,
                state_->txn_, state_->output_vio_, connection_closed);
    }
  } else {
    LOG_ERROR("TransformationPlugin=%p tshttptxn=%p unable to reenable output_vio=%p connection was closed=%d.", this, state_->txn_,
              state_->output_vio_, connection_closed);
  }

  return state_->bytes_written_;
}
