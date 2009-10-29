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
 * This plugin is used to test the following APIs:
 * -- INKHttpTxnUntransformedRespCache
 * -- INKHttpTxnTransformedRespCache
 * -- INKHttpTxnTransformRespGet
 * -- INKIOBufferBlockNext
 * -- INKIOBufferBlockReadAvail
 * -- INKIOBufferBlockWriteAvail
 * -- INKVConnCreate
 * -- INKVConnReadVIOGet
 * -- INKVIOMutexGet
 * -- INKVIOVConnGet
 *
 * It is based on the null-transform plugin. The above function calls are inserted into the appropriate places.
 */

#include <stdio.h>
#include "InkAPI.h"

#define DBG_TAG "test-transform-dbg"

#define PLUGIN_NAME "test-transform"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


typedef struct
{
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
  INKHttpTxn txn;
  int init_done;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) INKmalloc(sizeof(MyData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;
  data->txn = NULL;
  data->init_done = 0;

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

/*
 * test the following VIO functions:
 * -- INKVIOMutexGet
 * -- INKVIOVConnGet
 */
static void
test_vio(INKCont contp)
{
  LOG_SET_FUNCTION_NAME("test_vio");
  INKVConn vio_vconn;
  INKVIO output_vio;
  INKVIO input_vio;
  INKMutex m1, m2;

  /* Get the read (output) VIO for the vconnection */
  if ((output_vio = INKVConnReadVIOGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR("INKVConnReadVIOGet");
  if ((input_vio = INKVConnWriteVIOGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR("INKVConnWriteVIOGet")

      /* get the mutex of the VIO */
      if ((m1 = INKVIOMutexGet(input_vio)) == INK_ERROR_PTR)
      LOG_ERROR("INKVIOMutexGet");

  /* get the vio mutex */
  if ((m2 = INKContMutexGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR("INKContMutexGet");

  /* the VIO mutex should equal to the VConn mutex */
  if (m1 != m2)
    LOG_ERROR("INKVIOMutexGet");

  /* get the VConn of the VIO */
  if ((vio_vconn = INKVIOVConnGet(input_vio)) == INK_ERROR_PTR)
    LOG_ERROR("INKVIOVConnGet");

  /* the vconn should equal to the continuation */
  if (vio_vconn != contp)
    LOG_ERROR("vio_vconn");

  /* negative test */
#ifdef DEBUG
  if (INKVConnReadVIOGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKVConnReadVIOGet");
  if (INKVConnWriteVIOGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKVConnWriteVIOGet")
      if (INKVIOMutexGet(NULL) != INK_ERROR_PTR)
      LOG_ERROR_NEG("INKVIOMutexGet");
  if (INKContMutexGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKContMutexGet");
#endif

}

/* test several IOBuffer functions */
static void
test_iobuffer()
{
  LOG_SET_FUNCTION_NAME("test_iobuffer");

  INKIOBuffer bufp = NULL;
  INKIOBufferBlock blockp = NULL;
  INKIOBufferReader readerp = NULL;
  int read_avail = 0, write_avail = 0, writestart = 0, avail = 0, towrite = 0;
  char *start;
  const char *STRING_CONSTANT = "constant string to be copied into an iobuffer";

  /* create an IOBuffer */
  if ((bufp = INKIOBufferCreate()) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");


  /* Write STRING_CONSTANT at the beginning of the iobuffer */
  if ((blockp = INKIOBufferStart(bufp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferStart");
  if ((start = INKIOBufferBlockWriteStart(blockp, &avail)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferBlockWriteStart");

  towrite = strlen(STRING_CONSTANT);
  while (start && towrite > 0) {
    if (towrite > avail) {
      memcpy(start, &STRING_CONSTANT[writestart], avail);
      writestart += avail;
      towrite -= avail;
      if ((INKIOBufferProduce(bufp, avail)) == INK_ERROR)
        LOG_ERROR_AND_CLEANUP("INKIOBufferProduce");
      if ((blockp = INKIOBufferStart(bufp)) == INK_ERROR_PTR)
        LOG_ERROR_AND_CLEANUP("INKIOBufferStart");
      if ((start = INKIOBufferBlockWriteStart(blockp, &avail)) == INK_ERROR_PTR)
        LOG_ERROR_AND_CLEANUP("INKIOBufferBlockWriteStart");
    } else {
      memcpy(start, &STRING_CONSTANT[writestart], towrite);
      writestart += towrite;
      if (INKIOBufferProduce(bufp, towrite) == INK_ERROR)
        LOG_ERROR_AND_CLEANUP("INKIOBufferProduce");
      towrite = 0;
    }
  }


  /* get the next block in the IOBuffer */
  if (INKIOBufferBlockNext(blockp) == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKIOBufferBlockNext");
  }

  /* print the read avail and write avail of the iobuffer */
  if ((readerp = INKIOBufferReaderAlloc(bufp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferReaderAlloc");
  if ((read_avail = INKIOBufferBlockReadAvail(blockp, readerp)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferBlockReadAvail");
  if ((write_avail = INKIOBufferBlockWriteAvail(blockp)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferBlockWriteAvail");

  INKDebug(DBG_TAG, "read_avail = %d", read_avail);
  INKDebug(DBG_TAG, "write_avail = %d", write_avail);

  /* negative test */
#ifdef DEBUG
  if (INKIOBufferStart(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKIOBufferStart");
  if (INKIOBufferBlockWriteStart(NULL, &avail) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKIOBufferBlockWriteStart");
  if (INKIOBufferProduce(NULL, 0) != INK_ERROR)
    LOG_ERROR_NEG("INKIOBufferProduce");
  if (INKIOBufferBlockNext(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKIOBufferBlockNext");

  if (INKIOBufferBlockReadAvail(NULL, readerp) != INK_ERROR)
    LOG_ERROR_NEG("INKIOBufferBlockReadAvail");
  if (INKIOBufferBlockReadAvail(blockp, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKIOBufferBlockReadAvail");

  if (INKIOBufferBlockWriteAvail(NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKIOBufferBlockWriteAvail");

#endif

  /* cleanup */
Lcleanup:
  INKIOBufferDestroy(bufp);
}


static int
transform_init(INKCont contp, MyData * data)
{
  LOG_SET_FUNCTION_NAME("transform_init");

  INKVConn output_conn;
  INKVIO input_vio;

  INKMBuffer bufp;
  INKMLoc hdr_loc = NULL;
  INKMLoc ce_loc = NULL;        /* for the content encoding mime field */


  if ((output_conn = INKTransformOutputVConnGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKTransformOutputVConnGet");
  if ((input_vio = INKVConnWriteVIOGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKVConnWriteVIOGet");

  if ((data->output_buffer = INKIOBufferCreate()) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKIOBufferCreate");
  if ((data->output_reader = INKIOBufferReaderAlloc(data->output_buffer)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKIOBufferReaderAlloc");
  if ((data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader,
                                        INKVIONBytesGet(input_vio))) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKVConnWrite");

  /* 
   * Mark the output data as having null content encoding 
   */
  if (INKHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc) != 1) {
    LOG_ERROR_AND_CLEANUP("INKHttpTxnTransformRespGet");
  }

  INKDebug(DBG_TAG, "Adding Content-Encoding mime field");
  if ((ce_loc = INKMimeHdrFieldCreate(bufp, hdr_loc)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldCreate");
  if (INKMimeHdrFieldNameSet(bufp, hdr_loc, ce_loc, "Content-Encoding", -1) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldNameSet");
  if (INKMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, "null", -1) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueStringInsert");
  if (INKMimeHdrFieldAppend(bufp, hdr_loc, ce_loc) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldAppend");

  /* negative test */
#ifdef DEBUG
  if (INKTransformOutputVConnGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKTransformOutputVConnGet");
#endif

Lcleanup:
  if (VALID_POINTER(ce_loc))
    INKHandleMLocRelease(bufp, hdr_loc, ce_loc);
  if (VALID_POINTER(hdr_loc))
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  data->init_done = 1;
  return 1;
}

static int
handle_transform(INKCont contp)
{
  LOG_SET_FUNCTION_NAME("handle_transform");

  INKVConn output_conn;
  INKVIO input_vio;
  INKIOBuffer input_buffer;
  MyData *data;
  int towrite, avail, ntodo;

  /* Get the output (downstream) vconnection where we'll write data to. */

  if ((output_conn = INKTransformOutputVConnGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKTransformOutputVConnGet");

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection). 
   */
  if ((input_vio = INKVConnWriteVIOGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKVConnWriteVIOGet");

  /* test VIO */
  test_vio(contp);

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */
  if ((data = INKContDataGet(contp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKContDataGet");

  if (data->init_done == 0) {
    transform_init(contp, data);
  }

  /* We also check to see if the input VIO's buffer is non-NULL. A
   * NULL buffer indicates that the write operation has been
   * shutdown and that the upstream continuation does not want us to send any
   * more WRITE_READY or WRITE_COMPLETE events. For this simplistic
   * transformation that means we're done. In a more complex
   * transformation we might have to finish writing the transformed
   * data to our output connection. 
   */
  if ((input_buffer = INKVIOBufferGet(input_vio)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKVIOBufferGet");

  /* negative test */
#ifdef DEBUG
  if (INKVIOBufferGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKVIOBufferGet");
  if (INKVIOReaderGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKVIOReaderGet");
  if (INKVIONTodoGet(NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONTodoGet");
  if (INKVIONDoneGet(NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONDoneGet");

  if (INKVIONBytesSet(NULL, 1) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONBytesSet");
  if (INKVIONBytesSet(data->output_vio, -1) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONBytesSet");

  if (INKVIONDoneSet(NULL, 1) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONDoneSet");
  if (INKVIONDoneSet(input_vio, -1) != INK_ERROR)
    LOG_ERROR_NEG("INKVIONDoneSet");
#endif

  if (!input_buffer) {
    if (INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio)) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKVIONBytesSet");
    if (INKVIOReenable(data->output_vio) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKVIOReenable");
    return;
  }

  /* Determine how much data we have left to read. For this null
   * transform plugin this is also the amount of data we have left
   * to write to the output connection. 
   */
  if ((towrite = INKVIONTodoGet(input_vio)) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKVIONTodoGet");
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer. 
     */
    if ((avail = INKIOBufferReaderAvail(INKVIOReaderGet(input_vio))) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKIOBufferReaderAvail");

    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      if (INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), towrite, 0) == INK_ERROR)
        LOG_ERROR_AND_RETURN("INKIOBufferCopy");

      /* negative test for INKIOBufferCopy */
#ifdef DEBUG
      if (INKIOBufferCopy(NULL, INKVIOReaderGet(input_vio), towrite, 0) != INK_ERROR)
        LOG_ERROR_NEG("INKIOBufferCopy");
      if (INKIOBufferCopy(INKVIOBufferGet(data->output_vio), NULL, towrite, 0) != INK_ERROR)
        LOG_ERROR_NEG("INKIOBufferCopy");
      if (INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), -1, 0) != INK_ERROR)
        LOG_ERROR_NEG("INKIOBufferCopy");
      if (INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), towrite, -1) != INK_ERROR)
        LOG_ERROR_NEG("INKIOBufferCopy");
#endif

      /* Tell the read buffer that we have read the data and are no
       * longer interested in it. 
       */
      if (INKIOBufferReaderConsume(INKVIOReaderGet(input_vio), towrite) == INK_ERROR)
        LOG_ERROR_AND_RETURN("INKIOBufferReaderConsume");

      /* Modify the input VIO to reflect how much data we've
       * completed. 
       */
      if (INKVIONDoneSet(input_vio, INKVIONDoneGet(input_vio) + towrite) == INK_ERROR)
        LOG_ERROR_AND_RETURN("INKVIONDoneSet");
    }
  }

  /* Now we check the input VIO to see if there is data left to
   * read. 
   */
  if ((ntodo = INKVIONTodoGet(input_vio)) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKVIONTodoGet");

  if (ntodo > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
       * connection by reenabling the output VIO. This will wake up
       * the output connection and allow it to consume data from the
       * output buffer. 
       */
      if (INKVIOReenable(data->output_vio) == INK_ERROR)
        LOG_ERROR_AND_RETURN("INKVIOReenable");

      /* Call back the input VIO continuation to let it know that we
       * are ready for more data. 
       */
      INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_READY, input_vio);
    }
  } else {
    /* If there is no data left to read, then we modify the output
     * VIO to reflect how much data the output connection should
     * expect. This allows the output connection to know when it
     * is done reading. We then reenable the output connection so
     * that it can consume the data we just gave it. 
     */
    if (INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio)) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKVIONBytesSet");
    if (INKVIOReenable(data->output_vio) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKVIOReenable");

    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation. 
     */
    INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }

}

static int
null_transform(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("null_transform");

  /* Check to see if the transformation has been closed by a call to
   * INKVConnClose. 
   */
  if (INKVConnClosedGet(contp)) {
    my_data_destroy(INKContDataGet(contp));
    if (INKContDestroy(contp) == INK_ERROR)
      LOG_ERROR("INKContDestroy");
    return 0;
  } else {
    switch (event) {
    case INK_EVENT_ERROR:
      {
        INKVIO input_vio;

        /* Get the write VIO for the write operation that was
         * performed on ourself. This VIO contains the continuation of
         * our parent transformation. This is the input VIO.  
         */
        if ((input_vio = INKVConnWriteVIOGet(contp)) == INK_ERROR_PTR)
          LOG_ERROR_AND_RETURN("INKVConnWriteVIOGet");

        /* Call back the write VIO continuation to let it know that we
         * have completed the write operation. 
         */
        INKContCall(INKVIOContGet(input_vio), INK_EVENT_ERROR, input_vio);
      }
      break;
    case INK_EVENT_VCONN_WRITE_COMPLETE:
      /* When our output connection says that it has finished
       * reading all the data we've written to it then we should
       * shutdown the write portion of its connection to
       * indicate that we don't want to hear about it anymore.
       */
      if (INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1) == INK_ERROR)
        LOG_ERROR_AND_RETURN("INKVConnShutdown");
      break;
    case INK_EVENT_VCONN_WRITE_READY:
      handle_transform(contp);
      break;

    case INK_EVENT_IMMEDIATE:
      handle_transform(contp);
      break;

    default:
      handle_transform(contp);
      break;
    }
  }

  /* negative test */
#ifdef DEBUG
  if (INKVConnClosedGet(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKVConnClosedGet");
  }
  if (INKVIOContGet(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKVIOContGet");
  }
#endif

  return 0;
}

static int
transformable(INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("transformable");

  /*
   *  We are only interested in transforming "200 OK" responses.
   */
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKHttpStatus resp_status;

  if (!INKHttpTxnServerRespGet(txnp, &bufp, &hdr_loc))
    LOG_ERROR_AND_RETURN("INKHttpTxnServerRespGet");

  if ((resp_status = INKHttpHdrStatusGet(bufp, hdr_loc)) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpHdrStatusGet");

  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  return ((resp_status == INK_HTTP_STATUS_OK) ? 1 : 0);
}

static int
transform_add(INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("transform_add");
  MyData *data;
  INKVConn connp;

  if ((connp = INKTransformCreate(null_transform, txnp)) == INK_ERROR_PTR)
    LOG_ERROR_AND_RETURN("INKTransformCreate");

  if (INKHttpTxnHookAdd(txnp, INK_HTTP_RESPONSE_TRANSFORM_HOOK, connp) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpTxnHookAdd");

  data = my_data_alloc();
  data->txn = txnp;
  if (INKContDataSet(connp, data) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKContDataSet");

  /* Cache the transformed content */
  if (INKHttpTxnUntransformedRespCache(txnp, 0) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpTxnUntransformedRespCache");
  if (INKHttpTxnTransformedRespCache(txnp, 1) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpTxnTransformedRespCache");

  /* negative test for INKHttpTxnTransformedRespCache */
#ifdef DEBUG
  if (INKHttpTxnUntransformedRespCache(NULL, 0) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpTxnUntransformedRespCache");

  if (INKHttpTxnTransformedRespCache(NULL, 1) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpTxnTransformedRespCache");

  if (INKTransformCreate(null_transform, NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKTransformCreate");
#endif
}

static int
transform_plugin(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("transform_plugin");
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    if (transformable(txnp)) {
      transform_add(txnp);
    }
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR)
      LOG_ERROR_AND_RETURN("INKHttpTxnReenable");

    return 0;
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
       least Traffic Server 3.5.2 to run */
    if (major_ts_version > 3) {
      result = 1;
    } else if (major_ts_version == 3) {
      if (minor_ts_version > 5) {
        result = 1;
      } else if (minor_ts_version == 5) {
        if (patch_ts_version >= 2) {
          result = 1;
        }
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPlugiInit");
  INKPluginRegistrationInfo info;

  info.plugin_name = "null-transform";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }

  test_iobuffer();

  if (INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, INKContCreate(transform_plugin, NULL)) == INK_ERROR)
    LOG_ERROR("INKHttpHookAdd");
}
