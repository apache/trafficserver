.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: txnbox_common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _selection:

Selection
***************

Selection is the mechanism for conditional operation. This is done by specifying the extraction of a
feature then applying :term:`comparison`\s. Each cmparison has an associated list of directives
which is invoked if the comparison is successful. The :txb:drtv:`with` directive is used for
selection. The key :code:`select` is used to anchor the list of comparisons.  ::

   with: ua-req-host
   select:
   -  match: "mail.example.one"
      do:
      -  proxy-req-url: "https://example.com/mail"
   -  match: "search.example.one"
      do:
      -  proxy-req-url: "https://engine.example.one"

Here :ex:`ua-req-host` is an extractor that extracts the host of the URL in the client request.
The value of the :code:`select` key is a list of objects which consist of a comparison and a list of
directives as the value of the :code:`do` key.

The comparison :txb:cmp:`match` is a comparison operator that does string comparisons between its
value and the active feature. The directive :txb:drtv:`proxy-req-url` sets upstream destination in
the proxy request. What this configuration snippet does is change requests for "mail.example.one" to
requests to "example com/mail" and requests for "search.example.on" to "engine.example.one".

The :code:`with` / :code:`select` mechanism is a directive and so selection can be nested to an
arbitrary depth. Each selection can be on a different feature. As result the relative importance of
features is determined by the particular configuration. This means decisions can be made in a
hierarchial style rather than a single linear set of rules, which enables a large reduction in "cross
talk" between rules.
