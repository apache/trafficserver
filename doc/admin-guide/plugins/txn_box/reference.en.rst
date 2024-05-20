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

   Copyright 2022, Alan M. Carroll


.. include:: ../../../common.defs
.. include:: txnbox_common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _reference:

********
Glossary
********

.. glossary::
   :sorted:

   transaction (txn_box)
      A :term:`request` and the corresponding :term:`response`.

   request
      A request from a client to an :term:`upstream`.

   response
      The response or result of a :term:`request`.

   upstream
      A node which is the destination of a network connection.

   feature
      Data which is derived from the transaction and treated as a unit.

   extraction
      Using data from the transaction to create a :term:`feature`.

   extractor
      A mechanism which can performat :term:`extraction`.

   feature expression
      A expression that describes how to extract a fature.

   modifier
      An object that modifies a feature in a feature expression.

   directive
      An action to perform.

   selection
      Making a choice based on a :term:`feature`.

   comparison
      An operator that compares local data to a feature and indicates if the data matches the feature.

   load time
      The period of time during which a configuration is being loaded.

   run time
      When the configuration is being applied to network traffic.

   hook
      A |TS| hook, a particular point in the processing of network traffic.

   argument
      A value provided to a :term:`directive`, :term:`extractor`,:term:`modifier`, or :term:`comparison`.
      An argument affects the behavior in various ways, specified by the particular element. Most
      commonly it sets either the scope of the element or serves as a list of flags. Arguments are
      always a string or a list of strings.

   scope
      A subtree of the YAML configuration where special data is available.

   active
      A scope that is currently accessible.
