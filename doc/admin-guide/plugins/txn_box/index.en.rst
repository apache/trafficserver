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

   Copyright 2020, Verizon Media

.. include:: ../../../common.defs
.. include:: txnbox_common.defs

***************
Transaction Box
***************

.. important::

   This is an experimental plugin and it should be build by using `-DBUILD_EXPERIMENTAL_PLUGINS=YES`.


Transaction Box, or |TxB|, is an Apache Traffic Server plugin to manipulate
:term:`transaction`\s. The functionality is based on requests I have received over the years from
users and admnistrators for |TS|. The primary points of interest are

*  YAML based configuration.
*  Smooth interaction between global and remap hooks.
*  Consistent access to data, a single way to access data usable in all circumstances.

|TxB| is designed as a very general plugin which can replace a number of other plugins. It is also
intended, in the long run, to replace "remap.config".

.. toctree::
   :maxdepth: 2

   txn_box.en
   building.en
   install.en
   expr.en.rst
   directive.en
   selection.en
   guide.en
   examples.en
   arch.en
   user/ExtractorReference.en
   user/DirectiveReference.en
   user/ComparisonReference.en
   user/ModifierReference.en
   future.en
   misc.en
   dev/dev-guide.en
   dev/acceleration.en

Reference
*********

.. toctree::
   :maxdepth: 1

   reference.en
