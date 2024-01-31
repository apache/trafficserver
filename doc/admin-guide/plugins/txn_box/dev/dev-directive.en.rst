.. Copyright 2022, Alan M. Carroll
   SPDX-License-Identifier: Apache-2.0

.. include:: ../txnbox_common.defs

.. highlight:: cpp
.. default-domain:: txb

.. _dev-directive:

Directive Development
*********************

Directives are implemented by a class, which must be a subclass of `Directive`. Each use of
the directive in a configuraiton creates a new instance of the class.


Required Methods
================

Instance Loader
---------------

The first requirement for a directive implementation is creating the instance loader. This is a
functor to be invoked by the configuration loader when the directive is used to create the instance.
By convention this is named :code:`load`.

The signature is

.. cpp:function:: swoc::Rv<Directive::Handle> load(Config& cfg, CfgStaticData const * rtti, YAML::Node drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node key_value)

   :param cfg: Configuration instance.
   :param rtti: Directive static data.
   :param drtv_node: YAML node for the directive use.
   :param name: Name of the directive.
   :param arg: Argument to the directive.
   :param key_value: Value of :arg:`drtv_node`.

   This function is responsible for either creating a fully initialized instance of the directive class or
   an error report if that fails.

The configuration instance :arg:`cfg` is the configuration that is loading.

For each directive class, there is some configuration static data. This is passed via the
:arg:`rtti` parameter which points at an instance of `CfgStaticData`. This value will also be
available to the instance later via `Directive::_rtti`. This will be set by the configuration
loader, it does not need to be set by the instance loader. In some cases, however, it is needed by
the instance loader and because the instance has not yet been created it can't be loaded from the
directive instance.

A directive is always a map. :arg:`drtv_node` is the map node and :arg:`key_value` is the node for
the value of the key that determined which directive the map is. :arg:`name` is the name of the
directive as used to look up the instance loader and :arg:`arg` is the argument, if any. I.e. if the
directive key is "proxy-rsp-field<Best-Band>" then :arg:`name` will be "proxy-rsp-field" and
:arg:`arg` will be "Best-Band".

The instance loader is responsible for all directive parsing, including any nested keys and the
argument.

Optional Methods
================

Configuration Initializer
-------------------------

A directive can specify a static method to invoke the first time the directive is used in a configuration.

.. cpp:function:: swoc::Errata cfg_init(Config& cfg, CfgStaticData const* rtti)

   :param cfg: Configuration instance.
   :param rtti: Directive static data.

:arg:`cfg`is the configuration instance and :arg:`rtti` is the directive related static information.
The primary use of this is a data structure that is shared among instances. This can be constructed
in this method and stored in the :arg:`rtti` member :code:`_cfg_store`.
