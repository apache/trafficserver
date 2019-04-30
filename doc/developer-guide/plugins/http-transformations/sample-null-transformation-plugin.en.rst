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

.. _developer-plugins-http-transformations-null-transform:

Sample Null Transform Plugin
****************************

This section provides a step-by-step description of what the null
transform plugin does, along with sections of code that apply. For
context, you can find each code snippet in the complete source code.
Some of the error checking details are left out - to give the
description a step-by-step flow, only the highlights of the transform
are included.

Below is an overview of the null transform plugin:

1.  Gets a handle to HTTP transactions.

    .. code-block:: c

        void
        TSPluginInit (int argc, const char *argv[]) {
            TSHttpHookAdd (TS_HTTP_READ_RESPONSE_HDR_HOOK,
                    TSContCreate (transform_plugin, NULL));

    With this ``TSPluginInit`` routine, the plugin is called back every
    time Traffic Server reads a response header.

2.  Checks to see if the transaction response is transformable.

    .. code-block:: c

        static int transform_plugin (TSCont contp, TSEvent event, void *edata) {
            TSHttpTxn txnp = (TSHttpTxn) edata;
            switch (event) {
                case TS_EVENT_HTTP_READ_RESPONSE_HDR:
                    if (transformable (txnp)) {
                        transform_add (txnp);
                    }

    The default behavior for transformations is to cache the transformed
    content (you can also tell Traffic Server to cache untransformed
    content, if you want). Therefore, only responses received directly
    from an origin server need to be transformed. Objects served from
    cache are already transformed. To determine whether the response is
    from the origin server, the routine ``transformable`` checks the
    response header for the "200 OK" server response.

    .. code-block:: c

        static int transformable (TSHttpTxn txnp)
        {
            TSMBuffer bufp;
            TSMLoc hdr_loc;
            TSHttpStatus resp_status;
            TSHttpTxnServerRespGet (txnp, &bufp, &hdr_loc);

            if (TS_HTTP_STATUS_OK == (resp_status =
                        TSHttpHdrStatusGet (bufp, hdr_loc)) ) {
                return 1;
            } else {
                return 0;
            }
        }

3.  If the response is transformable, then the plugin creates a
    transformation vconnection that gets called back when the response
    data is ready to be transformed (as it is streaming from the origin
    server).

    .. code-block:: c

        static void transform_add (TSHttpTxn txnp)
        {
            TSVConn connp;
            connp = TSTransformCreate (null_transform, txnp);
            TSHttpTxnHookAdd (txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
        }

    The previous code fragment shows that the handler function for the
    transformation vconnection is ``null_transform``.

4.  Get a handle to the output vconnection (that receives data from the
    transformation).

    .. code-block:: c

        output_conn = TSTransformOutputVConnGet (contp);

5.  Get a handle to the input VIO. (See the ``handle_transform``
    function.)

    .. code-block:: c

        input_vio = TSVConnWriteVIOGet (contp);

    This is so that the transformation can get information about the
    upstream vconnection's write operation to the input buffer.

6.  Initiate a write to the output vconnection of the specified number
    of bytes. When the write is initiated, the transformation expects to
    receive ``WRITE_READY``, ``WRITE_COMPLETE``, or ``ERROR`` events
    from the output vconnection. See the ``handle_transform`` function
    for the following code fragment:

    .. code-block:: c

        data->output_vio = TSVConnWrite (output_conn, contp,
            data->output_reader, TSVIONBytesGet (input_vio));

7.  Copy data from the input buffer to the output buffer. See the
    ``handle_transform`` function for the following code fragment:

    .. code-block:: c

        TSIOBufferCopy (TSVIOBufferGet (data->output_vio),
                TSVIOReaderGet (input_vio), towrite, 0);

8.  Tell the input buffer that the transformation has read the data. See
    the ``handle_transform`` function for the following code fragment:

    .. code-block:: c

        TSIOBufferReaderConsume (TSVIOReaderGet (input_vio), towrite);

9.  Modify the input VIO to tell it how much data has been read
    (increase the value of ``ndone``). See the ``handle_transform``
    function for the following code fragment:

    .. code-block:: c

        TSVIONDoneSet (input_vio, TSVIONDoneGet (input_vio) + towrite);

10. If there is more data left to read ( if ndone < nbytes), then the
    ``handle_transform`` function wakes up the downstream vconnection
    with a reenable and wakes up the upstream vconnection by sending it
    ``WRITE_READY``:

    .. code-block:: c

        if (TSVIONTodoGet (input_vio) > 0) {
            if (towrite > 0) {
                TSVIOReenable (data->output_vio);

                TSContCall (TSVIOContGet (input_vio),
                        TS_EVENT_VCONN_WRITE_READY, input_vio);
            }
            } else {

    The process of passing data through the transformation is
    illustrated in the following diagram. The downstream vconnections
    send ``WRITE_READY`` events when they need more data; when data is
    available, the upstream vconnections reenable the downstream
    vconnections. In this instance, the ``TSVIOReenable`` function sends
    ``TS_EVENT_IMMEDIATE``.

    **Passing Data Through a Transformation**
    {#PassingDataThroughaTransformation}

.. figure:: /static/images/sdk/vconnection1.jpg
      :alt: Passing Data Through a Transformation

      Passing Data Through a Transformation

11. If the ``handle_transform`` function finds there is no more data to
    read, then it sets ``nbytes`` to ``ndone`` on the output
    (downstream) VIO and wakes up the output vconnection with a
    reenable. It then triggers the end of the write operation from the
    upstream vconnection by sending the upstream vconnection a
    ``WRITE_COMPLETE`` event.

    .. code-block:: c

      TSVIONBytesSet (data->output_vio, TSVIONDoneGet (input_vio));
         TSVIOReenable (data->output_vio);
         TSContCall (TSVIOContGet (input_vio),
            TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
      }

    When the upstream vconnection receives the ``WRITE_COMPLETE`` event,
    it will probably shut down the write operation.

12. Similarly, when the downstream vconnection has consumed all of the
    data, it sends the transformation a ``WRITE_COMPLETE`` event. The
    transformation handles this event with a shut down (the
    transformation shuts down the write operation to the downstream
    vconnection). See the ``null_plugin`` function for the following
    code fragment:

    .. code-block:: c

        case TS_EVENT_VCONN_WRITE_COMPLETE:
            TSVConnShutdown (TSTransformOutputVConnGet (contp), 0, 1
            break;

    The following diagram illustrates the flow of events:

    **Ending the Transformation** {#EndingTransformation}

    .. figure:: /static/images/sdk/vconnection2.jpg
       :alt: Ending the Transformation

       Ending the Transformation

