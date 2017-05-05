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

.. _logging-architecture-introduction:

A gentle introduction to ATS logging internals
**********************************************

Preface
=======

The logging subsystem is a rather large and tricky section of the code
base. You'll find that over the years, as people have come and gone,
large swathes of the code may lack comments and/or documentation. Even
worse, when there are comments, some (but not all) might be flat out
wrong or outdated.

Your author has put in some effort in adding comments and removing wrong
documentation, but the effort is ongoing.

Note: before reading this, make sure you read the :ref:`admin-logging`
chapter so you don't lose sight of the big picture.

Memory layout
=============

Here we will discuss the internal (and in the case of binary logging,
also external) memory layout for logs. Keep in mind that you should
revisit this section after reading the rest of this doc.

Log data for each transaction (henceforth called a log entry) is stored in a
``LogBuffer``. There may be more than one log entry in each ``LogBuffer``. Each
``LogBuffer`` is prepended with a ``LogBufferHeader``. Each log entry is
prepended with a ``LogEntryHeader``. In this manner, the layout for a single
``LogBuffer`` might look something like this:

::

                                         free space
          LogBuffer                      |
                                         v
        +--+--+----+--+---+--+-----+------------+
        |bh|eh|eeee|eh|eee|eh|eeeee|xxxxxxxxxxxx|
        +--+--+----+--+---+--+-----+------------+
         ^  ^   ^   ^
         |  |   |   |
         |  |   |   +- a LogEntryHeader
         |  |   +----- actual log entry data
         |  +--------- a LogEntryHeader describing the entry
         +------------ a LogBufferHeader containing info about the log entries

Important data structures
=========================

There are a lot of data structures present in the logging code, but
undoubtedly the two most important are ``LogObject`` and ``LogBuffer``.
They are defined in ``proxy/logging/LogObject.h`` and
``proxy/logging/LogBuffer.h``, respectively.

LogObject
---------

Each ``LogObject`` represents a logical ATS logging object. This may
sound tautological, but that's because the implementation fits the
abstraction well. Hand in glove, so to speak. In typical cases (with the
notable exceptions of logging to pipe and logging over network), a
``LogObject`` will map to a file on disk.

When a logging event occurs, ATS will cycle through all the configured
``LogObject``\ s and attempt to save that logging event to each
``LogObject``. In this way, the same event can be saved in a variety of
different formats and places.

The list of ``LogObject``\ s is stored in the ``LogObjectManager`` class,
defined in ``proxy/logging/LogObject.h``. There is one and only one
``LogObjectManager`` instance stored inside the ``LogConfig`` instance, which
is in turn stored inside static ``Log`` class. As indicated by the decades old
comment in ``Log.h``, the ``Log`` class should ideally be converted to a
namespace. Feeling confused yet? We're just getting started.

Brief detour: ``LogConfig`` stores all the configuration the logging
subsystem needs. Pretty straightforward.

LogBuffer
---------

The ``LogBuffer`` class is designed to provide a thread-safe mechanism
to buffer/store log entries before they’re flushed. To reduce system call
overhead, ``LogBuffer``\ s are designed to avoid heavy-weight mutexes in
favor of using lightweight atomics built on top of compare-and-swap
operations. When a caller wants to write into a ``LogBuffer``, the
caller “checks out” a segment of the buffer to write into. ``LogBuffer``
makes sure that no two callers are served overlapping segments. To
illustrate this point, consider this diagram of a buffer:

::

                  LogBuffer instance
              +--------------------------------+
              | thread_1's segment             |
              |--------------------------------|
              | thread_2's segment             |
              |                                |
              |                                |
              |--------------------------------|
              | thread_3's segment             |
              |                                |
              |                                |
              |                                |
              |--------------------------------|
              | thread_4's segment             |
              |--------------------------------|
              | <unused>                       |
              |                                |
              |                                |
              |                                |
              |                                |
              |                                |
              |                                |
              |                                |
              +--------------------------------+

In this manner, since no two threads are writing in the other’s segment,
we avoid race conditions during the actual logging. This also makes
LogBuffer’s critical section extremely small. In fact, the only time we
need to enter a critical section is when we do the book keeping to keep
track of which segments are checked out. Despite this, it's not unusual
to see between 5% and 20% of total processor time spent inside ``LogBuffer``
serialization code. It's unclear at this time whether or not actual locks
will improve performance, so further performance testing is still necessary.

There's a lot more that could be said about ``LogBuffer``. If you're
interested, come read it on the author's `personal
website <https://dxuuu.xyz/optimistic-concurrency.html>`__

Brief overview of the code
==========================

Here I'll cover the most important parts of the logging code. Note that
what's being covered here is the main data path, the path user agent
accesses take to getting into a log file. Much more can be said about
the rest of the logging code, but it's all rather trivial to manually
figure out once you know the data path and data structures. In an effort
to keep this document timeless, we will avoid documenting more code than
this.

``proxy/logging/Log.h`` and ``proxy/logging/Log.cc`` are the entry
points into the logging subsystem. There are a few notable functions in
``Log.cc`` that we should pay close attention to: ``Log::access(..)``,
``Log::error(..)``, ``preproc_thread_main(..)``, and
``flush_thread_main(..)``.

``Log::access(..)`` and ``Log::error(..)``
------------------------------------------

These two functions are the entirety of the API that the logging
subsystem exposes to the rest of ATS. ``Log::access(..)`` records access
events, eg. when a user agent requests a document through ATS. These
entries are typically sent to ``squid.[b]log``. ``Log::error(..)`` is
used to put error logs into ``error.log``.

``preproc_thread_main(..)``
---------------------------

``preproc_thread_main(..)`` is a thread that runs inside |ATS|'s event system.
Think of it as just a regular POSIX pthread. This thread periodically takes a
look all the full ``LogBuffer``\ s, does some ``preproc``\ essing work on them,
and then finally adds the full and preprocessed ``LogBuffer``\ s to the
global/static ``Log::flush_data_list``.  ``flush_thread_main(..)`` then
consumes these processed ``LogBuffer``\ s.

``flush_thread_main(..)``
-------------------------

Just like ``preproc_thread_main(..)``, ``flush_thread_main(..)`` is run
in a thread like environment. ``flush_thread_main(..)``'s role is rather
simple.

1. Pop each processed ``LogBuffer`` off the global/static queue.

2. Check to make sure all the file structures underpinning our
   ``LogObject``\ s are good to go.

3. Flush the ``LogBuffer``\ s onto disk or through the network (in the
   case of collated logs).

Misc
====

Adding LogFields
----------------

If you're working with logging code, there's a good chance you'll be
adding more log fields. This isn't so much hard as it's annoying. The
best way to learn all the incantations is to look at an example. For
example, `this
commit <https://github.com/apache/trafficserver/commit/ead9d56da076b43a>`__.
