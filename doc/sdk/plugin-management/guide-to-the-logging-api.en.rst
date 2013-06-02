Guide to the Logging API
************************

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

The logging API enables your plugin to log entries in a custom text log
file that you create with the call ``TSTextLogObjectCreate``. This log
file is part of Traffic Server's logging system; by default, it is
stored in the logging directory. Once you have created the log object,
you can set log properties.

The logging API enables you to:

-  Establish a custom text log for your plugin: see
   ```TSTextLogObjectCreate`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ae75e85e476efeaa16ded185da7a3081b>`__

-  Set the log header for your custom text log: see
   ```TSTextLogObjectHeaderSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a7c10f89fe8bcb6b733f4a83b5a73b71c>`__

-  Enable or disable rolling your custom text log: see
   ```TSTextLogObjectRollingEnabledSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#aec1e883f0735ee40c8b56d90cf27acd1>`__

-  Set the rolling interval (in seconds) for your custom text log: see
   ```TSTextLogObjectRollingIntervalSecSet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#aac0be2e81694db0363c5321e8a2019ce>`__

-  Set the rolling offset for your custom text log: see
   ```TSTextLogObjectRollingOffsetHrSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a9d90885b975947c241f74c33550180b4>`__

-  Write text entries to the custom text log: see
   ```TSTextLogObjectWrite`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a34de01e5603ea639d7ce6c7bf9613254>`__

-  Flush the contents of the custom text log's write buffer to disk: see
   ```TSTextLogObjectFlush`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad746b22f992c2adb5f0271e5136a6ca1>`__

-  Destroy custom text logs when you are done with them: see
   ```TSTextLogObjectDestroy`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#af6521931ada7bbc38194e79e60081d54>`__

The steps below show how the logging API is used in the
``blacklist-1.c`` sample plugin. For the complete source code, see the
`*Sample Source Code* <App_SampleSourceCode.html>`__ appendix.

1. A new log file is defined as a global variable.

   ::

         ::::c
         static TSTextLogObject log;

2. In ``TSPluginInit``, a new log object is allocated:

   ::

           ::::c
           log = TSTextLogObjectCreate("blacklist", TS_LOG_MODE_ADD_TIMESTAMP, NULL, &error);

   The new log is named ``blacklist.log``. Each entry written to the log
   will have a timestamp. The ``NULL`` argument specifies that the new
   log does not have a log header. The error argument stores the result
   of the log creation; if the log is created successfully, then an
   error will be equal to ``TS_LOG_ERROR_NO_ERROR``.

3. After creating the log, the plugin makes sure that the log was
   created successfully:

   ::

       ::::c
       if (!log) {
           printf("Blacklist plugin: error %d while creating log\n", error);
       }

4. The ``blacklist-1`` plugin matches the host portion of the URL (in
   each client request) with a list of blacklisted sites (stored in the
   array ``sites[``]):

   ::::c for (i = 0; i < nsites; i++) { if (strncmp (host, sites[i],
   host\_length) == 0) { If the host matches one of the blacklisted
   sites (such as ``sites[i]``), then the plugin writes a blacklist
   entry to ``blacklist.log``:

   ::::c if (log) { TSTextLogObjectWrite(log, "blacklisting site: %s",
   sites[i]); The format of the log entry is as follows:

   :::text blacklisting site: sites[i] The log is not flushed or
   destroyed in the ``blacklist-1`` plugin - it lives for the life of
   the plugin.


