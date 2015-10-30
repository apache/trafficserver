.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../../common.defs

.. _developer-plugins-http-transformations-buffered-null:

Sample Buffered Null Transform Plugin
*************************************

The buffered null transform, ``bnull-transform.c``, reads the response
content into a buffer and then writes the full buffer out to the client.
Many examples of transformations, such as compression, require you to
gather the full response content in order to perform the transformation.

The buffered null transform uses a state variable to keep track of when
it is (a) reading data into the buffer and (b) writing the data from the
buffer to the downstream vconnection.

The following is a step-by-step walk through the buffered null
transform:

#.  Gets a handle to HTTP transactions.

    .. code-block:: c

       void
          TSPluginInit (int argc, const char *argv[]) {
             TSHttpHookAdd (TS_HTTP_READ_RESPONSE_HDR_HOOK,
                TSContCreate (transform_plugin, NULL)); }

    With this ``TSPluginInit`` routine, the plugin is called back every
    time Traffic Server reads a response header.

#.  Checks to see if the transaction response is transformable.

    .. code-block:: c

       static int transform_plugin (TSCont contp, TSEvent event, void *edata) {
          TSHttpTxn txnp = (TSHttpTxn) edata;
          switch (event) {
             case TS_EVENT_HTTP_READ_RESPONSE_HDR:
                if (transformable (txnp)) {
                   transform_add (txnp);
                }

    The default behavior for transformations is to cache the transformed
    content (if desired, you also can tell Traffic Server to cache
    untransformed content). Therefore, only responses received directly
    from an origin server need to be transformed. Objects served from
    the cache are already transformed. To determine whether the response
    is from the origin server, the routine transformable checks the
    response header for the "200 OK" server response.

    .. code-block:: c

       {
          TSMBuffer bufp;
          TSMLoc hdr_loc;
          TSHttpStatus resp_status;

          TSHttpTxnServerRespGet (txnp, &bufp, &hdr_loc);

          if(TS_HTTP_STATUS_OK==
             (resp_status=TSHttpHdrStatusGet(bufp,hdr_loc)))
          {
             return 1;
          }
          else {
             return 0;
          }
       }

#. If the response is transformable, then the plugin creates a
   transformation vconnection that gets called back when the response
   data is ready to be transformed (as it is streaming from the origin
   server).

   .. code-block:: c

      static void transform_add (TSHttpTxn txnp)
      {
         TSVConn connp;
         connp = TSTransformCreate (bnull_transform, txnp);
         TSHttpTxnHookAdd (txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
      }

   The previous code fragment shows that the handler function for the
   transformation vconnection is ``bnull_transform``.

#. The ``bnull_transform`` function has to handle ``ERROR``,
   ``WRITE_COMPLETE``, ``WRITE_READY``, and ``IMMEDIATE`` events. If
   the transform is just beginning, the event received is probably
   ``IMMEDIATE``. The ``bnull_transform`` function calls
   ``handle_transform`` to handle ``WRITE_READY`` and ``IMMEDIATE``.

#. The ``handle_transform`` function examines the data parameter for
   the continuation passed to it (the continuation passed to
   ``handle_transform`` is the transformation vconnection). The data
   structure keeps track of two states: copying the data into the
   buffer (``STATE_BUFFER_DATA``) and writing the contents of the
   buffer to the output vconnection (``STATE_OUTPUT_DATA``).

#. Get a handle to the input VIO (see the ``handle_buffering``
   function). ``input_vio = TSVConnWriteVIOGet (contp);`` This is so
   that the transformation can get information about the upstream
   vconnection's write operation to the input buffer.

#. Copy data from the input buffer to the output buffer. See the
   ``handle_buffering`` function for the following code fragment:

   .. code-block:: c

      TSIOBufferCopy (data->output_buffer,
         TSVIOReaderGet (write_vio), towrite, 0);

#. Tell the input buffer that the transformation has read the data. See
   the ``handle_buffering`` function for the following code fragment:

   .. code-block:: c

      TSIOBufferReaderConsume (TSVIOReaderGet (write_vio), towrite);

#. Modify the input VIO to tell it how much data has been read
   (increase the value of ``ndone``). See the ``handle_buffering``
   function for the following code fragment:

   .. code-block:: c

      TSVIONDoneSet (write_vio, TSVIONDoneGet (write_vio) + towrite); }

#. If there is more data left to read ( if ndone < nbytes), then the
   ``handle_buffering`` function wakes up the upstream vconnection by
   sending it ``WRITE_READY``:

   .. code-block:: c

      if (TSVIONTodoGet (write_vio) > 0) {
         if (towrite > 0) {
            TSContCall (TSVIOContGet (write_vio),
               TS_EVENT_VCONN_WRITE_READY, write_vio);
         }
      } else {

   The process of passing data through the transformation is
   illustrated in the following diagram. The transformation sends
   ``WRITE_READY`` events when it needs more data; when data is
   available, the upstream vconnection reenables the transformation
   with an ``IMMEDIATE`` event.

   The following diagram illustrates the read from an input
   vconnection:

   **Reading Data Into the Buffer (the ``STATE_BUFFER_DATA`` State)**
   {#ReadingDataIntoBuffer}

   .. figure:: /static/images/sdk/vconn_buffer.jpg
      :alt: Reading Data Into the Buffer the STATE\_BUFFER\_DATA State

      Reading Data Into the Buffer the STATE\_BUFFER\_DATA State

#. When the data is read into the output buffer, the
   ``handle_buffering`` function sets the state of the transformation's
   data structure to ``STATE_OUTPUT_DATA`` and calls the upstream
   vconnection back with the ``WRITE_COMPLETE`` event.

   .. code-block:: c

      data->state = STATE_OUTPUT_DATA;
      TSContCall (TSVIOContGet (write_vio),
         TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);

#. The upstream vconnection will probably shut down the write operation
   when it receives the ``WRITE_COMPLETE`` event. The handler function
   of the transformation, ``bnull_transform``, receives an
   ``IMMEDIATE`` event and calls the ``handle_transform`` function.
   This time, the state is ``STATE_OUTPUT_DATA``, so
   ``handle_transform`` calls ``handle_output``.

#. The ``handle_output`` function gets a handle to the output
   vconnection: ``output_conn = TSTransformOutputVConnGet (contp);``

#. The ``handle_output`` function writes the buffer to the output
   vconnection:

   .. code-block:: c

      data->output_vio =
         TSVConnWrite (output_conn, contp, data->output_reader,
         TSIOBufferReaderAvail (data->output_reader) );

   The following diagram illustrates the write to the output
   vconnection:

   **Writing the Buffered Data to the Output Vconnection**
   {#WritingBufferedtDataIntoVConnection}

   .. figure:: /static/images/sdk/vconn_buf_output.jpg
      :alt: Writing the Buffered Data to the Output Vconnection

      Writing the Buffered Data to the Output Vconnection

