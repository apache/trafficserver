.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: txnbox_common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _arch:

************
Architecture
************

Extending
*********

|TxB| is intended to be easily extendible with regard to adding new directives, comparisons,
and extractors. For each such extension there are two major phases that must be supported,
loading and invoking.

Loading is parsing data in the configuration file. Invoking happens during transaction processing.

Extractor
=========

An extractor gathers data and provides it to the plugin. While this is usually data in a transaction
that is not required. The gathered data is called a :term:`feature`. Every extractor must be able to
provide its feature in string format. It can also provide the feature in one of a few predefined
feature types -

*  :code:`INTEGER`, a signed integral value.

*  :code:`BOOL`, a boolean value that is the equivalent of :code:`true` and :code:`false`.

*  :code:`IP_ADDR`, an IP address.

Other feature types may be supported in the future.

An extractor must inherit from `Extractor`.

Comparison
==========

A comparison compares fixed data to a feature and potentially provides some side effects on a
successful match. At a minimum, the comparison class implementation must provide a function operator
overload which performs the run time comparison with the feature. This can will be one or more of
several overloads. The precise methods depend on which feature types are supported.

   *  String - feature argument type is :code:`swoc::TextView`.
   *  Boolean - feature argument type is :code:`bool`.
   *  Number - feature argument type is :code:`intmax_t`.
   *  IP Address - feature argument type is :code:`swoc::IPAddr`.

This is the only required elements, but there are several others which are very convienient and
should be added unless there is a reason to not do so.

*  A :code:`static` :code:`const` :code:`std::string` which contains the name of the comparison.

*  A :code:`static` :code:`const` `FeatureMask` which contains the valid feature types for the comparison.

*  A :code:`static` :code:`load` method which constructs an instance from YAML configuration.

*  Storage for the comparison fixed data.

The former could be hardwired wherever it is used, but having a single copy is better. The
:code:`load` method could be done with a free function or a lambda but again it is better to
consolidate this in the class itself.

To illustrate, consider the :code:`suffix` comparison. The bare minimum class definition is::

   class Cmp_Suffix : public Comparison {
   public:
      bool operator() (Context& ctx, TextView& text) const override;
   };

Because :code:`Cmp_Suffix` class is only valid for string features, it provides only the
:code:`TextView` overload. The more complete declaration would be::

   class Cmp_Suffix : public Comparison {
   public:
      static const std::string KEY;
      static const FeatureMask TYPES;

      bool operator() (Context& ctx, TextView& text) const override;

      static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

   protected:
      Extractor::Expr _value; ///< Suffix value to compare.
   };

The :code:`is_valid_for` method is defined to return :code:`true` only for the :code:`VIEW` feature
type, which is consistent with providing only the :code:`swoc::TextView` function operator
overload. That method calls :code:`TextView::ends_with` to do the check.

In order to be available to the configuration, the comparison must be passed, along with its name,
to the `Comparison::define` method. There are various initialization mechanism, the one used
inside |TxB| looks like this::

   namespace {
   [[maybe_unused]] bool INITIALIZED = [] () -> bool {
      Comparison::define(Cmp_Suffix::KEY, Cmp_Suffix::TYPES, &Cmp_Suffix::load);

      return true;
   } ();

More detail is available at `Cmp_Suffix`.
