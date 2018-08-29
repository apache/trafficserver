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

.. |Extendible| replace:: :class:`Extendible`
.. |FieldId| replace:: :class:`~Extendible::FieldId`
.. |FieldId_C| replace:: :type:`FieldId_C`
.. |FieldSchema| replace:: :class:`~Extendible::FieldSchema`
.. |Schema| replace:: :class:`~Extendible::Schema`

.. |ATOMIC| replace:: :enumerator:`~AccessEnum::ATOMIC`
.. |BIT| replace:: :enumerator:`~AccessEnum::BIT`
.. |ACIDPTR| replace:: :enumerator:`~AccessEnum::ACIDPTR`
.. |STATIC| replace:: :enumerator:`~AccessEnum::STATIC`
.. |DIRECT| replace:: :enumerator:`~AccessEnum::DIRECT`
.. |C_API| replace:: :enumerator:`~AccessEnum::C_API`

.. _Extendible:

Extendible
**********

Synopsis
++++++++

.. code-block:: cpp

   #include "ts/Extendible.h"

|Extendible| allows Plugins to append additional storage to Core data structures.

Use Case:

TSCore

  Defines class ``Host`` as |Extendible|

TSPlugin ``HealthStatus``

  Appends field ``"down reason code"`` to ``Host``
  Now the plugin does not need to create completely new lookup table and all implementation that comes with it.


Description
+++++++++++

A data class that inherits from Extendible, uses a CRTP (Curiously Recurring Template Pattern) so that its static |Schema| instance is unique among other |Extendible| types.

.. code-block:: cpp

   class ExtendibleExample : Extendible<ExtendibleExample> {
      int real_member = 0;
   }


During system init, code and plugins can add fields to the |Extendible|'s schema. This will update the `Memory Layout`_ of the schema, and the memory offsets of all fields. The schema does not know the field's type, but it stores the byte size and creates lambdas of the type's constructor and destructor. And to avoid corruption, the code asserts that no instances are in use when adding fields.

.. code-block:: cpp

   ExtendibleExample::FieldId<ATOMIC,int> fld_my_int;

   void PluginInit() {
     fld_my_int = ExtendibleExample::schema.addField("my_plugin_int");
   }


When an |Extendible| derived class is instantiated, :code:`new()` will allocate a block of memory for the derived class and all added fields. There is zero memory overhead per instance, unless using :enumerator:`~AccessEnum::ACIDPTR` field access type.

.. code-block:: cpp

   ExtendibleExample* alloc_example() {
     return new ExtendibleExample();
   }

Memory Layout
-------------
One block of memory is allocated per |Extendible|, which include all member variables and appended fields.
Within the block, memory is arranged in the following order:

#. Derived members (+padding align next field)
#. Fields (largest to smallest)
#. Packed Bits

Strongly Typed Fields
---------------------
:class:`template<AccessEnum FieldAccess_e, typename T> Extendible::FieldId`

|FieldId| is a templated ``T`` reference. One benefit is that all type casting is internal to the |Extendible|,
which simplifies the code using it. Also this provides compile errors for common misuses and type mismatches.

.. code-block:: cpp

   // Core code
   class Food : Extendible<Food> {}
   class Car : Extendible<Car> {}

   // Example Plugin
   Food::FieldId<STATIC,float> fld_food_weight;
   Food::FieldId<STATIC,time_t> fld_expr_date;
   Car::FieldId<STATIC,float> fld_max_speed;
   Car::FieldId<STATIC,float> fld_car_weight;

   PluginInit() {
      Food.schema.addField(fld_food_weight, "weight");
      Food.schema.addField(fld_expr_date,"expire date");
      Car.schema.addField(fld_max_speed,"max_speed");
      Car.schema.addField(fld_car_weight,"weight"); // 'weight' is unique within 'Car'
   }

   PluginFunc() {
      Food banana;
      Car camry;

      // Common user errors

      float expire_date = banana.get(fld_expr_date);
      //^^^                                              Compile error: cannot convert time_t to float
      float speed = banana.get(fld_max_speed);
      //                       ^^^^^^^^^^^^^             Compile error: fld_max_speed is not of type Extendible<Food>::FieldId
      float weight = camry.get(fld_food_weight);
      //                       ^^^^^^^^^^^^^^^           Compile error: fld_food_weight is not of type Extendible<Car>::FieldId
   }

Field Access Types
------------------

.. _AccessType:

..
  Currently Sphinx will link to the first overloaded version of the method /
  function. (As of Sphinx 1.7.6)

.. |GET| replace:: :func:`~Extendible::get`
.. |READBIT| replace:: :func:`~Extendible::readBit`
.. |WRITEBIT| replace:: :func:`~Extendible::writeBit`
.. |WRITEACIDPTR| replace:: :func:`~Extendible::writeAcidPtr`
.. |INIT| replace:: :func:`~Extendible::init`

================   =================================   ================================================================   =================================================   ================================================
Enums              Allocates                                           API                                                            Pros                                               Cons
================   =================================   ================================================================   =================================================   ================================================
|ATOMIC|           ``std::atomic<T>``                  |GET|                                                              Leverages ``std::atomic`` API. No Locking.          Only works on small data types.
|BIT|              1 bit from packed bits              |GET|, |READBIT|, |WRITEBIT|                                       Memory efficient.                                   Cannot return reference.
|ACIDPTR|          ``std::make_shared<T>``             |GET|, |WRITEACIDPTR|                                              Avoid skew in non-atomic structures.                Non-contiguous memory allocations. Uses locking.
|STATIC|           ``T``                               |GET|, |INIT|                                                      Const reference. Fast. Type safe.                   No concurrency protection.
|DIRECT|           ``T``                               |GET|                                                              Direct reference. Fast. Type safe.                  No concurrency protection.
|C_API|            a number of bytes                   |GET|                                                              Can use in C.                                       No concurrency protection. Not type safe.
================   =================================   ================================================================   =================================================   ================================================

:code:`operator[](FieldId)` has been overridden to call :code:`get(FieldId)` for all access types.

Unfortunately our data is not "one type fits all". I expect that most config values will be stored as :enumerator:`AccessEnum::STATIC`, most states values will be :enumerator:`AccessEnum::ATOMIC` or :enumerator:`AccessEnum::BIT`, while vectored results will be :enumerator:`AccessEnum::ACIDPTR`.

Reference
+++++++++

.. enum:: AccessEnum

   .. enumerator:: ATOMIC

      Represents atomic field reference (read, write or other atomic operation).

   .. enumerator:: BIT

      Represents compressed boolean fields.

   .. enumerator:: STATIC

      Represents immutable field, value is not expected to change, no internal thread safety.

   .. enumerator:: ACIDPTR

      Represents a pointer promising Atomicity, Consistency, Isolation, Durability

      .. seealso:: :ref:`AcidPtr`

   .. enumerator:: DIRECT

      Represents a mutable field with no internal thread safety.

   .. enumerator:: C_API

      Represents C-Style pointer access.

.. class:: template<typename Derived_t> Extendible

   Allocates block of memory, uses |FieldId| or schema to access slices of memory.

   :tparam Derived_t: The class that you want to extend at runtime.

   .. class:: template<AccessEnum FieldAccess_e, typename T> FieldId

      The handle used to access a field. These are templated to prevent human error, and branching logic.

      :tparam FieldAccess_e: The type of access to the field.
      :tparam T: The type of the field.

   .. class:: FieldSchema

      Stores attributes, constructor and destructor of a field.

   .. class:: Schema

      Manages fields and memory layout of an |Extendible| type.

      .. function:: template<AccessEnum FieldAccess_e, typename T> bool addField(FieldId<FieldAccess_e, T> & field_id, std::string const & field_name)

         Add a new field to this record type.

   .. member:: static Schema schema

      one schema instance per |Extendible| to define contained fields

   .. function:: template<AccessEnum FieldAccess_e, typename T> std::atomic<T> & get(FieldId<AccessEnum::ATOMIC, T> const & field)

   .. type::  BitFieldId = FieldId<AccessEnum::BIT, bool>

   .. function:: bool const get(BitFieldId field) const

   .. function:: bool const readBit(BitFieldId field) const

   .. function:: void writeBit(BitFieldId field, bool const val)

   .. function:: template <AccessEnum FieldAccess_e, typename T> T const & get(FieldId<AccessEnum::STATIC, T> field) const

   .. function:: template <AccessEnum FieldAccess_e, typename T> T &init(FieldId<AccessEnum::STATIC, T> field)

   .. function:: template <AccessEnum FieldAccess_e, typename T> std::shared_ptr<const T> get(FieldId<AccessEnum::ACIDPTR, T> field) const

   .. function:: template <AccessEnum FieldAccess_e, typename T> AcidCommitPtr<T> writeAcidPtr(FieldId<AccessEnum::ACIDPTR, T> field)

   .. function:: void * get(FieldId_C & field)

   .. function template <AccessEnum FieldAccess_e, typename T> auto operator[](FieldId<FieldAccess_e, T> field)

      :return: the result of :function:`get`


.. type:: const void* FieldId_C

   The handle used to access a field through C API. Human error not allowed by convention.

