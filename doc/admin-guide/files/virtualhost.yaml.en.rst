
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

.. configfile:: virtualhost.yaml

virtualhost.yaml
****************

The :file:`virtualhost.yaml` file defines configuration blocks that apply to a group of domains.
Each virtual host entry defines a set of domains and the remap rules associated with those domains.
Virtual host remap rules override global :file:`remap.config` rules but remain fully backward compatible
with existing configurations. If absent, ATS behaves exactly as before.

Currently, this file only supports :file:`remap.config` overrides. Future versions will expand virtual
host support to additional configuration types (e.g. :file:`sni.yaml`, :file:`ssl_multicert.config`,
:file:`parent.config`, etc)

By default this is named :file:`virtualhost.yaml`. The filename can be changed by setting
:ts:cv:`proxy.config.virtualhost.filename`.


Configuration
=============

:file:`virtualhost.yaml` is YAML format with top level namespace **virtualhost** and a list of virtual host
entries. Each virtual host entry must provide an **id** and at least one domain defined in **domains**.

An example configuration looks like:

.. code-block:: yaml

   virtualhost:
     - id: example
       domains:
         - example.com

       remap:
         - map http://example.com http://origin.example.com/


===================== ==========================================================
Field Name            Description
===================== ==========================================================
``id``                 Virtual host identifier to perform specific operations on
``domains``            List of domains to resolve a request to
``remap``              List of remap rules as defined in remap.config
===================== ==========================================================

``domains``
   Domains can be defined as request domain name or subdomains using wildcard feature.
   Wildcard support only allows single left most ``*``. This does not support regex.
   When matching to a virtual host entry, domains with exact match have precedence
   over wildcard. If a domain matches to multiple wildcard domains, the virtual host
   config defined first has precedence.

   For example:
      Supported:
      - ``foo.example.com``
      - ``*.example.com``
      - ``*.com``

      NOT Supported:
      - ``foo[0-9]+.example.com`` (regex)
      - ``bar.*.example.net`` (``*`` in the middle)
      - ``*.bar.*.com`` (multiple ``*``)
      - ``*.*.baz.com`` (multiple ``*``)
      - ``baz*.example.net`` (partial wildcard)
      - ``*baz.example.net`` (partial wildcard)
      - ``b*z.example.net`` (partial wildcard)
      - ``*`` (global)

Evaluation Order
----------------

|TS| evaluates a request using deterministic precedence in the following order:

1. Resolve to a single virtualhost
   a. Check for an exact domain match. If any virtual host lists the request hostname explicitly, that virtual host is selected.
   b. Check for a wildcard domain match. If any virtual host wildcard domains define a subdomain of the request hostname in the form ``*.[domain]``, that virtual host is selected.
   c. If no matching virtual host exists, the request proceeds using global configuration (i.e :file:`remap.config`). Skip to step 3.
2. Within selected virtual host config, use virtual host remap rules.
   a. Follow existing :file:`remap.config` rules and matching orders. If a matching remap rule is found, that remap rule is selected.
3. If neither virtual host nor remap rules match, ATS falls back to global :file:`remap.config` resolution.

Only one virtual host entry may match a given request. If multiple entries could match, ATS uses the first matching
entry defined in :file:`virtualhost.yaml`.


Granular Reload
===============

|TS| now supports granular configuration reloads for individual virtual hosts defined in :file:`virtualhost.yaml`.
In addition to reloading the entire |TS| configuration with :option:`traffic_ctl config reload`, users can
selectively reload a single virtual host entry without affecting other virtual host entries.

By only updating the necessary changes, this reduces configuration deployment time and improves visibility on the changes made.

To reload for a specific virtual host, use:

::

   $ traffic_ctl config reload --virtualhost <id>

Where **<id>** is the virtual host ID defined in :file:`virtualhost.yaml`. Only the **<id>** virtual host
configuration will be reloaded. This does not affect other virtual hosts or global configuration files.

Example:

::

   $ traffic_ctl config reload --virtualhost foo
   ┌ Virtualhost: foo
   └┬ Reload status: ok
    ├ Message: Virtualhost successfully reloaded


Examples
========

.. code-block:: yaml

   # virtualhost.yaml
   virtualhost:
     - id: example
       domains:
         - example.com

       remap:
         - map http://example.com/ http://origin.example.com/

   # remap.config
   map / http://other.example.com/

This rules translates in the following translation.

================================================ ========================================================
Client Request                                   Translated Request
================================================ ========================================================
``http://example.com/index.html``                ``http://origin.example.com/index.html``
``http://www.x.com/index.html``                  ``http://other.example.com/index.html``
================================================ ========================================================

.. code-block:: yaml

   # virtualhost.yaml
   virtualhost:
     - id: example
       domains:
         - "*.example.com"

       remap:
         - regex_map http://sub[0-9]+.example.com/ http://origin$1.example.com/

      - id: foo
       domains:
         - foo.example.com

       remap:
         - map http:/foo.example.com/ http://foo.origin.com/

This rules translates in the following translation.

================================================ ========================================================
Client Request                                   Translated Request
================================================ ========================================================
``http://sub0.example.com/index.html``           ``http://origin0.example.com/index.html``
``http://foo.example.com/index.html``            ``http://foo.origin.com/index.html``
``http://bar.example.com/index.html``             No remap rule found in virtual host entry `example`
================================================ ========================================================


See Also
========

:file:`remap.config`
