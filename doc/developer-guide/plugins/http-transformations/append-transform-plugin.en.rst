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

.. _developer-plugins-http-transformations-append:

Append Transform Plugin
***********************

The append_transform plugin appends text to the body of an HTTP
response. It obtains this text from a file; the name of the file
containing the append text is a parameter you specify in
``plugin.config``, as follows:

::

    append_transform.so path/to/file

The append_transform plugin is based on ``null_transform.c``. The only
difference is that after the plugin feeds the document through the
transformation, it adds text to the response.

Below is a list of the functions in ``append_transform.c``, in the order
they appear in the source code. Below each entry is a description of
what the function does:

-  **``my_data_alloc``**

   Allocates and initializes a ``MyData`` structure. The plugin defines
   a struct, ``MyData``, as follows:

   .. code-block:: c

       typedef struct {
           TSVIO output_vio;
           TSIOBuffer output_buffer;
           TSIOBufferReader output_reader;
           int append_needed;
       } MyData;

   The ``MyData`` structure is used to represent data that the
   transformation (vconnection) needs. The transformation's data pointer
   is set to a ``MyData`` pointer using ``TSContDataSet`` in the
   ``handle_transform`` routine.

-  **``my_data_destroy``**

   Destroys objects of type ``MyData``. To deallocate the transform's
   data, the ``append_transform`` routine (see below) calls
   ``my_data_destroy`` when the transformation is complete.

-  **``handle_transform``**

   This function does the actual data transformation. The transformation
   is created in ``transform_add`` (see below). ``handle_transform`` is
   called by ``append_transform``.

-  **``append_transform``**

   This is the handler function for the transformation vconnection
   created in ``transform_add``. It is the implementation of the
   vconnection.

   -  If the transformation vconnection has been closed, then
      ``append_transform`` calls ``my_data_destroy`` to destroy the
      vconnection.

   -  If ``append_transform`` receives an error event, then it calls
      back the continuation to let it know it has completed the write
      operation.

   -  If it receives a ``WRITE_COMPLETE`` event, then it shuts down the
      write portion of its vconnection.

   -  If it receives a ``WRITE_READY`` or any other event (such as
      ``TS_HTTP_RESPONSE_TRANSFORM_HOOK``), then it calls
      ``handle_transform`` to attempt to transform more data.

-  **``transformable``**

   The plugin transforms only documents that have a content type of
   ``text/html``. This function examines the ``Content-Type`` MIME
   header field in the response header. If the value of the MIME field
   is ``text/html``, then the function returns 1; otherwise, it returns
   zero.

-  **``transform_add``**

   Creates the transformation for the current transaction and sets up a
   transformation hook. The handler function for the transformation is
   ``append_transform``.

-  **``transform_plugin``**

   This is the handler function for the main continuation for the
   plugin. Traffic Server calls this function whenever it reads an HTTP
   response header. ``transform_plugin`` does the following:

   -  Gets a handle to the HTTP transaction being processed

   -  Calls ``transformable`` to determine whether the response document
      content is of type ``text/html``

   -  If the content is transformable, then it calls ``transform_add``
      to create the transformation.

   -  Calls ``TSHttpTxnReenable`` to continue the transaction

-  **``load``**

   Opens the file containing the text to be appended and loads the
   contents of the file into an ``TSIOBuffer`` called ``append_buffer``.

-  **``TSPluginInit``**

   Does the following:

   -  Checks to make sure that the required configuration information
      (the append text filename) is entered in ``plugin.config``
      correctly.

   -  If there is a filename, then ``TSPluginInit`` calls load to load
      the text.

   -  Creates a continuation for the plugin. The handler for this
      continuation is ``transform_plugin``.

   -  Adds the plugin's continuation to
      ``TS_HTTP_READ_RESPONSE_HDR_HOOK``. In other words, it sets up a
      callback of the plugin's continuation when Traffic Server reads
      HTTP response headers.
