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

/*
 * append-transform.c:  an example program that appends the text
 *                      contained in a file to all HTTP/text response
 *                      bodies.  
 *                    
 *
 *
 *	Usage:	
 * 	(NT): AppendTransform.dll <filename>
 * 	(Solaris): append-transform.so <filename>
 * 
 *              <filename> is the name of the file containing the 
 *              text to be appended
 *
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "InkAPI.h"

typedef struct
{
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
  int append_needed;
} MyData;

static INKIOBuffer append_buffer;
static INKIOBufferReader append_buffer_reader;
static int append_buffer_length;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) INKmalloc(sizeof(MyData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->append_needed = 1;

  return data;
}

static void
my_data_destroy(MyData * data)
{
  if (data) {
    if (data->output_buffer) {
      INKIOBufferDestroy(data->output_buffer);
    }
    INKfree(data);
  }
}

static void
handle_transform(INKCont contp)
{
  INKVConn output_conn;
  INKVIO write_vio;
  MyData *data;
  int towrite;
  int avail;

  /* Get the output connection where we'll write data to. */
  output_conn = INKTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = INKVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
     structure contains the output VIO and output buffer. If the
     private data structure pointer is NULL, then we'll create it
     and initialize its internals. */
  data = INKContDataGet(contp);
  if (!data) {
    towrite = INKVIONBytesGet(write_vio);
    if (towrite != INT_MAX) {
      towrite += append_buffer_length;
    }
    data = my_data_alloc();
    data->output_buffer = INKIOBufferCreate();
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);
    data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader, towrite);
    INKContDataSet(contp, data);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this simplistic
     transformation that means we're done. In a more complex
     transformation we might have to finish writing the transformed
     data to our output connection. */
  if (!INKVIOBufferGet(write_vio)) {
    if (data->append_needed) {
      data->append_needed = 0;
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(write_vio) + append_buffer_length);
    INKVIOReenable(data->output_vio);
    return;
  }

  /* Determine how much data we have left to read. For this append
     transform plugin this is also the amount of data we have left
     to write to the output connection. */
  towrite = INKVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    avail = INKIOBufferReaderAvail(INKVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      INKIOBufferReaderConsume(INKVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      INKVIONDoneSet(write_vio, INKVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (INKVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
         connection by reenabling the output VIO. This will wakeup
         the output connection and allow it to consume data from the
         output buffer. */
      INKVIOReenable(data->output_vio);

      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    if (data->append_needed) {
      data->append_needed = 0;
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), append_buffer_reader, append_buffer_length, 0);
    }

    /* If there is no data left to read, then we modify the output
       VIO to reflect how much data the output connection should
       expect. This allows the output connection to know when it
       is done reading. We then reenable the output connection so
       that it can consume the data we just gave it. */
    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(write_vio) + append_buffer_length);
    INKVIOReenable(data->output_vio);

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    INKContCall(INKVIOContGet(write_vio), INK_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }
}

static int
append_transform(INKCont contp, INKEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to
     INKVConnClose. */
  if (INKVConnClosedGet(contp)) {
    my_data_destroy(INKContDataGet(contp));
    INKContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case INK_EVENT_ERROR:
      {
        INKVIO write_vio;

        /* Get the write VIO for the write operation that was
           performed on ourself. This VIO contains the continuation of
           our parent transformation. */
        write_vio = INKVConnWriteVIOGet(contp);

        /* Call back the write VIO continuation to let it know that we
           have completed the write operation. */
        INKContCall(INKVIOContGet(write_vio), INK_EVENT_ERROR, write_vio);
      }
      break;
    case INK_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
         reading all the data we've written to it then we should
         shutdown the write portion of its connection to
         indicate that we don't want to hear about it anymore. */
      INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1);
      break;
    case INK_EVENT_VCONN_WRITE_READY:
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

static int
transformable(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc field_loc;
  INKHttpStatus resp_status;
  const char *value;

  INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc);

  /*
   *  We are only interested in "200 OK" responses.
   */

  if (INK_HTTP_STATUS_OK == (resp_status = INKHttpHdrStatusGet(bufp, hdr_loc))) {

    /* We only want to do the transformation on documents that have a
       content type of "text/html". */
    field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, "Content-Type", -1);
    if (!field_loc) {
      return 0;
    }

    value = INKMimeFieldValueGet(bufp, field_loc, 0, NULL);
#ifndef _WIN32
    if (value && (strncasecmp(value, "text/html", sizeof("text/html") - 1) == 0)) {
#else
    if (value && (strnicmp(value, "text/html", sizeof("text/html") - 1) == 0)) {
#endif
      return 1;
    } else {
      return 0;
    }

  }

  return 0;                     /* not a 200 */
}

static void
transform_add(INKHttpTxn txnp)
{
  INKVConn connp;

  connp = INKTransformCreate(append_transform, txnp);

  INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

static int
transform_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      transform_add(txnp);
    }
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    return 0;
  default:
    break;
  }

  return 0;
}

static int
load(const char *filename)
{
  INKFile fp;
  INKIOBufferBlock blk;
  char *p;
  int avail;
  int err;

  fp = INKfopen(filename, "r");
  if (!fp) {
    return 0;
  }

  append_buffer = INKIOBufferCreate();
  append_buffer_reader = INKIOBufferReaderAlloc(append_buffer);

  for (;;) {
    blk = INKIOBufferStart(append_buffer);
    p = INKIOBufferBlockWriteStart(blk, &avail);

    err = INKfread(fp, p, avail);
    if (err > 0) {
      INKIOBufferProduce(append_buffer, err);
    } else {
      break;
    }
  }

  append_buffer_length = INKIOBufferReaderAvail(append_buffer_reader);

  INKfclose(fp);
  return 1;
}

void
INKPluginInit(int argc, const char *argv[])
{

  if (argc != 2) {
    INKError("usage: %s <filename>\n", argv[0]);
    return;
  }

  if (!load(argv[1])) {
    return;
  }

  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(transform_plugin, NULL));
}
