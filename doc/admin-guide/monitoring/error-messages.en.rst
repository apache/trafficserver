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

.. include:: ../../common.defs

.. _admin-monitoring-errors:

Error Messages
**************

The following table lists messages that can appear in system log files.
This list is not exhaustive; it simply describes common warning messages
that can occur and which might require your attention.

Fatal Process Messages
======================

``Accept port is not between 1 and 65535. Please check configuration``
   The port specified in :file:`records.config` that accepts
   incoming HTTP requests is not valid.

``Self loop is detected in parent proxy configuration``
   The name and port of the parent proxy match that of Traffic Server.
   This creates a loop when Traffic Server attempts to send the request
   to the parent proxy.

Process Warnings
================

``<Logfile> error: error_number``
   Generic logging error.

``Connect by disallowed client <IP address>, closing``
   The specified client is not allowed to connect to Traffic Server;
   the client IP address is not listed in the ``ip_allow.yaml`` file.

``Could not rename log <filename> to <rolled filename>``
   System error when renaming log file during roll.

``Log format symbol <symbol name> not found``
   Custom log format references a field symbol that does not exist.
   Refer to :ref:`admin-logging-formats`.

``Missing field for field marker``
   Error reading a log buffer.

``Unable to open log file <filename>, errno=<error number>``
   Cannot open the log file.

``Error accessing disk <disk name>``
   Traffic Server might have a cache read problem. You might need to
   replace the disk.

``Too many errors accessing disk <disk name>: declaring disk bad``
   Traffic Server is not using the cache disk because it encountered
   too many errors. The disk might be corrupt and might have to be
   replaced.

``No cache disks specified in storage.config file: cache disabled``
   The Traffic Server :file:`storage.config` file does not list any cache
   disks; Traffic Server is running in proxy-only mode. You must add
   the disks you want to use for the cache to :file:`storage.config`.

Alarm Messages
==============

``[ConfigManager::ConfigManager] Config file is read-only: <filename>``
   Go to the Traffic Server ``config`` directory and check the
   indicated file permissions; change if necessary.

``[ConfigManager::ConfigManager] Unable to read or write config file <filename>``
   Go to the Traffic Server ``config`` directory and make sure the
   indicated file exists. Check permissions and modify if necessary.

``[Traffic Manager] Configuration File Update Failed: <error number>``
   Go to the Traffic Server ``config`` directory and check the
   indicated file permissions; change if necessary.

``[Traffic Manager] Mgmt <==>Proxy conn. closed``
   An informational message to inform you that the :program:`traffic_server`
   process is down.

``Access logging suspended - configured space allocation exhausted.``
   The space allocated to the event log files is full; you must either
   increase the space or delete some log files so that access logging
   to continue. To prevent this error, consider rolling log files more
   frequently and enabling the autodelete feature.

``Access logging suspended - no more space on the logging partition.``
   The entire partition containing the event logs is full; you must
   delete or move some log files to enable access logging to continue.
   To prevent this error, consider rolling log files more frequently
   and enabling the autodelete feature.

``Created zero length place holder for config file <filename>``
   Go to the Traffic Server ``config`` directory and check the
   indicated file. If it is indeed zero in length, then use a backup
   copy of the configuration file.

``Traffic Server could not open logfile <filename>``
   Check permissions for the indicated file and the logging directory.

``Traffic Server failed to parse line <line number> of the logging config file <filename>``
   Check your custom log configuration file; there could be syntax
   errors. Refer to :ref:`admin-logging-fields` for correct custom log format fields.

``vip_config binary is not setuid root, manager will be unable to enable virtual ip addresses``
   The :program:`traffic_manager` process is not able to set virtual IP
   addresses. You must ``setuid root`` for the ``vip_config`` file in
   the Traffic Server ``bin`` directory.

.. _body-factory:

HTML Messages Sent to Clients
=============================

Traffic Server returns detailed error messages to client browsers when there are
problems with the HTTP transactions requested by the browser. These Traffic
Server response messages correspond to standard HTTP response codes, but provide
more information. A list of the more frequently encountered HTTP response codes
is provided in :ref:`appendix-http-status-codes`.

The error messages can be customized. The actual response is generated from a template. These
templates are stored in files which means the errors responses can be customized by modifying these
files. The default directory for the template files is ``PREFIX/body_factory/default`` but this can
be changed by the configuration variable :ts:cv:`proxy.config.body_factory.template_sets_dir`. All
files in this directory are added to a lookup table which is consulted when the error message is
generated. The name used for lookup is by default that listed in the :ref:`following table
<body-factory-error-table>`. It can be overridden by
:ts:cv:`proxy.config.body_factory.template_base` which, if set, is a string that is prepended to the
search name along with an underscore. For example, if the default lookup name is
``cache#read_error`` then by default the response will be generated from the template in the file
named ``cache#read_error``. If the template base name were set to ``apache`` then the lookup would
look for a file named ``apache_cache#read_error`` in the template table. This can be used to switch
out error message sets or, because this variable is overridable, to select an error message set
based on data in the transaction. In addition the suffix ``_default`` has a special meaning. If
there is a file with the base name and that suffix it is used as the default error page for the base
set, instead of falling back to the global (built in) default page in the case where there is not a
file that matches the specific error. In the example case, if the file ``apache_default`` exists
it would be used instead of ``cache#read_error`` if there is no ``apache_cache#read_error``.

The text for an error message is processed as if it were a :ref:`admin-logging-fields` which
enables customization by values present in the transaction for which the error occurred.

The following table lists the hard-coded Traffic Server HTTP messages,
with corresponding HTTP response codes and customizable files.

.. _body-factory-error-table:

``Access Denied``
   ``403``
   You are not allowed to access the document at location ``URL``.
   ``access#denied``

``Cache Read Error``
   ``500``
   Error reading from cache; please retry request.
   ``cache#read_error``

``Connection Timed Out``
   ``504``
   Too much time has elapsed since the server has sent data.
   ``timeout#inactivity``

``Content Length Required``
   ``400``
   Could not process this request because ``Content-Length`` was not specified.
   ``request#no_content_length``

``Cycle Detected``
   ``400``
   Your request is prohibited because it would cause an HTTP proxy cycle.
   ``request#cycle_detected``

``Forbidden``
    ``403``
    ``<port number>`` is not an allowed port for SSL connections (you have made a request for a secure SSL connection to a forbidden port  number).
    ``access#ssl_forbidden``

``Host Header Required``
   ``400``
   An attempt was made to transparently proxy your request, but this
   attempt failed because your browser did not send an HTTP ``Host``
   header. Manually configure your browser to use
   ``http://<proxy name>:<proxy port>`` as the HTTP
   proxy. Alternatively, end users can upgrade to a browser that
   supports the HTTP ``Host`` header field.
   ``interception#no_host``

``Host Header Required``
   ``400``
   Because your browser did not send a ``Host`` HTTP header field, the
   virtual host being requested could not be determined. To access the
   website correctly, you must upgrade to a browser that supports the
   HTTP ``Host`` header field.
   ``request#no_host``

``HTTP Version Not Supported``
   ``505``
   The origin server ``<server name>`` is using an unsupported version
   of the HTTP protocol.
   ``response#bad_version``

``Invalid Content Length``
   ``400``
   Could not process this request because the specified ``Content-Length``
   was invalid (less than 0)..
   ``request#invalid_content_length``

``Invalid HTTP Request``
   ``400``
   Could not process this ``<client request>`` HTTP method request for ``URL``.
   ``request#syntax_error``

``Invalid HTTP Response``
   ``502``
   The host ``<server name>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Malformed Server Response``
   ``502``
   The host ``<server name>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Malformed Server Response Status``
   ``502``
   The host ``<server name>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Maximum Transaction Time exceeded``
   ``504``
   Too much time has elapsed while transmitting document ``URL``.
   ``timeout#activity``

``No Response Header From Server``
   ``502``
   The host ``<server name>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Not Cached``
   ``504``
   This document was not available in the cache, and you (the client)
   only accept cached copies.
   ``cache#not_in_cache``

``Not Found on Accelerator``
   ``404``
   The request for ``URL`` on host ``<server name>`` was not found.
   Check the location and try again.
   ``urlrouting#no_mapping``

``NULL``
   ``502``
   The host ``<hostname>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Proxy Authentication Required``
   ``407``
   Please log in with username and password.
   ``access#proxy_auth_required``

``Server Hangup``
   ``502``
   The server ``<hostname>`` closed the connection before the transaction was completed.
   ``connect#hangup``

``Temporarily Moved``
   ``302``
   The document you requested, ``URL``, has moved to a new location. The new location is ``<new URL>``.
   ``redirect#moved_temporarily``

``Transcoding Not Available``
   ``406``
   Unable to provide the document ``URL`` in the format requested by your browser.
   ``transcoding#unsupported``

``Tunnel Connection Failed``
   ``502``
   Could not connect to the server ``<hostname>``.
   ``connect#failed_connect``

``Unknown Error``
   ``502``
   The host ``<hostname>`` did not return the document ``URL`` correctly.
   ``response#bad_response``

``Unknown Host``
   ``500``
   Unable to locate the server named ``<hostname>``; the server does
   not have a DNS entry. Perhaps there is a misspelling in the server
   name or the server no longer exists; double-check the name and try
   again.
   ``connect#dns_failed``

``Unsupported URL Scheme``
   ``400``
   Cannot perform your request for the document ``URL`` because the
   protocol scheme is unknown.
   ``request#scheme_unsupported``

``URI Too Long``
   ``414``
   Could not process this request because the request uri
   was too long ..
   ``request#uri_len_too_long``

