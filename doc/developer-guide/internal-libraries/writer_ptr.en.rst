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

write_ptr
**********

Synopsis
++++++++

.. code-block:: cpp

   #include "ts/write_ptr.h"

Provides transparent interface for "copy on write" and "commit when done" in a familiar unique_ptr style.

Description
+++++++++++

* On construction, duplicate values from a shared_ptr to a unique_ptr. (aka copy read to write memory)
* On destruction, move value ptr to shared_ptr. (aka move write ptr to read ptr) 

The write_ptr executes this transparent to the writer code. It copies the data on construction, and finalizes on destruction. A MemLock is used to allow exclusive read and write access, however the access is made to as fast as possible.

Implemented for use COPYSWAP_ interface in Extendible_. But could be used elsewhere without modification.

COPYSWAP
--------
Fields using COPYSWAP are allocated as std::shared_ptr<FieldType> pointing to new FieldType(). When write access is requested, the field is copy constructed to a create a write_ptr. 

.. uml::
   :align: center

   title Read while Write

   actor ExistingReaders
   actor NewReaders
   actor Writer
   storage Extendible
   card "x=foo" as Data
   card "x=bar" as DataPrime

   ExistingReaders --> Data : read only
   Extendible -> Data : shared_ptr
   NewReaders --> Extendible : read only
   Writer --> DataPrime : read/write
   Data .> DataPrime : copy

When the writer is done, the field ptr is updated to point at the written copy, so that future read request will use it. Existing reader will continue to use the old data. 

.. uml::
   :align: center

   title Write Finalize

   actor ExistingReaders
   actor NewReaders
   storage Extendible
   card "x=foo" as Data
   card "x=bar" as DataPrime

   ExistingReaders --> Data : read only
   Extendible -> DataPrime : shared_ptr
   NewReaders --> Extendible : read only


.. uml::
   :align: center
   
   title CopySwap Reader/Reader Contention
   box "MemLock"
   participant "Access" as AccessMutex
   end box
   participant "Extendible\nCopySwap Field" as Extendible
   actor Reader_A #green
   actor Reader_B #red
   Reader_A -[#green]> Extendible: get(field)
   Extendible <[#green]- AccessMutex: lock
   activate AccessMutex #green
   Reader_B -> Extendible: get(field)
   Extendible -[#green]> Reader_A: copy const shared_ptr
   activate Reader_A
   note left
     Contention limited to
     duration of shared_ptr copy.
     (internally defined)
   end note
   Extendible -[#green]> AccessMutex: unlock
   deactivate AccessMutex
   Extendible <- AccessMutex: lock
   activate AccessMutex #red
   Extendible -> Reader_B: copy const shared_ptr
   activate Reader_B
   Extendible -> AccessMutex: unlock
   deactivate AccessMutex


.. uml::
   :align: center

   Title CopySwap Writer/Reader Contention
   box "MemLock"
   participant "Access" as AccessMutex
   participant "Write" as WriteMutex
   end box
   participant "Extendible\nCopySwap Field" as Extendible
   actor Writer #red
   actor Reader #green

   Writer -> Extendible: write(field)
   Extendible <- WriteMutex: lock
   activate WriteMutex #red
   Extendible -> Writer: copy to write_ptr
   activate Writer
   hnote over Writer #pink
     update write_ptr
   end hnote
   Reader -[#green]> Extendible: get(field)
   Extendible <[#green]- AccessMutex: lock
   activate AccessMutex #green

   Writer -> Extendible: ~write_ptr() 
   deactivate Writer
   Extendible -[#green]> Reader: copy const shared_ptr
   activate Reader
   note left
     Contention limited to duration 
     of shared_ptr copy/reset.
     (internally defined)
   end note
   Extendible -[#green]> AccessMutex: unlock
   deactivate AccessMutex

   Extendible <- AccessMutex: lock
   activate AccessMutex #red
   hnote over Reader #lightgreen
     use shared copy
   end hnote
   Extendible -> Extendible: reset shared_ptr
   Extendible -> AccessMutex: unlock
   deactivate AccessMutex
   Extendible -> WriteMutex: unlock
   deactivate WriteMutex


.. uml::
   :align: center

   Title CopySwap Writer/Writer Contention
   box "MemLock"
   participant "Access" as AccessMutex
   participant "Write" as WriteMutex
   end box
   participant "Extendible\nCopySwap Field" as Extendible
   actor Writer_A #red
   actor Writer_B #blue

   Writer_A -> Extendible: write(field)
   Extendible <- WriteMutex: lock
   activate WriteMutex #red
   Extendible -> Writer_A: copy to write_ptr
   activate Writer_A
   hnote over Writer_A #pink
     update write_ptr
   end hnote
   Writer_B -[#blue]> Extendible: write(field)

   Writer_A -> Extendible: ~write_ptr()
   deactivate Writer_A
   Extendible <- AccessMutex: lock
   activate AccessMutex #red
   Extendible -> Extendible: reset shared_ptr
   Extendible -> AccessMutex: unlock
   deactivate AccessMutex
   Extendible -> WriteMutex: unlock
   deactivate WriteMutex

   note over WriteMutex
     Contention for duration 
     of write_ptr scope.
     (externally defined)
   end note

   Extendible <[#blue]- WriteMutex: lock
   activate WriteMutex #blue
   Extendible -[#blue]> Writer_B: copy to write_ptr
   activate Writer_B
   hnote over Writer_B #lightblue
     update write_ptr
   end hnote
   Writer_B -[#blue]> Extendible: ~write_ptr()
   deactivate Writer_B
   deactivate AccessMutex

   Extendible <[#blue]- AccessMutex: lock
   activate AccessMutex #blue
   Extendible -[#blue]> Extendible: reset shared_ptr
   Extendible -[#blue]> AccessMutex: unlock
   deactivate AccessMutex
   Extendible -[#blue]> WriteMutex: unlock
   deactivate WriteMutex
