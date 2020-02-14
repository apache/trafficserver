/* Licensed to the Apache Software Foundation (ASF) under one
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

#include <cstring>

// Unit Test for API: TSHttpTxnTransformRespGet
//                    TSHttpTxnTransformedRespCache
//                    TSHttpTxnUntransformedRespCache
//
namespace TransformTest
{
Logger log;

struct ContData {
  bool good{true};
  void
  test(bool result)
  {
    good = good && result;
  }

  bool transform_created{false};
};

/** Append Transform Data Structure **/
struct AppendTransformTestData {
  TSVIO output_vio               = nullptr;
  TSIOBuffer output_buffer       = nullptr;
  TSIOBufferReader output_reader = nullptr;
  ContData *test_data            = nullptr;
  int append_needed              = 1;

  ~AppendTransformTestData()
  {
    if (output_buffer) {
      TSIOBufferDestroy(output_buffer);
    }
  }
};

/**** Append Transform Code (Tailored to needs)****/

TSIOBuffer append_buffer;
TSIOBufferReader append_buffer_reader;
int64_t append_buffer_length;

void
handle_transform(TSCont contp)
{
  TSVConn output_conn;
  TSVIO write_vio;
  int64_t towrite;
  int64_t avail;

  /* Get the output connection where we'll write data to. */
  output_conn = TSTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer.
  */
  auto *data = static_cast<AppendTransformTestData *>(TSContDataGet(contp));
  if (!data->output_buffer) {
    towrite = TSVIONBytesGet(write_vio);
    if (towrite != INT64_MAX) {
      towrite += append_buffer_length;
    }
    data->output_buffer = TSIOBufferCreate();
    data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
    data->output_vio    = TSVConnWrite(output_conn, contp, data->output_reader, towrite);
  }
  TSReleaseAssert(data->output_vio);

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this simplistic
     transformation that means we're done. In a more complex
     transformation we might have to finish writing the transformed
     data to our output connection. */
  if (!TSVIOBufferGet(write_vio)) {
    if (data->append_needed) {
      data->append_needed = 0;
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio) + append_buffer_length);
    TSVIOReenable(data->output_vio);
    return;
  }

  /* Determine how much data we have left to read. For this append
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
         connection by reenabling the output VIO. This will wakeup
         the output connection and allow it to consume data from the
         output buffer. */
      TSVIOReenable(data->output_vio);

      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    if (data->append_needed) {
      data->append_needed = 0;
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    /* If there is no data left to read, then we modify the output
       VIO to reflect how much data the output connection should
       expect. This allows the output connection to know when it
       is done reading. We then reenable the output connection so
       that it can consume the data we just gave it. */
    TSVIONBytesSet(data->output_vio, TSVIONDoneGet(write_vio) + append_buffer_length);
    TSVIOReenable(data->output_vio);

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
}

int
transformtest_transform(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  auto *data = static_cast<AppendTransformTestData *>(TSContDataGet(contp));
  if (!data->test_data->transform_created) {
    data->test_data->transform_created = true;
    log("TSTransformCreate -- function ran -- ok");
  }
  /* Check to see if the transformation has been closed by a call to
     TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    delete data;
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      TSVIO write_vio;

      /* Get the write VIO for the write operation that was
         performed on ourself. This VIO contains the continuation of
         our parent transformation. */
      write_vio = TSVConnWriteVIOGet(contp);

      /* Call back the write VIO continuation to let it know that we
         have completed the write operation. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_ERROR, write_vio);
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
    default:
      /* If we get a WRITE_READY event or any other type of
         event (sent, perhaps, because we were reenabled) then
         we'll attempt to transform more data. */
      handle_transform(contp);
      break;
    }
  }

  return 0;
}

int
transformable(TSHttpTxn txnp, ContData *data)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int ret = 0;

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    log("TSHttpTxnTransform -- [transformable]: TSHttpTxnServerRespGet return 0");
    return ret;
  }

  /*
   *  We are only interested in "200 OK" responses.
   */

  if (TS_HTTP_STATUS_OK == TSHttpHdrStatusGet(bufp, hdr_loc)) {
    ret = 1;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return ret; /* not a 200 */
}

void
transform_add(TSHttpTxn txnp, ContData *test_data)
{
  TSVConn connp = TSTransformCreate(transformtest_transform, txnp);
  if (connp == nullptr) {
    log("TSHttpTxnTransform -- Unable to create Transformation.");
    return;
  }

  // Add data to the continuation
  auto *data      = new AppendTransformTestData;
  data->test_data = test_data;
  TSContDataSet(connp, data);

  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
  return;
}

int
load(const char *append_string)
{
  TSIOBufferBlock blk;
  char *p;
  int64_t avail;

  append_buffer        = TSIOBufferCreate();
  append_buffer_reader = TSIOBufferReaderAlloc(append_buffer);

  blk = TSIOBufferStart(append_buffer);
  p   = TSIOBufferBlockWriteStart(blk, &avail);

  TSstrlcpy(p, append_string, avail);
  if (append_string != nullptr) {
    TSIOBufferProduce(append_buffer, strlen(append_string));
  }

  append_buffer_length = TSIOBufferReaderAvail(append_buffer_reader);

  return 1;
}

/**** Append Transform Code Ends ****/

TSCont cont{nullptr};

// Depending on the timing of the DNS response, OS_DNS can happen before or after CACHE_LOOKUP.
//
int
contFunc(TSCont contp, TSEvent event, void *event_data)
{
  TSReleaseAssert(event_data != nullptr);

  auto txn = static_cast<TSHttpTxn>(event_data);

  auto txn_id = GetTxnID(txn).txn_id();

  int txn_number;

  if (("TRANSFORM1" == txn_id) || ("TRANSFORM1_DUP" == txn_id)) {
    txn_number = 4;

  } else if (("TRANSFORM2" == txn_id) || ("TRANSFORM2_DUP" == txn_id)) {
    txn_number = 5;

  } else {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSReleaseAssert(contp == cont);

  auto data = static_cast<ContData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSSkipRemappingSet(txn, 1);
  } break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    /* Setup hooks for Transformation */
    if (transformable(txn, data)) {
      transform_add(txn, data);
    }
    /* Call TransformedRespCache or UntransformedRespCache depending on request */
    {
      TSMBuffer bufp;
      TSMLoc hdr;
      TSMLoc field;

      if (TSHttpTxnClientReqGet(txn, &bufp, &hdr) != TS_SUCCESS) {
        log("TSHttpTxnTransform -- TSHttpTxnClientReqGet did not return TS_SUCCESS -- fail");
      } else {
        if (TS_NULL_MLOC == (field = TSMimeHdrFieldFind(bufp, hdr, "Request", -1))) {
          log("TSHttpTxnTransform -- Didn't find field request -- ");
        } else {
          int reqid = TSMimeHdrFieldValueIntGet(bufp, hdr, field, 0);
          if (reqid == 1) {
            TSHttpTxnTransformedRespCache(txn, 0);
            TSHttpTxnUntransformedRespCache(txn, 1);

          } else if (reqid == 2) {
            TSHttpTxnTransformedRespCache(txn, 1);
            TSHttpTxnUntransformedRespCache(txn, 0);
          } else {
            log("TSHttpTxnTransform -- Bad request ID %d -- fail", reqid);
          }
          if (TSHandleMLocRelease(bufp, hdr, field) != TS_SUCCESS) {
            log("TSHttpTxnTransform -- Unable to release handle to field in Client request -- fail");
          }
        }
        if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr) != TS_SUCCESS) {
          log("TSHttpTxnTransform -- Unable to release handle to Client request -- fail");
        }
      }
    }

    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
    TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, cont);
  } break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnTransformRespGet, "transform response", txn_number, TS_HTTP_STATUS_OK));
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    if (data->transform_created) {
      log("Transform created -- ok");

    } else {
      log("Transform creation -- falied");
    }

    log(data->good ? "Transform test -- ok" : "Transform test -- failed");
    log.flush();
  } break;

  default:
    TSError("Unexpected event %d", event);
    TSReleaseAssert(false);
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
init()
{
  log.open(run_dir_path + "/TransformTest.tlog");

  cont = TSContCreate(contFunc, nullptr);

  auto data = static_cast<ContData *>(TSmalloc(sizeof(ContData)));

  ::new (data) ContData;

  TSContDataSet(cont, data);

  /* Prepare the buffer to be appended to responses */
  load("\nThis is a transformed response");

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont); // so we can skip remapping

  /* Register to HTTP hooks that are called in case of a cache MISS */
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
}

void
cleanup()
{
  TSfree(TSContDataGet(cont));

  TSContDestroy(cont);

  log.close();
}

} // namespace TransformTest
