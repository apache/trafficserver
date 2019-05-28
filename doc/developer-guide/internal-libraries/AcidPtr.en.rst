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

.. highlight:: cpp
.. default-domain:: cpp

.. |AcidPtr| replace:: :class:`AcidPtr`
.. |AcidCommitPtr| replace:: :class:`AcidCommitPtr`


.. _ACIDPTR:

AcidPtr & AcidCommitPtr
***********************

Synopsis
++++++++

.. code-block:: cpp

   #include "tscore/AcidPtr.h"

|AcidPtr| provides atomic access to a std::shared_ptr.
|AcidCommitPtr| provides exclusive write access to data.

Description
+++++++++++

Provides transparent interface for "copy on write" and "commit when done" in a familiar unique_ptr style.

Named after the desirable properties of a database, ACID_ acronym:

* Atomic - reads and writes avoid skew, by using mutex locks.
* Consistent - data can only be changed by a commit, and only one commit can exist at a time per data.
* Isolated - commits of a single point of data are not concurrent. But commits of separate data can be concurrent.
* Durable - shared_ptr is used to keep older versions of data in memory while references exist.

.. _ACID: https://en.wikipedia.org/wiki/ACID_(computer_science)

.. uml:
   class AcidPtr<T> {
     -std::shared_ptr<T> data
     ----
     +AcidPtr()
     +AcidPtr(T*)
     +std::shared_ptr<const T> getPtr()
     +void commit(T*)
     +AcidCommmitPtr<T> startCommit()
     ----
     ~_finishCommit(T*)
   }

   class AcidCommitPtr<T> {
     -AcidPtr& data_ptr
     -std::unique_lock commit_lock
     ----
     -AcidCommmitPtr() = delete
     +AcidCommmitPtr(AcidPtr&)
     +~AcidCommmitPtr()
     +void abort()
   }

   class std::unique_ptr {

   }

   AcidCommitPtr--|>std::unique_ptr

Performance
-----------
Note this code is currently implemented with mutex locks, it is expected to be fast due to the very brief duration of each lock. It would be plausible to change the implementation to use atomics or spin locks.


|AcidCommitPtr|

* On construction, duplicate values from a shared_ptr to a unique_ptr. (aka copy read to write memory)
* On destruction, move value ptr to shared_ptr. (aka move write ptr to read ptr)

The |AcidCommitPtr| executes this transparent to the writer code. It copies the data on construction, and finalizes on destruction. A MemLock is used to allow exclusive read and write access, however the access is made to as fast as possible.

Use Cases
---------
Implemented for use |ACIDPTR| interface in :class:`Extendible`. But could be used elsewhere without modification.

.. uml::
   :align: center

   title Read while Write

   actor ExistingReaders
   actor NewReaders
   actor Writer
   storage AcidPtr
   card "x=foo" as Data
   card "x=bar" as DataPrime

   ExistingReaders --> Data : read only
   AcidPtr -> Data : shared_ptr
   NewReaders --> AcidPtr : read only
   Writer --> DataPrime : read/write
   Data .> DataPrime : copy

When the writer is done, :func:`~AcidCommitPtr::~AcidCommitPtr()` is called and its |AcidPtr| is updated to point at the written copy, so that future read requests will use it. Existing reader will continue to use the old data.

.. uml::
   :align: center

   title Write Finalize

   actor ExistingReaders
   actor NewReaders
   storage AcidPtr
   card "x=foo" as Data
   card "x=bar" as DataPrime

   ExistingReaders --> Data : read only
   AcidPtr -> DataPrime : shared_ptr
   NewReaders --> AcidPtr : read only


.. uml::
   :align: center

   title AcidPtr Reader/Reader Contention
   box "MemLock"
   participant "Access" as AccessMutex
   end box
   participant AcidPtr
   actor Reader_A #green
   actor Reader_B #red
   Reader_A -[#green]> AcidPtr: getPtr()
   AcidPtr <[#green]- AccessMutex: lock
   activate AccessMutex #green
   Reader_B -> AcidPtr: getPtr()
   AcidPtr -[#green]> Reader_A: copy const shared_ptr
   activate Reader_A
   note left
     Contention limited to
     duration of shared_ptr copy.
     (internally defined)
   end note
   AcidPtr -[#green]> AccessMutex: unlock
   deactivate AccessMutex
   AcidPtr <- AccessMutex: lock
   activate AccessMutex #red
   AcidPtr -> Reader_B: copy const shared_ptr
   activate Reader_B
   AcidPtr -> AccessMutex: unlock
   deactivate AccessMutex


.. uml::
   :align: center

   Title AcidPtr Writer/Reader Contention
   box "MemLock"
   participant "Access" as AccessMutex
   participant "Write" as WriteMutex
   end box
   participant AcidPtr
   actor Writer #red
   actor Reader #green

   Writer -> AcidPtr: startCommit()
   AcidPtr <- WriteMutex: lock
   activate WriteMutex #red
   AcidPtr -> Writer: copy to AcidCommitPtr
   activate Writer
   hnote over Writer #pink
     update AcidCommitPtr
   end hnote
   Reader -[#green]> AcidPtr: getPtr()
   AcidPtr <[#green]- AccessMutex: lock
   activate AccessMutex #green

   Writer -> AcidPtr: ~AcidCommitPtr()
   deactivate Writer
   AcidPtr -[#green]> Reader: copy const shared_ptr
   activate Reader
   note left
     Contention limited to duration
     of shared_ptr copy/reset.
     (internally defined)
   end note
   AcidPtr -[#green]> AccessMutex: unlock
   deactivate AccessMutex

   AcidPtr <- AccessMutex: lock
   activate AccessMutex #red
   hnote over Reader #lightgreen
     use shared copy
   end hnote
   AcidPtr -> AcidPtr: reset shared_ptr
   AcidPtr -> AccessMutex: unlock
   deactivate AccessMutex
   AcidPtr -> WriteMutex: unlock
   deactivate WriteMutex


.. uml::
   :align: center

   Title AcidPtr Writer/Writer Contention
   box "MemLock"
   participant "Access" as AccessMutex
   participant "Write" as WriteMutex
   end box
   participant AcidPtr
   actor Writer_A #red
   actor Writer_B #blue

   Writer_A -> AcidPtr: startCommit
   AcidPtr <- WriteMutex: lock
   activate WriteMutex #red
   AcidPtr -> Writer_A: copy to AcidCommitPtr
   activate Writer_A
   Writer_B -[#blue]> AcidPtr: startCommit
   hnote over Writer_A #pink
     update AcidCommitPtr
   end hnote
   note over Writer_A
     Contention for duration
     of AcidCommitPtr scope.
     (externally defined)
   end note
   Writer_A -> AcidPtr: ~AcidCommitPtr()
   deactivate Writer_A
   AcidPtr <- AccessMutex: lock
   activate AccessMutex #red
   AcidPtr -> AcidPtr: reset shared_ptr
   AcidPtr -> AccessMutex: unlock
   deactivate AccessMutex
   AcidPtr -> WriteMutex: unlock
   deactivate WriteMutex

   AcidPtr <[#blue]- WriteMutex: lock
   activate WriteMutex #blue
   AcidPtr -[#blue]> Writer_B: copy to AcidCommitPtr
   activate Writer_B
   hnote over Writer_B #lightblue
     update AcidCommitPtr
   end hnote
   Writer_B -[#blue]> AcidPtr: ~AcidCommitPtr()
   deactivate Writer_B
   deactivate AccessMutex

   AcidPtr <[#blue]- AccessMutex: lock
   activate AccessMutex #blue
   AcidPtr -[#blue]> AcidPtr: reset shared_ptr
   AcidPtr -[#blue]> AccessMutex: unlock
   deactivate AccessMutex
   AcidPtr -[#blue]> WriteMutex: unlock
   deactivate WriteMutex

Reference
+++++++++


.. class:: template<typename T> AcidPtr

   .. function:: template<typename T> const std::shared_ptr<const T> getPtr() const

.. class:: template<typename T> AcidCommitPtr

   .. function:: template<> ~AcidCommitPtr()

   .. function:: template<T> AcidCommitPtr<T> startCommit()

   .. function:: template<> void abort()

