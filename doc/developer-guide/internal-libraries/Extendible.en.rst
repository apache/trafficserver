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

Extendible
**********

Synopsis
++++++++

.. code-block:: cpp

   #include "ts/Extendible.h"

Allow code (and Plugins) to declare member variables of a class during system init. Like metadata, but allocated and accessed more efficiently, and staticly.

Provide API for atomic and thread safe field access, to reduce the complexity and lock contention of it's container and user code.

Description
+++++++++++

A data class that inherits from Extendible, uses a CRTP (Curiously Recursive Template Pattern) so that it's static Schema instance is unique from other Extendible types.

.. code-block:: cpp

   class ExtendibleExample : Extendible<ExtendibleExample> {
      int real_member = 0;
   }

Structures
----------
* FieldId<AccessType_, FieldType> - the handle used to access a field. These are templated to prevent human error, and branching logic.
* FieldId_c - the handle used to access a field through C API. Human error not allowed by convention.
* FieldSchema - stores attributes, constructor and destructor of a field.
* Schema - manages fields and memory layout of an Extendible type. 
* Extendible<DerivedType> - allocates block of memory, uses FieldId or schema to access slices of memory.


During system init, code and plugins can add fields to the Extendible's schema. This will update the `Memory Layout`_ of the schema, and the memory offsets of all fields. The schema does not know the FieldType, but it stores the byte size and creates lambdas of the type's constructor and destructor. And to avoid corruption, the code asserts that no instances are in use when adding fields. 

.. code-block:: cpp

   ExtendibleExample:FieldId<ATOMIC,int> fld_my_int;
   ...
   void PluginInit() {
     fld_my_int = ExtendibleExample::schema.addField("my_plugin_int");
   }


When an Extendible derived class is instantiated, new() will allocate a block of memory for the derived class and all added fields. There is zero memory overhead per instance, unless using COPYSWAP_ field access type.

.. code-block:: cpp

   ExtendibleExample* alloc_example() {
     return new ExtendibleExample();
   }

Memory Layout
-------------
Memory is always arranged in the following order:

#. Derived members (+padding align next field)
#. Largest Field
#. Smallest Field
#. Packed Bits

+-------------------------------------------+
|Extendible::new()                          |
+-----------------+-------------------------+
|ExtendibleExample|Fields                   |
+-----------------+-------------------------+
|int real_member  |atomic<int> my_plugin_int|
+-----------------+-------------------------+

Extendible was written for efficient thread safety. When a field is added to the Extendible type, data type and access type part of the addField template, resulting in a strongly typed FieldId, and then will be implicitly enforced through every access of that field. 

.. code-block:: cpp

   int read_my_int(ExtendibleExample &ext) {
     return ext.get(fld_my_int);
   }
   void write_my_int(ExtendibleExample &ext, int val) {
     ext.get(fld_my_int) = val;
   }

   // Common user errors
   bool type_mismatch(ExtendibleExample &ext) {
     return ext.writeBit(fld_my_int); // Compile error: writeBit(field<BIT,bool>): field is not of type FieldId<BIT,bool>
   }
   int field_mismatch(ExtendibleOtherType &ext) {
     return ext.get(fld_my_int); // Compile error: fld_my_int is not of type ExtendibleOtherType::FieldId
   }


Field Access Types
------------------
.. _AccessType:

=========   =======================================   ============================================================   ========================================   =====================================
Enums       Allocates                                 API                                                            Pros                                       Cons  
=========   =======================================   ============================================================   ========================================   =====================================
ATOMIC      std::atomic<FieldType>                    :code:`get(FieldId)`                                           Leverages std::atomic<> API. No Locking.   Only works on small data types.
BIT         1 bit from packed bits                    :code:`get(FieldId), readBit(FieldId), writeBit(FieldId)`      Memory efficient.                          Cannot return reference. 
CONST       FieldType                                 :code:`get(FieldId), writeConst(FieldId)`                      Direct reference. Fast.                    No thread protection.
COPYSWAP_   shared_ptr<FieldType> = new FieldType()   :code:`get(FieldId), writeCopySwap(FieldId)`                   Avoid skew in non-atomic structures.       Non-contiguous memory allocations. Uses locking.
C_API       a number of bytes                         :code:`get()`                                                Can use in C.                              No thread protection.
=========   =======================================   ============================================================   ========================================   =====================================

:code:`operator[](FieldId)` has been overridden to call :code:`get(FieldId)`.

Unfortunately our data is not "one type fits all". I expect that most config values will be stored as CONST, most states values will be ATOMIC or BIT, while vectored results will be COPYSWAP.
