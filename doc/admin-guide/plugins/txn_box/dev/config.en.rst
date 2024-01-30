.. Copyright 2022, Alan M. Carroll
   SPDX-License-Identifier: Apache-2.0
.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _imp_config:

Configuration
*************

Configuration state is managed by the :txb:`Config` class. An instance of this represents a
configuration of |TxB|. The instance is similar to the global data for a process - data that is
tied to a configuration in general and not to any particular configuration element is stored here.
An instance of :txb:`Config` acts as the "context" for parsing configuration.

Directive Handling
==================

Directives interact heavily with this class. The :cpp:class::`Directive::FactoryInfo` contains the
static information about a directive. In addition, it has an "index" field which is used to track
which directives are used in a configuration.

To make a directive available it must register by using the :txb:`Config::define` method.
