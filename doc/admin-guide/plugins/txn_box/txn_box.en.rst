.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: ../../../common.defs
.. include:: txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _txn-box:

Concepts
********

|TxB| is based on the idea that for a given transaction and hook in |TS|, the administrator wants to
consider some subset of the information in the transaction and, based on that, perform specific
actions. There is a further presumption that the common use will be multiple comparisons against the
same data, each comparison associated with different actions. This is a generalization of how URL
rewriting happens currently in |TS| via "remap.config". The difference between this and remapping is
the latter considers only a fixed, very small subset of data in the transaction, the set of actions
is limited, and there is only one decision point.

To aid further explanation, some terms need to be defined.

Every action in |TxB| is associated with a specific :term:`hook`. A |TxB| hook is essentially the
same as a hook in |TS|. In addition to the hooks in |TS|, |TxB| has additional hooks specific to
|TxB|.

|TxB| has two phases of operation, :term:`load time` and :term:`run time`. Load time is the
time during which the configuration is being loaded and run time is when processing transactions.
Operations during load time are done by associating the action with a hook that triggers during
load time.

A :term:`feature` is data of interest for a transaction. A feature is created by :term:`Extraction`
which is specifed by a :term:`feature expression`, which is a series of literals and
:term:`extractor`\s. Each extractor retrieves a specific datum and the combination of those and
(optional) literals defines the resulting feature. Features can be one of several types, the most
common being a string.

A :term:`directive` is an action to be performed. Some directives can have an :term:`argument` which
provides additional control of the directive's action.

A :term:`comparison` is an operation that compares two features.

:term:`Selection` is using comparisons can to select specific directives to perform. This is the way
in which conditional actions are done in |TxB|.

A |TxB| configuration is organized at the top level by hooks, to which are attached the directives
that specify what |TxB| should do to manipulate the transaction. These directives can be conditional
based on comparisons to extracted features. This yields a very flexible and powerful mechanism for
manipulating transactions.

Configuration
*************

|TxB| can be configured as a global plugin or a remap plugin. In both cases it takes a configuration
file that is YAML.

For a global configuration, the top level directives must all be :txb:drtv:`when` thereby
associating every directive with a specific hook. For a remap configuration, all directives are
grouped in an implied :code:`when: remap` and therefore no explicit :code:`when` is required.

Each directive and extractor has an associated set of hooks in which it is valid, therefore some
will be available in a remap configuration and some will not. In particular there are several
directives which are specific to remap because they interact with the data passed to a remap plugin
which is not available in any other context.

For both global and remap plugins the configuration file must be specified. A specific key in the
file is used as the base of the configuration, ignoring any other data. For global configuration
this is by default the :code:`txn_box` key at the top level. For remap it is the top level node in
the file (generally the entire file). This can be overridden by a second parameter, which is a path
to the root configuration node. This must be a sequence of keys in maps, starting from the top. The
path is specified by a dot separated list of these keys. For example, consider a file with this at
the top node level. ::

   txn_box: # path - "txn_box"
      example-1: # path - "txn_box.example-1"
         inner-1: # path - "txn_box.examle-1.inner-1"
      example-2: # path "txn_box.example-2"

If "example-1" was to be the root, the path would be "txn_box.example-1". The global default,
"txn_box", would select "txn_box"" as the root node. The path could also be
"txn_box.example-1.inner-1" to select the inner most node. As a special case, the path "." means
"the unnamed top level node". Note this is problematic in the case of keys that contains ".", which
should be avoided.

The point of specifying a key is to enable |TxB| configuration to exist inside a file that contains
other configuration, or to enable having a single file that provides configuration for multiple
instances of |TxB|.

Hooks
============

The directive key :txb:drtv:`when` can be used to specify on which hook directives should be performed.
Each :code:`when` must also have a :code:`do` key which contains the directives. The value of :code:`when`
is the hook name, which must be one of

================== =============  ============ ========================
Hook               when           Abbreviation Plugin API Name
================== =============  ============ ========================
Client Request     read-request   ua-req       READ_REQUEST_HDR_HOOK
Proxy Request      send-request   proxy-req    SEND_REQUEST_HDR_HOOK
Upstream Response  read-response  upstream-rsp READ_RESPONSE_HDR_HOOK
Proxy Response     send-response  proxy-rsp    SEND_RESPONSE_HDR_HOOK
Pre remap          pre-remap                   PRE_REMAP_HOOK
Post remap         post-remap                  POST_REMAP_HOOK
Transaction Open   txn-open                    TXN_START
Load time          post-load
================== =============  ============ ========================

The abbreviations are primarily for consistency between hook tags, extractors, and directives.

For a global plugin, the top level directives must be :txb:drtv:`when`. To set the HTTP header field
``swoc`` to ``invoked`` immediately after the client request has been read, it would be ::

   txn_box:
   -  when: ua-req
      do:
      -  ua-req-field<swoc>: "invoked"

For a remap plugin, the directives are wrapped in a notional code:`when: remap` directive, so no
explicit :code:`when` directive is needed and all top level directives are performed during remap.
If :code:`when` is used the :code:`when` is exectued during remap, scheduling the contained
directives for the future hook on that transaction.

The ``post-load`` hook is invoked immediately after the configuration is loaded and parsed.
Directives on this hook may return errors which prevents the configuration from successfully
loading. It is used to create resources that persist for the entire time the configuration is in
use.
