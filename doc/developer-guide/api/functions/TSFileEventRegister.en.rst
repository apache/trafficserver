.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSFileEventRegister
*******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSWatchDescriptor TSFileEventRegister(const char *path, TSFileWatchKind kind, TSCont contp)

Description
===========

Attempt to register a watch on a file or directory.  :arg:`contp` will be called when
the OS reports a change in the file system at :arg:`path`.

Types
=====

.. enum:: TSFileWatchKind

   The kind of changes to watch on the path.

   ..  enumerator:: TS_WATCH_CREATE

      Only valid on directories.  :arg:`contp` is called after a file or directory has been
      created under :arg:`path`.  Some operating systems, such as Linux, will supply a file
      name of the newly created file or directory.  This name is passed to :arg:`contp` when
      it's available.

   ..  enumerator:: TS_WATCH_DELETE

      Valid on files and directories.  :arg:`contp` is called after :arg:`path` is deleted.

   ..  enumerator:: TS_WATCH_MODIFY

      Valid on files and directories.  :arg:`contp` is called after :arg:`path` has been modified.

.. struct:: TSFileWatchData

   A class that holds information for the callback.  :arg:`contp` will be called back with
   :arg:`edata` pointing to one of these.

   ..  member:: TSWatchDescriptor wd

      The watch descriptor for that was previously returned from :func:`TSFileEventRegister`.

   ..  member:: const char *name

      Only sometimes populated for :enumerator:`TS_WATCH_CREATE`.  The name of the created
      file.  Note that this name is not always available.  When it's unavailable, the value will
      be :code:`nullptr`.

.. type:: TSWatchDescriptor

   An opaque type that identifies a file system watch.

Return Value
============

Returns a TSWatchDescriptor on success, or -1 on failure.  The caller should store the
returned watch descriptor  .

