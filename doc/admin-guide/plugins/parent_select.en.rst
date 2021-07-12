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

.. _admin-plugins-parent-select:

Parent Select Plugin
********************

This remap plugin allows selection of parent proxies or origins during
requests. This provides the same functionality as the core :file:`parent.config`
and :file:`strategies.yaml` config files via a plugin.

Purpose
=======

The purpose of this plugin is to provide a base for creating custom
parent selection plugins, as well as to eventually replace the core
parent and strategy logic so all nontrivial parent selection will
be done via plugins.

Installation
============

This plugin is still experimental, but is included with |TS| when you
build with the experimental plugins enabled via ``configure``.

Configuration
=============

This plugin only functions as a remap plugin, and is therefore
configured in :file:`remap.config`.

It requires two options: the strategies config file, and the name of the strategy.

For example, a remap.config line might look like:

.. code-block::

    map https://example.net/ https://example.net/ @plugin=parent_select.so @pparam=strategies.yaml @pparam=example-strategy

This means all remap rules can use the same ``strategies.yaml`` file containing multiple strategies, or each remap rule can have its own strategies file, whichever the operator prefers.


Strategies file
---------------

The ``strategies.yaml`` file is the same format as the core ``strategies.yaml`` config file.

See :doc:`../files/strategies.yaml.en`.
