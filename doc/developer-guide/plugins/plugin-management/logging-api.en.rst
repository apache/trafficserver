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

.. _developer-plugins-management-logging-api:

Logging API
***********

The logging API enables your plugin to log entries in a custom text log
file that you create with the call :c:func:`TSTextLogObjectCreate`. This log
file is part of Traffic Server's logging system; by default, it is
stored in the logging directory. Once you have created the log object,
you can set log properties.

The logging API enables you to:

-  Establish a custom text log for your plugin: see
   :c:func:`TSTextLogObjectCreate`

-  Set the log header for your custom text log: see
   :c:func:`TSTextLogObjectHeaderSet`

-  Enable or disable rolling your custom text log: see
   :c:func:`TSTextLogObjectRollingEnabledSet`

-  Set the rolling interval (in seconds) for your custom text log: see
   :c:func:`TSTextLogObjectRollingIntervalSecSet`

-  Set the rolling offset for your custom text log: see
   :c:func:`TSTextLogObjectRollingOffsetHrSet`

-  Set the rolling size for your custom text log: see
   :c:func:`TSTextLogObjectRollingSizeMbSet`

-  Write text entries to the custom text log: see
   :c:func:`TSTextLogObjectWrite`

-  Flush the contents of the custom text log's write buffer to disk: see
   :c:func:`TSTextLogObjectFlush`

-  Destroy custom text logs when you are done with them: see
   :c:func:`TSTextLogObjectDestroy`

The steps below show how the logging API is used in the
``blacklist_1.c`` sample plugin. For the complete source code, see the
:ref:`developer-plugins-examples-blacklist-code` section.

#. A new log file is defined as a global variable.

   .. code-block:: c

         static TSTextLogObject log;

#. In ``TSPluginInit``, a new log object is allocated:

   .. code-block:: c

           TSReturnCode error = TSTextLogObjectCreate("blacklist",
                                TS_LOG_MODE_ADD_TIMESTAMP, &log);

   The new log is named ``blacklist.log``. Each entry written to the log
   will have a timestamp. The ``NULL`` argument specifies that the new
   log does not have a log header. The error argument stores the result
   of the log creation; if the log is created successfully, then an
   error will be equal to ``TS_LOG_ERROR_NO_ERROR``.

#. After creating the log, the plugin makes sure that the log was
   created successfully:

   .. code-block:: c

       if (error != TS_SUCCESS) {
           printf("Blacklist plugin: error %d while creating log\n", error);
       }

#. The :ref:`developer-plugins-examples-blacklist` matches the host portion of
   the URL (in each client request) with a list of blacklisted sites (stored in
   the array ``sites[]``):

   .. code-block:: c

       for (i = 0; i < nsites; i++) {
         if (strncmp (host, sites[i], host_length) == 0) {
           /* ... */
         }
       }

   If the host matches one of the blacklisted
   sites (such as ``sites[i]``), then the plugin writes a blacklist
   entry to ``blacklist.log``:

   .. code-block:: c

       if (log) { TSTextLogObjectWrite(log, "blacklisting site: %s",
       sites[i]);

   The format of the log entry is as follows:

   ::

       blacklisting site: sites[i]

   The log is not flushed or
   destroyed in the ``blacklist_1`` plugin - it lives for the life of
   the plugin.


