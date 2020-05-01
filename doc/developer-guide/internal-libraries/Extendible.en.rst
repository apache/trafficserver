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
.. |FieldId| replace:: :class:`template<typename Derived_t, typename Field_t> FieldId`
.. |ExtFieldContext| replace:: :type:`ExtFieldContext`
.. |FieldDesc| replace:: :class:`FieldDesc`
.. |Schema| replace:: :class:`Schema`


.. _Extendible:

Extendible
**********

Synopsis
++++++++

.. code-block:: cpp

   #include "tscore/Extendible.h"

|Extendible| allows Plugins to append additional storage to Core data structures and interface like a map or dictionary. Each additional field is declared during init, so that a custom allocator can malloc one block for the Datatype and its extended fields.
In C++, the |FieldId| are strongly typed field handles, which allows you to use Extendible in multiple inheritance, and at many levels of inheritance hierarchy, with compile time type safety.

Use Case:

  TSCore
    Defines class ``Host`` as |Extendible|

  TSPlugin ``HealthStatus``
    Extend the ``Host`` datatype with field ``<int> down reason code``. API returns a handle.

    Use :arg:`Data` and :arg:`handle` to read & write fields.


Description
+++++++++++

A data class that inherits from Extendible, uses a CRTP (Curiously Recurring Template Pattern)
so that its static |Schema| instance is unique among other |Extendible| types. Thus all instances of the type implicitly know memory layout of the fields.

.. code-block:: cpp

   class ExtendibleExample : public ext::Extendible<ExtendibleExample> {
      int real_member = 0;
   }

The documentation and code refers to the `Derived` type as the class that is inheriting from an |Extendible|.

During system init, code and plugins add fields to the |Extendible|'s schema. This will update the `Memory Layout`_ of the schema,
and the memory offsets of all fields. The schema does not know the field's type, but it stores the byte size and creates std::functions of
the type's constructor, destructor, and serializer. And to avoid corruption, the code asserts that no instances are in use when adding fields.

.. code-block:: cpp

   ext::FieldId<ExtendibleExample,int> fld_my_int;

   void PluginInit() {
     fld_my_int = ext::fieldAdd(fld_my_int, "my_plugin_int");
   }


When an derived class is instantiated, :func:`template<> create()` will allocate a block of memory for the derived class and all added
fields. The only memory overhead per instance is an uint16 used as a offset to the start of the extendible block. Then the constructor of the class
is called, followed by the constructors of each extendible field.

.. code-block:: cpp

   ExtendibleExample* alloc_example() {
     return ext::create<ExtendibleExample>();
   }

Memory Layout
-------------
One block of memory is allocated per |Extendible|, which include all member variables and extended fields.
Within the block, memory is arranged in the following order:

#. Derived members (+padding align next field)
#. Fields (largest to smallest)
#. Packed Bits

When using inheritance, all base cases arranged from most super to most derived,
then all |Extendible| blocks are arranged from most super to most derived.
If the fields are aligned, padding will be inserted where needed.

Strongly Typed Fields
---------------------
:class:`template<typename Derived_t, typename Field_t> FieldId`

|FieldId| is a templated ``Field_t`` reference. One benefit is that all type casting is internal to the |Extendible|,
which simplifies the code using it. Also this provides compile errors for common misuses and type mismatches.

.. code-block:: cpp

   // Core code
   class Food : public ext::Extendible<Food> {};
   class Car : public ext::Extendible<Car> {};

   // Example Plugin
   ext::FieldId<Food,float> fld_food_weight;
   ext::FieldId<Food,time_t> fld_expr_date;
   ext::FieldId<Car,float> fld_max_speed;
   ext::FieldId<Car,float> fld_car_weight;

   PluginInit() {
      ext::addField(fld_food_weight, "weight");
      ext::addField(fld_expr_date,"expire date");
      ext::addField(fld_max_speed,"max_speed");
      ext::addField(fld_car_weight,"weight"); // 'weight' is unique within 'Car'
   }

   PluginFunc() {
      Food *banana = ext::create<Food>();
      Car *camry = ext::create<Car>();

      // Common user errors

      float expire_date = ext::get(banana,fld_expr_date);
      //^^^
      // Compile error: cannot convert time_t to float

      float speed = ext::get(banana,fld_max_speed);
      //                            ^^^^^^^^^^^^^
      // Compile error: Cannot convert banana to type Extendible<Car>

      float weight = ext::get(camry,fld_food_weight);
      //                            ^^^^^^^^^^^^^^^
      // Compile error: Cannot convert camry to type Extendible<Food>, even though Car and Food each have a 'weight' field, the FieldId is strongly typed.

   }

   // Inheritance Example
   class Fruit : Food, Extendible<Fruit> {
      using super_type = Food;
   };

   ext::FieldId<Fruit,has_seeds> fld_has_seeds;

   Fruit.schema.addField(fld_has_seeds, "has_seeds");

   Fruit mango = ext::create<Fruit>();

   ext::set(mango, fld_has_seeds) = true;         // converts mango to Extendible<Fruit>
   ext::set(mango, fld_food_weight) = 2;          // converts mango to Extendible<Food>
   ext::set(mango, fld_max_speed) = 9;
   //              ^^^^^^^^^^^^^
   // Compile error: Cannot convert mango to type Extendible<Car>


Inheritance
-----------

   Unfortunately it is non-trivial handle multiple |Extendible| super types in the same inheritance tree.
   :func:`template<> create()` handles allocation and initialization of the entire `Derived` class, but it is dependent on each class defining :code:`using super_type = *some_super_class*;` so that it recurse through the classes.

.. code-block:: cpp

   struct A : public Extendible<A> {
      uint16_t a = {1};
   };

   struct B : public A {
      using super_type = A;
      uint16_t b       = {2};
   };

   struct C : public B, public Extendible<C> {
      using super_type = B;
      uint16_t c       = {3};
   };

   ext::FieldId<A, atomic<uint16_t>> ext_a_1;
   ext::FieldId<C, uint16_t> ext_c_1;

   C &x = *(ext::create<C>());
   ext::viewFormat(x);

:func:`viewFormat` prints a diagram of the position and size of bytes used within the allocated
memory.

.. code-block:: text

   1A | EXT  | 2b | ##________##__ |
   1A | BASE | 2b | __##__________ |
   1B | BASE | 2b | ____##________ |
   1C | EXT  | 2b | ______##____## |
   1C | BASE | 2b | ________##____ |


See :ts:git:`src/tscore/unit_tests/test_Extendible.cc` for more examples.

Reference
+++++++++

Namespace `ext`

.. class:: template<typename Derived_t, typename Field_t> FieldId

   The handle used to access a field. These are templated to prevent human error, and branching logic.

   :tparam Derived_t: The class that you want to extend at runtime.
   :tparam Field_t: The type of the field.

.. type:: const void* ExtFieldContext

   The handle used to access a field through C API. Human error not allowed by convention.


.. class:: template<typename Derived_t> Extendible

   Allocates block of memory, uses |FieldId| and |Schema| to access slices of memory.

   :tparam Derived_t: The class that you want to extend at runtime.

   .. member:: static Schema  schema

      one schema instance per |Extendible| to define contained |FieldDesc|

.. function:: template<typename Derived_t> Extendible* create()

   To be used in place of `new Derived_t()`.
   Allocate a block of memory. Construct the base data.
   Recursively construct and initialize `Derived_t::super_type` and its |Extendible| classes.

   :tparam Derived_t: The Derived class to allocate.

.. function:: template<typename Derived_t, typename Field_t> \
   bool fieldAdd(FieldId\<Derived_t, Field_t> & field_id, std::string const & field_name)

   Declare a new |FieldId| for Derived_t.

   :tparam Derived_t: The class that uses this field.
   :tparam Field_t: The type of the field.

.. function:: template<typename Derived_t, typename Field_t> \
   bool fieldFind(FieldId\<Derived_t, Field_t> & field_id, std::string const & field_name)

   Find an existing |FieldId| for Derived_t.

   :tparam Derived_t: The class that uses this field.
   :tparam Field_t: The type of the field.

.. function:: template<typename T, typename Derived_t,  typename Field_t> \
   auto const get(T const &, FieldId\<Derived_t,Field_t>)

   Returns T const& value from the field stored in the |Extendible| allocation.

   :tparam T: The class passed in.
   :tparam Derived_t: The class that uses this field.
   :tparam Field_t: The type of the field.

.. function:: template<typename T, typename Derived_t, typename Field_t> \
   auto set(T &, FieldId\<Derived_t,Field_t>)

   Returns T & value from the field stored in |Extendible| allocation.

   :tparam T: The class passed in.
   :tparam Derived_t: The class that uses this field.
   :tparam Field_t: The type of the field.

.. function:: template<typename Derived_t> size_t sizeOf()

   Recurse through super classes and sum memory needed for allocation.

   Depends on usage of `super_type` in each class.

   :tparam Derived_t: The class to measure.

.. function:: template<typename Derived_t> void viewFormat()

   Recurse through super classes and prints chart of bytes used within the allocation.

   Depends on usage of `super_type` in each class.

   :tparam Derived_t: The class to analyze.

.. function:: template <typename T> std::string toString(T const &t)

   Convert all extendible fields to std::strings (in a YAML-like format) using the :func:`serializeField()`

   :tparam Derived_t: The class to convert to string.

.. function:: template <typename Field_t> void serializeField(std::ostream &os, Field_t const &f)

   Converts a single field into a std::string (in a YAML-like format).

   Specialize this template or overload the `operator<<` for your field to convert properly.

   This is very useful when debugging.

   :tparam Derived_t: The field data type.

Namespace `ext::details`


.. class:: FieldDesc

   Defines a span of memory within the allocation, and holds the constructor, destructor and serializer as std::functions.

   Effectively the type-erased version of |FieldId|.

.. class:: Schema

   Manages a memory layout through a map of |FieldDesc|.



