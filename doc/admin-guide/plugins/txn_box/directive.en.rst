.. Copyright 2022, Alan M. Carroll
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../common.defs
.. include:: txnbox_common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _directive:

Directives
**********

Directives are implemented by subclassing `Directive`. The general convention is to prefix the
directive name with "Do\_" to form a class name.

Example
=======

note
++++

Consider a directive to log a "NOTE" in "diag.logs". Fundamentally this can be done by calling
the |TS| utility method `ts::Log_Note` and passing a string view. The directive is simply a
wrapper around this, to extact the view and then log it.

By convention the class is named ``Do_note`` and the declaration is

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-at: class Do_note
   :end-before: -- doc end Do_note

Each directive has a key that is the name of the directive, it is best to declare this as a
:code:`std::string`.

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-at: class Do_note
   :lines: 4-9
   :emphasize-lines: 2

:code:`inline` enables the member to be initialized in the class declaration. In this case the
directive is usable on any hook and so all of them are listed. The class `HookMask` is used to
hold a bit mask of the valid hooks. The utility function `MaskFor` can be used to create a bit
mask from hook enumeration values.

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-at: class Do_note
   :lines: 4-9
   :emphasize-lines: 4-6

The first requirement of the directive implementation is loading from the configuration to create an
instance of the directive class. The framework, when expecting a directive, checks if the node is an
object. If so, the keys in the object are checked for being a directive by matching the key name
with a table of directive names. If there is a match, the corresponding load functor is invoked.
Multiple arguments are passed to the functor.

:code:`Config& cfg`
   A reference to the configuration object, which contains the configuration loading context. See `Config`.

:code:`CfgStaticData`
   Every registered directive gets a config level block of information. This is a reference to that
   for the configuration being loaded. See `CfgStaticData`.

:code:`YAML::Node const& drtv_node`
   The directive map (YAML node). This is the YAML map that contains the directive key.

:code:`TextView const& name`
   The name of the directive. This is the same as the value in the directive table. In most cases it
   is irrelevant. In the cases of a group of similar directives, a single load functor could load
   all of them, distinguishing the exact case with this value.

:code:`TextView const& arg`
   A directive key can have an argument, which is additional text attached to the directive name
   with a period separator. Although implementationally arbitrary, the convention is the argument
   (if any) should be used to select the target of the directive if the value can't be known at
   compile time. Data used to perform the directive should be in the value of the directive key.

:code:`YAML::Node const& key_value`
   The value of the directive key. Note this can be any type of YAML data, including nested objects.
   This is not processed beyond being validated as valid YAML.

It is the responsibility of the load functor to do any further processing of the :arg:`key_value`
and construct an instance of the directive class using that information. If the functor is
implemented as a method it must be a static method as by definition there is no directive instance
yet.

Here is an example of using this directive.

.. code-block:: YAML
   :emphasize-lines: 2

   do:
   - note: "Something's happen here"

The "do" key contains a list of directives, each of which is an object. The first such object is the
``note`` object. It will be invoked with :arg:`drtv_node` being the object in the list for "do", while
:arg:`name` will be "note" and :arg:`key_value` the string ``"Something's happening here"``.

Loading implementation is straight forward. The value is expected to be a feature expression and all
that needs done is to store it in the instance for use at run time.

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-after: -- doc note::load
   :end-before: -- doc note::load

`Config::parse_expr` is used to parse the value as a feature expression. If not successful, the
lower level error is augmented with context information about the directive and returned. Otherwise
the parsed expression is stored in a newly created instance which is then returned.

When used at runtime, the :code:`invoke` method is called.

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-after: -- doc note::invoke
   :end-before: -- doc note::invoke

:arg:`ctx` is a per transaction context and serves as the root of accessible data structures. In
this case the parsed feature expression is passed to `Context::extract_view` to extract the
expression as a string, which is the logged.

redirect
++++++++

A more complex example would be the ``redirect`` directive.

.. code-block:: YAML

   redirect:
      location: "http://example.one"
      status: 302
      body: "Redirecting to {this::location} - please update your links."

For loading from configuration, :code:`key-value` is an object, with keys ``location``, ``status``,
and ``body``, which must be handled by the directive implementation. `FeatureGroup` is a
support class to support directives with multiple keys. It is not required but does provide much of
the boiler plate needed in this situation.

Some of the complexity stems from the fact that while the directive must be invoked on a relatively
early hook, the implementation must do some "fix ups" on a later hook and that information must be
cached between hooks. A structure is defined to hold this data

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-after: -- doc Do_redirect::CtxInfo
   :end-at: }

The significant problem is not storing this information, but finding it later. A pointer cannot be
stored in the instance, because there is only one per configuration instance which can be invoked in
multiple transactions simultaneously. The solution is to reserve the storage at the configuration
level which creates a fixed offset (per configuration) for storage in the per transaction context.
This is a (somewhat) expensive resource as such storage is reserved on every transaction even if not
used. The context storage reservation can vary between configuration instances (because the set of
directives reserving storage can vary) therefore configuration storage must be obtained to store
the reservation.

Note the context data is shared among all instances of this directive. This is acceptable
because for any specific transaction there can be only one redirection so it being overridden if
multiple instances are invoked for a transaction is useful.

Configuration loading tracks how many instances of a directive have been loaded. The first time a
directive is encountered, a functor is invoked. By default this is an empty function but it can
be replaced per directive. This is used by the "redirect" directive.

.. literalinclude::  ../../../../plugins/experimental/txn_box/plugin/src/Machinery.cc
   :start-at: Do_redirect::cfg_init
   :end-before: doc Do_redirect::cfg_init

This is passed the configuration instance and the per directive static information. This is useful
for data that is per directive per configuration. For "redirect" this is for reserving per context
storage and reserving a hook slot to use for the fix ups.

When using a feature group, each key becomes associated with an index and those indices are used
to access key information. There is a presumption that the value for every key is a feature
expression, but additional type checking can be done after loading. The group can be loaded
as a single scalar, as a list, or as an object (set of keys). Keys can be marked as required in
which case it is an error if the key is not present. After loading, the key indices can be found
by name and cached for fast lookup during invocation.
