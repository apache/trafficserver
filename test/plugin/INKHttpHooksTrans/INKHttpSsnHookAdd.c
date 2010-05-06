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
Tests for registering/processing events at the session level.
      INKHttpSsnHookAdd( HOOK_ID is either SSN_START or SSN_CLOSE ) 
*/


#include "ts.h"
#include <sys/types.h>
#include <stdio.h>


const char *const
  INKEventStrId[] = {
  "INK_EVENT_HTTP_CONTINUE",    /* 60000 */
  "INK_EVENT_HTTP_ERROR",       /* 60001 */
  "INK_EVENT_HTTP_READ_REQUEST_HDR",    /* 60002 */
  "INK_EVENT_HTTP_OS_DNS",      /* 60003 */
  "INK_EVENT_HTTP_SEND_REQUEST_HDR",    /* 60004 */
  "INK_EVENT_HTTP_READ_CACHE_HDR",      /* 60005 */
  "INK_EVENT_HTTP_READ_RESPONSE_HDR",   /* 60006 */
  "INK_EVENT_HTTP_SEND_RESPONSE_HDR",   /* 60007 */
  "INK_EVENT_HTTP_REQUEST_TRANSFORM",   /* 60008 */
  "INK_EVENT_HTTP_RESPONSE_TRANSFORM",  /* 60009 */
  "INK_EVENT_HTTP_SELECT_ALT",  /* 60010 */
  "INK_EVENT_HTTP_TXN_START",   /* 60011 */
  "INK_EVENT_HTTP_TXN_CLOSE",   /* 60012 */
  "INK_EVENT_HTTP_SSN_START",   /* 60013 */
  "INK_EVENT_HTTP_SSN_CLOSE",   /* 60014 */

  "INK_EVENT_MGMT_UPDATE"       /* 60100 */
};

/* 
 * We track that each hook was called using this array. We start with
 * all values set to zero, meaning that the INKEvent has not been 
 * received. 
 * There 16 entries.
*/
static int inktHookTbl[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define		index(x)	((x)%(1000))
static int inktHookTblSize;


/*********************** null-transform.c *********************************/
typedef struct
{
  INKVIO output_vio;
  INKIOBuffer output_buffer;
  INKIOBufferReader output_reader;
} MyData;

static MyData *
my_data_alloc()
{
  MyData *data;

  data = (MyData *) INKmalloc(sizeof(MyData));
  data->output_vio = NULL;
  data->output_buffer = NULL;
  data->output_reader = NULL;

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
  INKVIO input_vio;
  MyData *data;
  int towrite;
  int avail;

  /* Get the output (downstream) vconnection where we'll write data to. */

  output_conn = INKTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection). 
   */
  input_vio = INKVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */
  data = INKContDataGet(contp);
  if (!data) {
    data = my_data_alloc();
    data->output_buffer = INKIOBufferCreate();
    data->output_reader = INKIOBufferReaderAlloc(data->output_buffer);
    data->output_vio = INKVConnWrite(output_conn, contp, data->output_reader, INKVIONBytesGet(input_vio));
    INKContDataSet(contp, data);
  }

  /* We also check to see if the input VIO's buffer is non-NULL. A
   * NULL buffer indicates that the write operation has been
   * shutdown and that the upstream continuation does not want us to send any
   * more WRITE_READY or WRITE_COMPLETE events. For this simplistic
   * transformation that means we're done. In a more complex
   * transformation we might have to finish writing the transformed
   * data to our output connection. 
   */
  if (!INKVIOBufferGet(input_vio)) {
    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio));
    INKVIOReenable(data->output_vio);
    return;
  }

  /* Determine how much data we have left to read. For this null
   * transform plugin this is also the amount of data we have left
   * to write to the output connection. 
   */
  towrite = INKVIONTodoGet(input_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
     * the amount of data actually in the read buffer. 
     */
    avail = INKIOBufferReaderAvail(INKVIOReaderGet(input_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the output buffer. */
      INKIOBufferCopy(INKVIOBufferGet(data->output_vio), INKVIOReaderGet(input_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
       * longer interested in it. 
       */
      INKIOBufferReaderConsume(INKVIOReaderGet(input_vio), towrite);

      /* Modify the input VIO to reflect how much data we've
       * completed. 
       */
      INKVIONDoneSet(input_vio, INKVIONDoneGet(input_vio) + towrite);
    }
  }

  /* Now we check the input VIO to see if there is data left to
   * read. 
   */
  if (INKVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      /* If there is data left to read, then we reenable the output
       * connection by reenabling the output VIO. This will wake up
       * the output connection and allow it to consume data from the
       * output buffer. 
       */
      INKVIOReenable(data->output_vio);

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
    INKVIONBytesSet(data->output_vio, INKVIONDoneGet(input_vio));
    INKVIOReenable(data->output_vio);

    /* Call back the input VIO continuation to let it know that we
     * have completed the write operation. 
     */
    INKContCall(INKVIOContGet(input_vio), INK_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }
}

/* TODO implement the processing for these events
 * Recall that we are mostly interested in receiving the events
 * that have been registered. Cut/Paste for now.
*/
static int
null_transform(INKCont contp, INKEvent event, void *edata)
{
  /* This is the "event(s)" that are delivered for 
   * INK_EVENT_HTTP_RESPONSE_TRANSFORM. 
   */
  inktHookTbl[index(INK_EVENT_HTTP_RESPONSE_TRANSFORM)] = 1;
  ChkEvents(INK_EVENT_HTTP_RESPONSE_TRANSFORM);


  /* Check to see if the transformation has been closed by a call to
   * INKVConnClose. 
   */
  if (INKVConnClosedGet(contp)) {
    my_data_destroy(INKContDataGet(contp));
    INKContDestroy(contp);
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
        input_vio = INKVConnWriteVIOGet(contp);

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
      INKVConnShutdown(INKTransformOutputVConnGet(contp), 0, 1);
      break;
    case INK_EVENT_VCONN_WRITE_READY:
    default:
      /* If we get a WRITE_READY event or any other type of
       * event (sent, perhaps, because we were reenabled) then
       * we'll attempt to transform more data. 
       */
      handle_transform(contp);
      break;
    }
  }

  return 0;
}



/* Since this is event based, it can be re-used with 
 * INKHttpHookAdd()
 * INKHttpSsnHookAdd()
 * INKHttpTxnHokkAdd()
*/
static int
ChkEvents(const int event)
{
  int i, re = 0;
  /* INKDebug("INKHttpSsnHookAdd",  */
  printf("ChkEvents: -- %s -- \n", INKEventStrId[index(event)]);

  for (i = 0; i < inktHookTblSize; i++) {
    if (!inktHookTbl[i]) {
      printf("Event [%d] %s registered and not called back\n", i, INKEventStrId[i]);
      re = 1;
    }
  }
  return re;
}

/* event routine: for each INKHttpHookID this routine should be called 
 * with a matching event. 
*/
static int
SsnHookAddEvent(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;
  INKVConn vconnp;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_REQUEST_HDR)] = 1;
    /* List what events have been called back at 
     * this point in procesing 
     */
    ChkEvents(INK_EVENT_HTTP_READ_REQUEST_HDR);

    /* TODO test for INK_HTTP_REQUEST_TRANSFORM_HOOK */

    /* This event is delivered to a transaction. Reenable the
     * txnp pointer not the session. 
     */
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_OS_DNS:
    inktHookTbl[index(INK_EVENT_HTTP_OS_DNS)] = 1;
    ChkEvents(INK_EVENT_HTTP_OS_DNS);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_SEND_REQUEST_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_SEND_REQUEST_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_CACHE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_CACHE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_READ_CACHE_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_READ_RESPONSE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_READ_RESPONSE_HDR);

#error FIX Check if event is transformable using code from null-transform.c
    /* TODO fix check if event is transformable
     *  Using code from null-transform.c
     */
    vconnp = INKTransformCreate(null_transform, txnp);

    /* TODO verify
     * should be:
     *              INK_EVENT_HTTP_READ_REQUEST_HDR_HOOK
     *              INK_HTTP_REQUEST_TRANSFORM_HOOK 
     * for 
     *              INK_EVENT_HTTP_READ_RESPONSE_HDR_HOOK
     *              INK_HTTP_RESPONSE_TRANSFORM_HOOK 
     * Registering with a new continuation / callback.
     */

    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    inktHookTbl[index(INK_EVENT_HTTP_SEND_RESPONSE_HDR)] = 1;
    ChkEvents(INK_EVENT_HTTP_SEND_RESPONSE_HDR);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_REQUEST_TRANSFORM:
    inktHookTbl[index(INK_EVENT_HTTP_REQUEST_TRANSFORM)] = 1;
    ChkEvents(INK_EVENT_HTTP_REQUEST_TRANSFORM);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_RESPONSE_TRANSFORM:
    /* This event is not delivered here. See null_transform 
     * and verify that INK_EVENT_ERROR, 
     * INK_EVENT_VCONN_WRITE_COMPLETE, INK_EVENT_CVONN_WRITE_READY
     *
     * TODO do not use as a defined event if it does not get delivered
     */
    inktHookTbl[index(INK_EVENT_HTTP_RESPONSE_TRANSFORM)] = 1;
    ChkEvents(INK_EVENT_HTTP_RESPONSE_TRANSFORM);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SELECT_ALT:
    inktHookTbl[index(INK_EVENT_HTTP_SELECT_ALT)] = 1;
    ChkEvents(INK_EVENT_HTTP_SELECT_ALT);

    /* Non-blocking & synchronous event
     */
    break;

  case INK_EVENT_HTTP_TXN_START:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_START);
    INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, contp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_TXN_CLOSE:
    inktHookTbl[index(INK_EVENT_HTTP_TXN_CLOSE)] = 1;
    ChkEvents(INK_EVENT_HTTP_TXN_CLOSE);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SSN_START:

    inktHookTbl[index(INK_EVENT_HTTP_SSN_START)] = 1;
    ChkEvents(INK_EVENT_HTTP_SSN_START);        /* code re-use */

    /* For this session, register for all events */
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_REQUEST_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_OS_DNS_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SEND_REQUEST_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_CACHE_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    INKHttpSsnHookAdd(ssnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

    /* These are considered "global" hooks and must be reged at
     *  init.
     *INKHttpSsnHookAdd(ssnp,INK_HTTP_REQUEST_TRANSFORM_HOOK,contp);
     *INKHttpSsnHookAdd(ssnp,INK_HTTP_RESPONSE_TRANSFORM_HOOK,contp);
     *INKHttpSsnHookAdd(ssnp,INK_HTTP_SELECT_ALT_HOOK, contp);
     */

    INKHttpSsnHookAdd(ssnp, INK_HTTP_TXN_START_HOOK, contp);

                /******************************************************** 
		* We've already registered for this event as a global 
		* hook. Registering for this event at the session 
		* level will send this event twice: once for the registration
		* done at PluginInit and once for the sessions.
		*
		INKHttpSsnHookAdd(ssnp,INK_HTTP_SSN_START_HOOK, contp);
		*******************************************************/

    INKHttpSsnHookAdd(ssnp, INK_HTTP_SSN_CLOSE_HOOK, contp);

    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);

    break;

  case INK_EVENT_HTTP_SSN_CLOSE:
    /* Here as a result of: 
     * INKHTTPSsnHookAdd(ssnp, INK_HTTP_SSN_CLOSE_HOOK, contp) 
     */
    inktHookTbl[index(INK_EVENT_HTTP_SSN_CLOSE)] = 1;

    /* Assumption: at this point all other events have
     * have been called. Since a session can have one or
     * more transactions, the close of a session should
     * prompt us to check that all events have been called back 
     */
    if (ChkEvents(INK_EVENT_HTTP_SSN_CLOSE))
      INKError("INKHttpHook: Fail: All events not called back.\n");
    else
      INKError("INKHttpHook: Pass: All events called back.\n");

    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    INKError("INKHttpHook: undefined event [%d] received\n", event);
    break;
  }
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont myCont = NULL;
  inktHookTblSize = sizeof(inktHookTbl) / sizeof(int);

  /* Create continuation */
  myCont = INKContCreate(SsnHookAddEvent, NULL);
  if (myCont != NULL) {
    /* We need to register ourselves with a global hook
     * so that we can process a session.
     */
    INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, myCont);
    INKHttpHookAdd(INK_HTTP_SELECT_ALT_HOOK, myCont);
  } else
    INKError("INKHttpHook: INKContCreate() failed \n");
}
