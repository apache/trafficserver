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

.. _admin-plugins-hook-trace:

Hook Trace Plugin
*****************

The HookTrace plugins shows the fundamental set of hooks that are
commonly used by plugins. It is also useful for tracing the sequence
of hook calls a plugin can receive under different circumstances.

The plugin begins by adding a global hook on each of the hooks it
is interested in. Next, in the event handler, we simply cast the
event data pointer to the expected type.

Hook events can be shown in the |TS| diagnostic log by using the
``hook-trace`` diagnostic tag::

    $ traffic_server -T hook-trace

.. literalinclude:: ../../../plugins/experimental/hook-trace/hook-trace.cc
   :language: c
   :lines: 24-
