/** @file

  A brief file description

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

/* server-transform.c:  an example program that sends response content 
 *                      to a server to be transformed, and sends the 
 *                      transformed content to the client 
 *
 *
 *	Usage:	
 *	(NT): ServerTransform.dll 
 *	(Solaris): server-transform.so
 *
 *
 */

/* The protocol spoken with the server is simple. The plugin sends the
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
   content). */

#include <string.h>
#include <stdio.h>

#if !defined (_WIN32)
#  include <netinet/in.h>
#else
#  include <windows.h>
#endif

#include <ts/ts.h>

#define STATE_BUFFER       1
#define STATE_CONNECT      2
#define STATE_WRITE        3
#define STATE_READ_STATUS  4
#define STATE_READ         5
#define STATE_BYPASS       6

typedef struct
{
  int state;
  INKHttpTxn txn;

  INKIOBuffer input_buf;
  INKIOBufferReader input_reader;

  INKIOBuffer output_buf;
  INKIOBufferReader output_reader;
  INKVConn output_vc;
  INKVIO output_vio;

  INKAction pending_action;
  INKVConn server_vc;
  INKVIO server_vio;

  int content_length;
} TransformData;

static INKCont transform_create(INKHttpTxn txnp);
static void transform_destroy(INKCont contp);
static int transform_connect(INKCont contp, TransformData * data);
static int transform_write(INKCont contp, TransformData * data);
static int transform_read_status(INKCont contp, TransformData * data);
static int transform_read(INKCont contp, TransformData * data);
static int transform_bypass(INKCont contp, TransformData * data);
static int transform_buffer_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_connect_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_write_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_read_status_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_read_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_bypass_event(INKCont contp, TransformData * data, INKEvent event, void *edata);
static int transform_handler(INKCont contp, INKEvent event, void *edata);

#if !defined (_WIN32)
static in_addr_t server_ip;
#else
static unsigned int server_ip;
#endif

static int server_port;

static INKCont
transform_create(INKHttpTxn txnp)
{
  INKCont contp;
  TransformData *data;

  if ((contp = INKTransformCreate(transform_handler, txnp)) == INK_ERROR_PTR) {
    INKError("Error in creating Transformation. Retyring...");
    return NULL;
  }

  data = (TransformData *) INKmalloc(sizeof(TransformData));
  data->state = STATE_BUFFER;
  data->txn = txnp;
  data->input_buf = NULL;
  data->input_reader = NULL;
  data->output_buf = NULL;
  data->output_reader = NULL;
  data->output_vio = NULL;
  data->output_vc = NULL;
  data->pending_action = NULL;
  data->server_vc = NULL;
  data->server_vio = NULL;
  data->content_length = 0;

  if (INKContDataSet(contp, data) != INK_SUCCESS) {
    INKError("Error in setting continuation's data. INKContDataSet doesn't return INK_SUCCESS");
  }

  return contp;
}

static void
transform_destroy(INKCont contp)
{
  TransformData *data;

  data = INKContDataGet(contp);
  if ((data != INK_ERROR_PTR) || (data != NULL)) {
    if (data->input_buf) {
      if (INKIOBufferDestroy(data->input_buf) != INK_SUCCESS) {
        INKError("Unable to destroy input IO buffer");
      }
    }

    if (data->output_buf) {
      if (INKIOBufferDestroy(data->output_buf) != INK_SUCCESS) {
        INKError("Unable to destroy output IO buffer");
      }
    }

    if (data->pending_action) {
      if (INKActionCancel(data->pending_action) != INK_SUCCESS) {
        INKError("Unable to cancel the pending action");
      }
    }

    if (data->server_vc) {
      if (INKVConnAbort(data->server_vc, 1) != INK_SUCCESS) {
        INKError("Unable to abort server VConnection. INKVConnAbort doesn't return INK_SUCESS");
      }
    }

    INKfree(data);
  } else {
    INKError("Unable to get Continuation's Data. INKContDataGet returns INK_ERROR_PTR or NULL");
  }

  if (INKContDestroy(contp) != INK_SUCCESS) {
    INKError("Error in Destroying the continuation");
  }
}

static int
transform_connect(INKCont contp, TransformData * data)
{
  INKAction action;
  int content_length;

  data->state = STATE_CONNECT;

  content_length = INKIOBufferReaderAvail(data->input_reader);
  if (content_length != INK_ERROR) {
    data->content_length = content_length;
    data->content_length = htonl(data->content_length);

    /* Prepend the content length to the buffer.
     * If we decide to not send the content to the transforming
     * server then we need to make sure and skip input_reader
     * over the content length.
     */

    {
      INKIOBuffer temp;
      INKIOBufferReader tempReader;

      temp = INKIOBufferCreate();
      if (temp != INK_ERROR_PTR) {
        tempReader = INKIOBufferReaderAlloc(temp);

        if (tempReader != INK_ERROR_PTR) {

          if (INKIOBufferWrite(temp, (const char *) &data->content_length, sizeof(int)) == INK_ERROR) {
            INKError("INKIOBufferWrite returns INK_ERROR");
            if (INKIOBufferReaderFree(tempReader) == INK_ERROR) {
              INKError("INKIOBufferReaderFree returns INK_ERROR");
            }
            if (INKIOBufferDestroy(temp) == INK_ERROR) {
              INKError("INKIOBufferDestroy returns INK_ERROR");
            }
            return 0;
          }

          if (INKIOBufferCopy(temp, data->input_reader, data->content_length, 0) == INK_ERROR) {
            INKError("INKIOBufferCopy returns INK_ERROR");
            if (INKIOBufferReaderFree(tempReader) == INK_ERROR) {
              INKError("INKIOBufferReaderFree returns INK_ERROR");
            }
            if (INKIOBufferDestroy(temp) == INK_ERROR) {
              INKError("INKIOBufferDestroy returns INK_ERROR");
            }
            return 0;
          }

          if (INKIOBufferReaderFree(data->input_reader) == INK_ERROR) {
            INKError("Unable to free IOBuffer Reader");
          }

          if (INKIOBufferDestroy(data->input_buf) == INK_ERROR) {
            INKError("Trying to destroy IOBuffer returns INK_ERROR");
          }

          data->input_buf = temp;
          data->input_reader = tempReader;

        } else {
          INKError("Unable to allocate a reader for buffer");
          if (INKIOBufferDestroy(temp) == INK_ERROR) {
            INKError("Unable to destroy IOBuffer");
          }
          return 0;
        }
      } else {
        INKError("Unable to create IOBuffer.");
        return 0;
      }
    }
  } else {
    INKError("INKIOBufferReaderAvail returns INK_ERROR");
    return 0;
  }

  action = INKNetConnect(contp, server_ip, server_port);
  if (action != INK_ERROR_PTR) {
    if (!INKActionDone(action)) {
      data->pending_action = action;
    }
  } else {
    INKError("Unable to connect to server. INKNetConnect returns INK_ERROR_PTR");
  }

  return 0;
}

static int
transform_write(INKCont contp, TransformData * data)
{
  int content_length;

  data->state = STATE_WRITE;

  content_length = INKIOBufferReaderAvail(data->input_reader);
  if (content_length != INK_ERROR) {

    data->server_vio =
      INKVConnWrite(data->server_vc, contp, INKIOBufferReaderClone(data->input_reader), content_length);
    if (data->server_vio == INK_ERROR_PTR) {
      INKError("INKVConnWrite returns INK_ERROR_PTR");
    }
  } else {
    INKError("INKIOBufferReaderAvail returns INK_ERROR");
  }
  return 0;
}

static int
transform_read_status(INKCont contp, TransformData * data)
{
  data->state = STATE_READ_STATUS;

  data->output_buf = INKIOBufferCreate();
  if ((data->output_buf != NULL) && (data->output_buf != INK_ERROR_PTR)) {
    data->output_reader = INKIOBufferReaderAlloc(data->output_buf);
    if ((data->output_reader != NULL) && (data->output_reader != INK_ERROR_PTR)) {
      data->server_vio = INKVConnRead(data->server_vc, contp, data->output_buf, sizeof(int));
      if (data->server_vio == INK_ERROR_PTR) {
        INKError("INKVConnRead returns INK_ERROR_PTR");
      }

    } else {
      INKError("Error in Allocating a Reader to output buffer. INKIOBufferReaderAlloc returns NULL or INK_ERROR_PTR");
    }
  } else {
    INKError("Error in creating output buffer. INKIOBufferCreate returns INK_ERROR_PTR");
  }
  return 0;
}

static int
transform_read(INKCont contp, TransformData * data)
{
  data->state = STATE_READ;

  if (INKIOBufferDestroy(data->input_buf) != INK_SUCCESS) {
    INKError("Unable to destroy input IO Buffer. INKIOBuffer doesn't return INK_SUCCESS");
  }
  data->input_buf = NULL;
  data->input_reader = NULL;

  data->server_vio = INKVConnRead(data->server_vc, contp, data->output_buf, data->content_length);

  if (data->server_vio == INK_ERROR_PTR) {
    INKError("INKVConnRead returns INK_ERROR_PTR");
    return -1;
  }

  data->output_vc = INKTransformOutputVConnGet((INKVConn) contp);
  if ((data->output_vc == INK_ERROR_PTR) || (data->output_vc == NULL)) {
    INKError("INKTransformOutputVConnGet returns NULL or INK_ERROR_PTR");
  } else {
    data->output_vio = INKVConnWrite(data->output_vc, contp, data->output_reader, data->content_length);
    if ((data->output_vio == INK_ERROR_PTR) || (data->output_vio == NULL)) {
      INKError("INKVConnWrite returns NULL or INK_ERROR_PTR");
    }
  }

  return 0;
}

static int
transform_bypass(INKCont contp, TransformData * data)
{
  data->state = STATE_BYPASS;

  if (data->server_vc) {
    if (INKVConnAbort(data->server_vc, 1) != INK_SUCCESS) {
      INKError("Error in destroy server vc. INKVConnAbort doesn't return INK_SUCCESS");
    }
    data->server_vc = NULL;
    data->server_vio = NULL;
  }

  if (data->output_buf) {
    if (INKIOBufferDestroy(data->output_buf) != INK_SUCCESS) {
      INKError("Error in destroy output IO buffer. INKIOBufferDestroy doesn't return INK_SUCCESS");
    }
    data->output_buf = NULL;
    data->output_reader = NULL;
  }

  if (INKIOBufferReaderConsume(data->input_reader, sizeof(int)) != INK_SUCCESS) {
    INKError("Error in Consuming bytes from Reader. INKIObufferReaderConsume doesn't return INK_SUCCESS");
  }

  data->output_vc = INKTransformOutputVConnGet((INKVConn) contp);
  if ((data->output_vc == INK_ERROR_PTR) || (data->output_vc == NULL)) {
    INKError("INKTransformOutputVConnGet returns NULL or INK_ERROR_PTR");
  } else {
    data->output_vio =
      INKVConnWrite(data->output_vc, contp, data->input_reader, INKIOBufferReaderAvail(data->input_reader));
    if ((data->output_vio == INK_ERROR_PTR) || (data->output_vio == NULL)) {
      INKError("INKVConnWrite returns NULL or INK_ERROR_PTR");
    }
  }
  return 1;
}

static int
transform_buffer_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  INKVIO write_vio;
  int towrite;
  int avail;

  if (!data->input_buf) {
    data->input_buf = INKIOBufferCreate();
    if ((data->input_buf == NULL) || (data->input_buf == INK_ERROR_PTR)) {
      INKError("Error in Creating buffer");
      return -1;
    }
    data->input_reader = INKIOBufferReaderAlloc(data->input_buf);
    if ((data->input_reader == NULL) || (data->input_reader == INK_ERROR_PTR)) {
      INKError("Unable to allocate a reader to input buffer.");
      return -1;
    }
  }

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = INKVConnWriteVIOGet(contp);
  if (write_vio == INK_ERROR_PTR) {
    INKError("Corrupted write VIO received.");
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */
  if (!INKVIOBufferGet(write_vio)) {
    return transform_connect(contp, data);
  }

  /* Determine how much data we have left to read. For this server
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = INKVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = INKIOBufferReaderAvail(INKVIOReaderGet(write_vio));
    if (avail == INK_ERROR) {
      INKError("Unable to get the number of bytes availabe for reading");
    } else {
      if (towrite > avail) {
        towrite = avail;
      }

      if (towrite > 0) {
        /* Copy the data from the read buffer to the input buffer. */
        if (INKIOBufferCopy(data->input_buf, INKVIOReaderGet(write_vio), towrite, 0) == INK_ERROR) {
          INKError("Error in Copying the buffer");
        } else {

          /* Tell the read buffer that we have read the data and are no
             longer interested in it. */
          if (INKIOBufferReaderConsume(INKVIOReaderGet(write_vio), towrite) != INK_SUCCESS) {
            INKError("Unable to consume bytes from the buffer");
          }

          /* Modify the write VIO to reflect how much data we've
             completed. */
          if (INKVIONDoneSet(write_vio, INKVIONDoneGet(write_vio) + towrite) != INK_SUCCESS) {
            INKError("Unable to modify the write VIO to reflect how much data we have completed");
          }
        }
      }
    }
  } else {
    if (towrite == INK_ERROR) {
      INKError("INKVIONTodoGet returns INK_ERROR");
      return 0;
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (INKVIONTodoGet(write_vio) > 0) {
    /* Call back the write VIO continuation to let it know that we
       are ready for more data. */
    INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_READY, write_vio);
  } else {
    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_COMPLETE, write_vio);

    /* start compression... */
    return transform_connect(contp, data);
  }

  return 0;
}

static int
transform_connect_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_NET_CONNECT:
    data->pending_action = NULL;
    data->server_vc = (INKVConn) edata;
    return transform_write(contp, data);
  case INK_EVENT_NET_CONNECT_FAILED:
    data->pending_action = NULL;
    return transform_bypass(contp, data);
  default:
    break;
  }

  return 0;
}

static int
transform_write_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    if (INKVIOReenable(data->server_vio) != INK_SUCCESS) {
      INKError("Unable to reenable the server vio in INK_EVENT_VCONN_WRITE_READY");
    }
    break;
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    return transform_read_status(contp, data);
  default:
    /* An error occurred while writing to the server. Close down
       the connection to the server and bypass. */
    return transform_bypass(contp, data);
  }

  return 0;
}

static int
transform_read_status_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_ERROR:
  case INK_EVENT_VCONN_EOS:
    return transform_bypass(contp, data);
  case INK_EVENT_VCONN_READ_COMPLETE:
    if (INKIOBufferReaderAvail(data->output_reader) == sizeof(int)) {
      INKIOBufferBlock blk;
      char *buf;
      void *buf_ptr;
      int avail;
      int read_nbytes = sizeof(int);
      int read_ndone = 0;

      buf_ptr = &data->content_length;
      while (read_nbytes > 0) {
        blk = INKIOBufferReaderStart(data->output_reader);
        if (blk == INK_ERROR_PTR) {
          INKError("Error in Getting the pointer to starting of reader block");
        } else {
          buf = (char *) INKIOBufferBlockReadStart(blk, data->output_reader, &avail);
          if (buf != INK_ERROR_PTR) {
            read_ndone = (avail >= read_nbytes) ? read_nbytes : avail;
            memcpy(buf_ptr, buf, read_ndone);
            if (read_ndone > 0) {
              if (INKIOBufferReaderConsume(data->output_reader, read_ndone) != INK_SUCCESS) {
                INKError("Error in consuming data from the buffer");
              } else {
                read_nbytes -= read_ndone;
                /* move ptr frwd by read_ndone bytes */
                buf_ptr = (char *) buf_ptr + read_ndone;
              }
            }
          } else {
            INKError("INKIOBufferBlockReadStart returns INK_ERROR_PTR");
          }
        }
      }
      data->content_length = ntohl(data->content_length);
      return transform_read(contp, data);
    }
    return transform_bypass(contp, data);
  default:
    break;
  }

  return 0;
}

static int
transform_read_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_ERROR:
    if (INKVConnAbort(data->server_vc, 1) != INK_SUCCESS) {
      INKError("INKVConnAbort doesn't return INK_SUCCESS on server VConnection during INK_EVENT_ERROR");
    }
    data->server_vc = NULL;
    data->server_vio = NULL;

    if (INKVConnAbort(data->output_vc, 1) != INK_SUCCESS) {
      INKError("INKVConnAbort doesn't return INK_SUCCESS on output VConnection during INK_EVENT_ERROR");
    }
    data->output_vc = NULL;
    data->output_vio = NULL;
    break;
  case INK_EVENT_VCONN_EOS:
    if (INKVConnAbort(data->server_vc, 1) != INK_SUCCESS) {
      INKError("INKVConnAbort doesn't return INK_SUCCESS on server VConnection during INK_EVENT_VCONN_EOS");
    }
    data->server_vc = NULL;
    data->server_vio = NULL;

    if (INKVConnAbort(data->output_vc, 1) != INK_SUCCESS) {
      INKError("INKVConnAbort doesn't return INK_SUCCESS on output VConnection during INK_EVENT_VCONN_EOS");
    }
    data->output_vc = NULL;
    data->output_vio = NULL;
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
    if (INKVConnClose(data->server_vc) != INK_SUCCESS) {
      INKError("INKVConnClose doesn't return INK_SUCCESS on INK_EVENT_VCONN_READ_COMPLETE");
    }
    data->server_vc = NULL;
    data->server_vio = NULL;

    if (INKVIOReenable(data->output_vio) != INK_SUCCESS) {
      INKError("INKVIOReneable doesn't return INK_SUCCESS on INK_EVENT_VCONN_READ_COMPLETE");
    }
    break;
  case INK_EVENT_VCONN_READ_READY:
    if (INKVIOReenable(data->output_vio) != INK_SUCCESS) {
      INKError("INKVIOReneable doesn't return INK_SUCCESS on INK_EVENT_VCONN_READ_READY");
    }
    break;
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    if (INKVConnShutdown(data->output_vc, 0, 1) != INK_SUCCESS) {
      INKError("INKVConnShutdown doesn't return INK_SUCCESS during INK_EVENT_VCONN_WRITE_COMPLETE");
    }
    break;
  case INK_EVENT_VCONN_WRITE_READY:
    if (INKVIOReenable(data->server_vio) != INK_SUCCESS) {
      INKError("INKVIOReneable doesn't return INK_SUCCESS while reenabling on INK_EVENT_VCONN_WRITE_READY");
    }
    break;
  default:
    break;
  }

  return 0;
}

static int
transform_bypass_event(INKCont contp, TransformData * data, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    if (INKVConnShutdown(data->output_vc, 0, 1) != INK_SUCCESS) {
      INKError("Error in shutting down the VConnection while bypassing the event");
    }
    break;
  case INK_EVENT_VCONN_WRITE_READY:
  default:
    if (INKVIOReenable(data->output_vio) != INK_SUCCESS) {
      INKError("Error in re-enabling the VIO while bypassing the event");
    }
    break;
  }

  return 0;
}

static int
transform_handler(INKCont contp, INKEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to
     INKVConnClose. */
  if (INKVConnClosedGet(contp)) {
    transform_destroy(contp);
    return 0;
  } else {
    TransformData *data;
    int val = 0;

    data = (TransformData *) INKContDataGet(contp);
    if ((data == NULL) && (data == INK_ERROR_PTR)) {
      INKError("Didn't get Continuation's Data. Ignoring Event..");
      return 0;
    }
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
request_ok(INKHttpTxn txnp)
{
  /* Is the initial client request OK for transformation. This is a
     good place to check accept headers to see if the client can
     accept a transformed document. */
  return 1;
}

static int
cache_response_ok(INKHttpTxn txnp)
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
server_response_ok(INKHttpTxn txnp)
{
  /* Is the response the server sent OK for transformation. This is
   * a good place to check the server's response to see if it is
   * transformable. In this example, we will transform only "200 OK"
   * responses.  
   */

  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKHttpStatus resp_status;

  if (INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) == 0) {
    INKError("Unable to get handle to Server Response");
    return 0;
  }

  if ((resp_status = INKHttpHdrStatusGet(bufp, hdr_loc)) == INK_ERROR) {
    INKError("Error in Getting Status from Server response");
    if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) != INK_SUCCESS) {
      INKError("Unable to release handle to server request");
    }
    return 0;
  }

  if (INK_HTTP_STATUS_OK == resp_status) {
    if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) != INK_SUCCESS) {
      INKError("Unable to release handle to server request");
    }
    return 1;
  } else {
    if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) != INK_SUCCESS) {
      INKError("Unable to release handle to server request");
    }
    return 0;
  }
}

static int
transform_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    if (request_ok(txnp)) {
      if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_CACHE_HDR_HOOK, contp) != INK_SUCCESS) {
        INKError("Unable to add continuation to hook " "INK_HTTP_READ_CACHE_HDR_HOOK for this transaction");
      }
      if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_RESPONSE_HDR_HOOK, contp) != INK_SUCCESS) {
        INKError("Unable to add continuation to hook " "INK_HTTP_READ_RESPONSE_HDR_HOOK for this transaction");
      }
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      INKError("Error in re-enabling transaction at INK_HTTP_READ_REQUEST_HDR_HOOK");
    }
    break;
  case INK_EVENT_HTTP_READ_CACHE_HDR:
    if (cache_response_ok(txnp)) {
      if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, transform_create(txnp)) != INK_SUCCESS) {
        INKError("Unable to add continuation to tranformation hook " "for this transaction");
      }
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      INKError("Error in re-enabling transaction at INK_HTTP_READ_CACHE_HDR_HOOK");
    }
    break;
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (server_response_ok(txnp)) {
      if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, transform_create(txnp)) != INK_SUCCESS) {
        INKError("Unable to add continuation to tranformation hook " "for this transaction");
      }
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) != INK_SUCCESS) {
      INKError("Error in re-enabling transaction at INK_HTTP_READ_RESPONSE_HDR_HOOK");
    }
    break;
  default:
    break;
  }
  return 0;
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;
  INKCont cont;

  info.plugin_name = "server-transform";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  /* connect to the echo port on localhost */
  server_ip = (127 << 24) | (0 << 16) | (0 << 8) | (1);
  server_ip = htonl(server_ip);
  server_port = 7;

  if ((cont = INKContCreate(transform_plugin, NULL)) == INK_ERROR_PTR) {
    INKError("Unable to create continuation. Aborting...");
    return;
  }

  if (INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, cont) == INK_ERROR) {
    INKError("Unable to add the continuation to the hook. Aborting...");
    if (INKContDestroy(cont) == INK_ERROR) {
      INKError("Error in Destroying the continuation.");
    }
  }
}
