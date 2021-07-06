.. _icap-plugin:
.. include:: ../../common.defs

ICAP Plugin
***********

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


The ICAP plugin enables passing along the response header and body to
an external server for additional processing (generally scanning) using
the ICAP protocol.  The scanner, depending on whether the file contains
malicious content, will return a transformed version of the file. If the
file is clean, then the exact same file will be returned. Otherwise, the
scanner will block the file and return a message containing information
regarding the infection.

Installation
------------

This plugin is only built if the configure option ::

    --enable-experimental-plugins

is given at build time.

To use this plugin, you need to compile the plugin and add the following line in plugin.config (assuming the compiled plugin library is called icap_plugin.so):

.. code-block:: text

   icap_plugin.so scanner_server_ip scanner_server_port avoid_port debug_enable

There are 4 parameters to this plugin: the scanner_server_ip and scanner_server_port (standard ICAP port is 1344) is required to initiate connection to the ICAP server, and the avoid_port is specified to avoid processing traffic that appears on particular ports (e.g, requests passed via the parent select and already checked).  Parameter debug_enable will enable debug mode, which, in cases of ICAP server connection error or ICAP server too busy to handle the request, will return the origin response body to the client instead of an error message.

ICAP
----

`ICAP <https://tools.ietf.org/html/rfc3507>`__ is a light-weight protocol for executing a remote procedure call on HTTP messages. It is very handy to request transformations on HTTP requests and responses since ICAP servers are set up on the edge of the Internet. Therefore, ICAP has become an industry standard for Anti Virus service.

Virus Scanning via ICAP
-----------------------

Performing virus scanning via ICAP has several advantages over the traditional local host virus scanner deployment model. By performing virus scanner on a remote server, local resources are not used, allowing the main thread on the server be more performant. ICAP servers are frequently deployed on the edge near |TS| proxies, adding minimal latency on file transmission.

Plugin Design
-------------

The plugin is a transform plugin that is called on READ_RESPONSE_HDR_HOOK. Upon initiating the transform, the plugin will initiate a socket connection to the scanner server. Once the connection is made successfully, the plugin will formulate an ICAP request header, and send it out along with the response body from origin server. Since ICAP requires chunked transfer-encoding for transmitting body, the request sending to the scanner server should take the following form:

.. code-block:: text

   RESPMOD icap://127.0.0.1/avscan ICAP/1.0
   Host: 127.0.0.1
   Encapsulated: req-hdr=0, res-hdr=137, res-body=296

   GET /origin-resource HTTP/1.1
   Host: www.origin-server.com
   Accept: text/html, text/plain, image/gif
   Accept-Encoding: gzip, compress

   HTTP/1.1 200 OK
   Date: Mon, 10 Jan 2000 09:52:22 GMT
   Server: Apache/1.3.6 (Unix)
   ETag: "63840-1ab7-378d415b"
   Content-Type: text/html
   Content-Length: 51

   33
   This is data that was returned by an origin server.
   0

After finishing sending the message, the plugin will receive the returned message from the scanner and try to determine the action to take. ICAP's response message takes the following form:

.. code-block:: text

   ICAP/1.0 200 OK
   Date: Mon, 10 Jan 2000  09:55:21 GMT
   Server: ICAP-Server-Software/1.0
   Connection: close
   ISTag: "W3E4R7U9-L2E4-2"
   Encapsulated: res-hdr=0, res-body=222

   HTTP/1.1 200 OK
   Date: Mon, 10 Jan 2000  09:55:21 GMT
   Via: 1.0 icap.example.org (ICAP Example RespMod Service 1.1)
   Server: Apache/1.3.6 (Unix)
   ETag: "63840-1ab7-378d415b"
   Content-Type: text/html
   Content-Length: 95

   5f
   This is data that was returned by an origin server, but with
   value modified by an ICAP server.
   0

In case of virus detected, the scanner will include fields like "X-Infection-Found" and "X-Violations-Found" in the ICAP response header. Therefore, by evaluating the ICAP header, if infection headers are found, then then plugin will discard the HTTP response header from the origin server and use the HTTP header returned by ICAP server to generate the response header. Otherwise, the file is clean, in which case headers will not be modified. Once finished reading headers, the plugin proceeds to read the message body, which is, if file is clean, an exact copy of the response from origin server or, if file is bad, an error message generated by scanner server.

Scanner Server
--------------

We have tested with the `Symantec Protection Engine <https://support.symantec.com/us/en/article.doc11058.html>`__. We have also tested with the open-source ICAP server `C-ICAP <https://sourceforge.net/projects/c-icap/>`__ with the `ClamAV <https://www.clamav.net/>`__ scanning module.

Limitations
-----------

In the current version, the plugin only supports IPv4 addressing from the plugin to the ICAP server.

The plugin only processes responses.  It could be extended to also support passing the request headers and bodies to ICAP servers for processing.
