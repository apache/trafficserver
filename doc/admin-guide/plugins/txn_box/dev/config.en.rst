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


.. include:: ../../../../common.defs
.. include:: ../txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _imp_config:

Configuration
*************

Configuration state is managed by the `Config` class. An instance of this represents a
configuration of |TxB|. The instance is similar to the global data for a process - data that is
tied to a configuration in general and not to any particular configuration element is stored here.
An instance of `Config` acts as the "context" for parsing configuration.

Directive Handling
==================

Directives interact heavily with this class. The :cpp:class::`Directive::FactoryInfo` contains the
static information about a directive. In addition, it has an "index" field which is used to track
which directives are used in a configuration.

To make a directive available it must register by using the `Config::define` method.
