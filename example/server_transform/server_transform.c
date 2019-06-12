/** @file

  @brief An example program that sends response content to a server to be transformed and sends the
         transformed content to the client.

  The protocol spoken with the server is simple. The plugin sends the
  content-length of the document being transformed as a 4-byte
  integer and then it sends the document itself. The first 4-bytes of
  the server response are a status code/content length. If the code
  is greater than 0 then the plugin assumes transformation was
  successful and uses the code as the content length of the
  transformed document. If the status code is less than or equal to 0
  then the plugin bypasses transformation and sends the original
  document on through.

  The plugin does a fair amount of error checking and tries to bypass
  transformation in many cases such as when it can't connect to the
  server. This example plugin simply connects to port 7 on localhost,
  which on our solaris machines (and most unix machines) is the echo
  port. One nicety about the protocol is that simply having the
  server echo back what it is sent results in a "null"
  transformation. (i.e. A transformation which does not modify the
  content).

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

#include <string.h>
#include <stdio.h>

#include <netinet/in.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#define PLUGIN_NAME "server-transform"

#define STATE_BUFFER 1
#define STATE_CONNECT 2
#define STATE_WRITE 3
#define STATE_READ_STATUS 4
#define STATE_READ 5
#define STATE_BYPASS 6

typedef struct {
  int state;
  TSHttpTxn txn;

  TSIOBuffer input_buf;
  TSIOBufferReader input_reader;

  TSIOBuffer output_buf;
  TSIOBufferReader output_reader;
  TSVConn output_vc;
  TSVIO output_vio;

  TSAction pending_action;
  TSVConn server_vc;
  TSVIO server_vio;

  int content_length;
} TransformData;

static int transform_handler(TSCont contp, TSEvent event, void *edata);

static in_addr_t server_ip;
static int server_port;

static TSCont
transform_create(TSHttpTxn txnp)
{
  TSCont contp;
  TransformData *data;

  contp = TSTransformCreate(transform_handler, txnp);

  data                 = (TransformData *)TSmalloc(sizeof(TransformData));
  data->state          = STATE_BUFFER;
  data->txn            = txnp;
  data->input_buf      = NULL;
  data->input_reader   = NULL;
  data->output_buf     = NULL;
  data->output_reader  = NULL;
  data->output_vio     = NULL;
  data->output_vc      = NULL;
  data->pending_action = NULL;
  data->server_vc      = NULL;
  data->server_vio     = NULL;
  data->content_length = 0;

  TSContDataSet(contp, data);
  return contp;
}

static void
transform_destroy(TSCont contp)
{
  TransformData *data;

  data = TSContDataGet(contp);
  if (data != NULL) {
    if (data->input_buf) {
      TSIOBufferDestroy(data->input_buf);
    }

    if (data->output_buf) {
      TSIOBufferDestroy(data->output_buf);
    }

    if (data->pending_action) {
      TSActionCancel(data->pending_action);
    }

    if (data->server_vc) {
      TSVConnAbort(data->server_vc, 1);
    }

    TSfree(data);
  } else {
    TSError("[%s] Unable to get Continuation's Data. TSContDataGet returns NULL", PLUGIN_NAME);
  }

  TSContDestroy(contp);
}

static int
transform_connect(TSCont contp, TransformData *data)
{
  TSAction action;
  int content_length;
  struct sockaddr_in ip_addr;

  data->state = STATE_CONNECT;

  content_length = TSIOBufferReaderAvail(data->input_reader);
  if (content_length >= 0) {
    data->content_length = content_length;
    data->content_length = htonl(data->content_length);

    /* Prepend the content length to the buffer.
     * If we decide to not send the content to the transforming
     * server then we need to make sure and skip input_reader
     * over the content length.
     */

    {
      TSIOBuffer temp;
      TSIOBufferReader tempReader;

      temp       = TSIOBufferCreate();
      tempReader = TSIOBufferReaderAlloc(temp);

      TSIOBufferWrite(temp, (const char *)&content_length, sizeof(int));
      TSIOBufferCopy(temp, data->input_reader, content_length, 0);

      TSIOBufferReaderFree(data->input_reader);
      TSIOBufferDestroy(data->input_buf);
      data->input_buf    = temp;
      data->input_reader = tempReader;
    }
  } else {
    TSError("[%s] TSIOBufferReaderAvail returns TS_ERROR", PLUGIN_NAME);
    return 0;
  }

  /* TODO: This only supports IPv4, probably should be changed at some point, but
     it's an example ... */
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = server_ip; /* Should be in network byte order */
  ip_addr.sin_port        = server_port;
  TSDebug(PLUGIN_NAME, "net connect.");
  action = TSNetConnect(contp, (struct sockaddr const *)&ip_addr);

  if (!TSActionDone(action)) {
    data->pending_action = action;
  }

  return 0;
}

static int
transform_write(TSCont contp, TransformData *data)
{
  int content_length;

  data->state = STATE_WRITE;

  content_length = TSIOBufferReaderAvail(data->input_reader);
  if (content_length >= 0) {
    data->server_vio = TSVConnWrite(data->server_vc, contp, TSIOBufferReaderClone(data->input_reader), content_length);
  } else {
    TSError("[%s] TSIOBufferReaderAvail returns TS_ERROR", PLUGIN_NAME);
  }
  return 0;
}

static int
transform_read_status(TSCont contp, TransformData *data)
{
  data->state = STATE_READ_STATUS;

  data->output_buf    = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
  if (data->output_reader != NULL) {
    data->server_vio = TSVConnRead(data->server_vc, contp, data->output_buf, sizeof(int));
  } else {
    TSError("[%s] Error in Allocating a Reader to output buffer. TSIOBufferReaderAlloc returns NULL", PLUGIN_NAME);
  }

  return 0;
}

static int
transform_read(TSCont contp, TransformData *data)
{
  data->state = STATE_READ;

  TSIOBufferDestroy(data->input_buf);
  data->input_buf    = NULL;
  data->input_reader = NULL;

  data->server_vio = TSVConnRead(data->server_vc, contp, data->output_buf, data->content_length);
  data->output_vc  = TSTransformOutputVConnGet((TSVConn)contp);
  if (data->output_vc == NULL) {
    TSError("[%s] TSTransformOutputVConnGet returns NULL", PLUGIN_NAME);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, data->content_length);
    if (data->output_vio == NULL) {
      TSError("[%s] TSVConnWrite returns NULL", PLUGIN_NAME);
    }
  }

  return 0;
}

static int
transform_bypass(TSCont contp, TransformData *data)
{
  data->state = STATE_BYPASS;

  if (data->server_vc) {
    TSVConnAbort(data->server_vc, 1);
    data->server_vc  = NULL;
    data->server_vio = NULL;
  }

  if (data->output_buf) {
    TSIOBufferDestroy(data->output_buf);
    data->output_buf    = NULL;
    data->output_reader = NULL;
  }

  TSIOBufferReaderConsume(data->input_reader, sizeof(int));
  data->output_vc = TSTransformOutputVConnGet((TSVConn)contp);
  if (data->output_vc == NULL) {
    TSError("[%s] TSTransformOutputVConnGet returns NULL", PLUGIN_NAME);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->input_reader, TSIOBufferReaderAvail(data->input_reader));
    if (data->output_vio == NULL) {
      TSError("[%s] TSVConnWrite returns NULL", PLUGIN_NAME);
    }
  }
  return 1;
}

static int
transform_buffer_event(TSCont contp, TransformData *data, TSEvent event ATS_UNUSED, void *edata ATS_UNUSED)
{
  TSVIO write_vio;
  int towrite;

  if (!data->input_buf) {
    data->input_buf    = TSIOBufferCreate();
    data->input_reader = TSIOBufferReaderAlloc(data->input_buf);
  }

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */
  if (!TSVIOBufferGet(write_vio)) {
    return transform_connect(contp, data);
  }

  /* Determine how much data we have left to read. For this server
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    int avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      TSIOBufferCopy(data->input_buf, TSVIOReaderGet(write_vio), towrite, 0);

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
    /* Call back the write VIO continuation to let it know that we
       are ready for more data. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
  } else {
    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);

    /* start compression... */
    return transform_connect(contp, data);
  }

  return 0;
}

static int
transform_connect_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_NET_CONNECT:
    TSDebug(PLUGIN_NAME, "connected");

    data->pending_action = NULL;
    data->server_vc      = (TSVConn)edata;
    return transform_write(contp, data);
  case TS_EVENT_NET_CONNECT_FAILED:
    TSDebug(PLUGIN_NAME, "connect failed");
    data->pending_action = NULL;
    return transform_bypass(contp, data);
  default:
    break;
  }

  return 0;
}

static int
transform_write_event(TSCont contp, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    TSVIOReenable(data->server_vio);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return transform_read_status(contp, data);
  case TS_EVENT_ERROR:
    return transform_bypass(contp, data);
  case TS_EVENT_IMMEDIATE:
    TSVIOReenable(data->server_vio);
    break;
  default:
    /* An error occurred while writing to the server. Close down
       the connection to the server and bypass. */
    return transform_bypass(contp, data);
  }

  return 0;
}

static int
transform_read_status_event(TSCont contp, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
{
  switch (event) {
  case TS_EVENT_ERROR:
  case TS_EVENT_VCONN_EOS:
    return transform_bypass(contp, data);
  case TS_EVENT_VCONN_READ_COMPLETE:
    if (TSIOBufferReaderAvail(data->output_reader) == sizeof(int)) {
      void *buf_ptr;
      int64_t avail;
      int64_t read_nbytes = sizeof(int);

      buf_ptr = &data->content_length;
      while (read_nbytes > 0) {
        TSIOBufferBlock blk = TSIOBufferReaderStart(data->output_reader);
        char *buf           = (char *)TSIOBufferBlockReadStart(blk, data->output_reader, &avail);
        int64_t read_ndone  = (avail >= read_nbytes) ? read_nbytes : avail;

        memcpy(buf_ptr, buf, read_ndone);
        if (read_ndone > 0) {
          TSIOBufferReaderConsume(data->output_reader, read_ndone);
          read_nbytes -= read_ndone;
          /* move ptr frwd by read_ndone bytes */
          buf_ptr = (char *)buf_ptr + read_ndone;
        }
      }
      // data->content_length = ntohl(data->content_length);
      return transform_read(contp, data);
    }
    return transform_bypass(contp, data);
  default:
    break;
  }

  return 0;
}

static int
transform_read_event(TSCont contp ATS_UNUSED, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
{
  switch (event) {
  case TS_EVENT_ERROR:
    TSVConnAbort(data->server_vc, 1);
    data->server_vc  = NULL;
    data->server_vio = NULL;

    TSVConnAbort(data->output_vc, 1);
    data->output_vc  = NULL;
    data->output_vio = NULL;
    break;
  case TS_EVENT_VCONN_EOS:
    TSVConnAbort(data->server_vc, 1);
    data->server_vc  = NULL;
    data->server_vio = NULL;

    TSVConnAbort(data->output_vc, 1);
    data->output_vc  = NULL;
    data->output_vio = NULL;
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
    TSVConnClose(data->server_vc);
    data->server_vc  = NULL;
    data->server_vio = NULL;

    TSVIOReenable(data->output_vio);
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSVIOReenable(data->output_vio);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    TSVIOReenable(data->server_vio);
    break;
  default:
    break;
  }

  return 0;
}

static int
transform_bypass_event(TSCont contp ATS_UNUSED, TransformData *data, TSEvent event, void *edata ATS_UNUSED)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
  default:
    TSVIOReenable(data->output_vio);
    break;
  }

  return 0;
}

static int
transform_handler(TSCont contp, TSEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to
     TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    TSDebug(PLUGIN_NAME, "transformation closed");
    transform_destroy(contp);
    return 0;
  } else {
    TransformData *data;
    int val = 0;

    data = (TransformData *)TSContDataGet(contp);
    if (data == NULL) {
      TSError("[%s] Didn't get Continuation's Data, ignoring event", PLUGIN_NAME);
      return 0;
    }
    TSDebug(PLUGIN_NAME, "transform handler event [%d], data->state = [%d]", event, data->state);

    do {
      switch (data->state) {
      case STATE_BUFFER:
        val = transform_buffer_event(contp, data, event, edata);
        break;
      case STATE_CONNECT:
        val = transform_connect_event(contp, data, event, edata);
        break;
      case STATE_WRITE:
        val = transform_write_event(contp, data, event, edata);
        break;
      case STATE_READ_STATUS:
        val = transform_read_status_event(contp, data, event, edata);
        break;
      case STATE_READ:
        val = transform_read_event(contp, data, event, edata);
        break;
      case STATE_BYPASS:
        val = transform_bypass_event(contp, data, event, edata);
        break;
      }
    } while (val);
  }

  return 0;
}

static int
request_ok(TSHttpTxn txnp ATS_UNUSED)
{
  /* Is the initial client request OK for transformation. This is a
     good place to check accept headers to see if the client can
     accept a transformed document. */
  return 1;
}

static int
cache_response_ok(TSHttpTxn txnp ATS_UNUSED)
{
  /* Is the response we're reading from cache OK for
   * transformation. This is a good place to check the cached
   * response to see if it is transformable. The default
   * behavior is to cache transformed content; therefore
   * to avoid transforming twice we will not transform
   * content served from the cache.
   */
  return 0;
}

static int
server_response_ok(TSHttpTxn txnp)
{
  /* Is the response the server sent OK for transformation. This is
   * a good place to check the server's response to see if it is
   * transformable. In this example, we will transform only "200 OK"
   * responses.
   */

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Unable to get handle to Server Response", PLUGIN_NAME);
    return 0;
  }

  resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
  if (TS_HTTP_STATUS_OK == resp_status) {
    if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to release handle to server request", PLUGIN_NAME);
    }
    return 1;
  } else {
    if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to release handle to server request", PLUGIN_NAME);
    }
    return 0;
  }
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (request_ok(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_CACHE_HDR_HOOK, contp);
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_CACHE_HDR:
    if (cache_response_ok(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform_create(txnp));
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (server_response_ok(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform_create(txnp));
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;
  TSCont cont;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  /* connect to the echo port on localhost */
  server_ip   = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  server_ip   = htonl(server_ip);
  server_port = 7;

  cont = TSContCreate(transform_plugin, NULL);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
}
