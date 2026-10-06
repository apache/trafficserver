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

.. _admin-plugins-jax-fingerprint:

JAx Fingerprint Plugin
**********************

Description
===========

The JAx Fingerprint plugin generates client fingerprints based on the JA4+ or JA3 algorithms designed by John Althouse.

Fingerprints can be used for:

* Client identification and tracking
* Bot detection and mitigation
* Security analytics and threat intelligence
* Understanding client implementation patterns


Plugin Configuration
====================

The plugin is configured with a YAML file that is passed as the plugin's only argument. A relative
path is resolved from the |TS| configuration directory. You can use the plugin as a global plugin,
a remap plugin, or both.

To use the plugin as a global plugin, add the following line to :file:`plugin.config`::

    jax_fingerprint.so jax_fingerprint.yaml

To use the plugin as a remap plugin, append the following to a remap rule in :file:`remap.config`::

    @plugin=jax_fingerprint.so @pparam=jax_fingerprint_remap.yaml

To use the plugin in a hybrid setup, load it both in :file:`plugin.config` and in
:file:`remap.config`, without setting ``standalone`` in either configuration. See
`Plugin Behavior`_ for how to choose between these setups.

When loaded through :file:`plugin.yaml`, the configuration can also be provided inline through the
``config`` field:

.. code-block:: yaml

    plugins:
      - path: jax_fingerprint.so
        config: |
          jax_fingerprint:
            fingerprints:
              - method: JA4
                standalone: true
                header: x-ja4

Configuration File
------------------

The configuration file contains a single ``jax_fingerprint`` map with a ``fingerprints`` list.
Each entry in the list configures one fingerprinting method. A single configuration file can list
as many methods as needed, and every method listed is generated independently. For example, the
following global configuration adds JA3, JA4, and JA4H fingerprints to every request:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA3
          standalone: true
          header: x-ja3
        - method: JA4
          standalone: true
          header: x-ja4
          log_filename: jax_ja4
        - method: JA4H
          standalone: true
          header: x-ja4h

Unrecognized keys, duplicate keys, and invalid values are reported in :file:`diags.log` and cause the configuration
to be rejected. A rejected remap configuration fails the :file:`remap.config` load.

Each fingerprint entry supports the following keys.

``method``
    The fingerprinting method to use: ``JA4``, ``JA4H``, or ``JA3``. This key is required.

``standalone``
    ``true`` or ``false``. The default is ``false``. Set this to ``true`` when you use either the
    global setup or the remap setup. Leave it unset in both configurations when you use the
    hybrid setup.

``mode``
    What to do when a client request already contains the headers named by ``header`` and/or
    ``via_header``. Available values are ``overwrite``, ``keep``, and ``append``. The default is
    ``overwrite``.

``header``
    The name of the header field where the plugin stores the generated fingerprint value. If not
    specified, the fingerprint header is not added.

``via_header``
    The name of the header field where the plugin stores the generated fingerprint-via value. If
    not specified, the fingerprint-via header is not added.

``servernames``
    A list of server names for which the plugin generates fingerprints. If not specified, the plugin
    generates fingerprints for any server name. For example:

    .. code-block:: yaml

        servernames:
          - abc.example
          - xyz.example

``export``
    The name of the registry in which generated fingerprints are published for other plugins. See
    `Fingerprint Registry Export`_. The default registry name is ``jax_fingerprint``.

``log_filename``
    The filename for the plugin log file. If not specified, log output is suppressed. See
    `Log Output`_.

``log_field``
    Registers a custom log field with the given symbol name that can be used in
    :file:`logging.yaml` log formats. The log field outputs the generated fingerprint value for each
    transaction. If not specified, no custom log field is registered.

    For example, with ``log_field: jaxja4`` you can use ``%<jaxja4>`` in a log format string in
    :file:`logging.yaml`.

    .. note:: This key is only supported in the configuration loaded from :file:`plugin.config`.
       Log fields are global and must be registered before log formats are parsed at startup. If
       you use a remap-only setup, you must also load the plugin globally with ``log_field`` to
       register the log field.


Reloading the Configuration
---------------------------

The configuration loaded from :file:`plugin.config` can be re-read without restarting |TS| by
sending the plugin a message::

    traffic_ctl plugin msg jax_fingerprint.reload

The following keys take effect for new connections and transactions after a successful reload:

* ``servernames``
* ``header``
* ``via_header``
* ``mode``

The other keys configure hooks, registries, log files, and log fields that are set up when |TS|
starts, so they can only be changed with a restart. The list of fingerprints must contain the same
entries, in the same order, with the same ``method``, ``standalone``, ``export``,
``log_filename``, and ``log_field`` values as the configuration loaded at startup. A reload that
changes any of these, or that fails to load, is rejected with an error in :file:`diags.log` and the
current configuration stays in effect. A successful reload is noted in :file:`diags.log`.

For example, to start fingerprinting connections for a new service, add its server name to the
list and reload:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA4
          servernames:
            - abc.example
            - xyz.example
            - new-service.example

Remap configurations are reloaded with :file:`remap.config` (for example, with
``traffic_ctl config reload``) and accept changes to any key.


Plugin Behavior
===============

Global plugin setup
-------------------

Global plugin setup is the best if you:
 * Need a fingerprint on every request

Remap plugin setup
------------------

Remap plugin setup is the best if you:
 * Need a fingerprint only on specific paths, or
 * Cannot use Global plugin setup

.. note:: For JA3 and JA4, fingerprints are always generated at the beginning of connections. Using remap plugin setup only reduces the overhead of adding HTTP headers and logging.

Hybrid setup
------------

Hybrid setup is the best if you:
 * Need a fingerprint only for specific server names (in TLS SNI extension), and
 * Need a fingerprint only on specific paths

Fingerprint Registry Export
---------------------------

Fingerprints with the same storage type and ``export`` name contribute to one registry, so a
ClientHello fingerprint is computed once even when more than one plugin uses it. For example:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA3
          export: security.fingerprints
        - method: JA4
          export: security.fingerprints

Consumers should use an explicit registry name to make the relationship clear, and must be loaded
after the JAx plugin in :file:`plugin.config`.

The export registry is an in-process, read-only view stored in a named |TS|
user-argument slot. The registry contains length-delimited method/value
entries plus a magic value, ABI version, and structure sizes, allowing
consumers to reject incompatible layouts. JAx owns the registry and strings
for the lifetime of the connection or transaction; consumers must not modify
them or retain their pointers.

Connection-based methods such as JA3 and JA4 use a VConn registry. Request-
based methods such as JA4H use a transaction registry and therefore cannot be
consumed at a ClientHello hook. If your organization implements custom JA
methods, these also can be exported through the same registry without adding a
method-specific API to ``ts.h``.


Log Output
==========

The plugin outputs a log file in the Traffic Server log directory (typically ``/var/log/trafficserver/``) if a log filename is
specified by the ``log_filename`` key.

**Log Format**::

    [timestamp] Client: <address>    <method_name>: <fingerprint>

**Example**::

    [Jan 29 10:15:23.456] Client: 192.168.1.100    JA4: t13d1516h2_8daaf6152771_b186095e22b6
    [Jan 29 10:15:24.123] Client: 10.0.0.50        JA4: t13d1715h2_8daaf6152771_02713d6af862


Using HTTP Headers in Origin Requests
=====================================

Origin servers can access the generated fingerprint through the injected HTTP header.
This allows the origin to:

* Make access control decisions based on client fingerprints
* Log fingerprints for security analysis
* Track client populations and TLS implementation patterns

The fingerprint-via header allows origin servers to track which Traffic Server proxy handled the request when multiple proxies are deployed.


Debugging
=========

To enable debug logging for the plugin, set the following in :file:`records.yaml`::

    records:
      diags:
        debug:
          enabled: 1
          tags: jax_fingerprint


Requirements
============

* Traffic Server must be built with TLS support (OpenSSL or BoringSSL) if you use JA3 or JA4


See Also
========
* JA3 Technical Specification: https://github.com/FoxIO-LLC/ja3
* JA4+ Technical Specification: https://github.com/FoxIO-LLC/ja4


Example Configurations
======================

Enable JA4 fingerprinting for every request
-------------------------------------------

This configuration adds an x-ja4 header to every request and logs each fingerprint to the
``jax_ja4`` log file.

**plugin.config**::

    jax_fingerprint.so jax_fingerprint.yaml

**jax_fingerprint.yaml**:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA4
          standalone: true
          header: x-ja4
          via_header: x-ja4-via
          log_filename: jax_ja4

Enable JA4H fingerprinting on a single remap rule
-------------------------------------------------

This configuration adds an x-ja4h header only to requests that match the remap rule.

**remap.config**::

    map https://www.example.com/ https://origin.example.com/ @plugin=jax_fingerprint.so @pparam=jax_ja4h.yaml

**jax_ja4h.yaml**:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA4H
          standalone: true
          header: x-ja4h

Enable JA4 fingerprinting by hybrid (global + remap) setup
----------------------------------------------------------

This configuration adds an x-my-ja4 header to requests that match the remap rule if the connection
was established for either abc.example or xyz.example. The global plugin generates the fingerprint
when the TLS connection is established, and the remap plugin adds the header.

**plugin.config**::

    jax_fingerprint.so jax_fingerprint.yaml

**jax_fingerprint.yaml**:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA4
          servernames:
            - abc.example
            - xyz.example

**remap.config**::

    map / http://origin.example/ @plugin=jax_fingerprint.so @pparam=jax_fingerprint_remap.yaml

**jax_fingerprint_remap.yaml**:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA4
          header: x-my-ja4

Log multiple fingerprints in the access log
-------------------------------------------

This configuration registers custom log fields for JA3, JA4, and JA4H fingerprints and records
them in the access log.

**plugin.config**::

    jax_fingerprint.so jax_fingerprint.yaml

**jax_fingerprint.yaml**:

.. code-block:: yaml

    jax_fingerprint:
      fingerprints:
        - method: JA3
          log_field: jaxja3
        - method: JA4
          log_field: jaxja4
        - method: JA4H
          standalone: true
          log_field: jaxja4h

**logging.yaml**:

.. code-block:: yaml

    logging:
      formats:
        - name: fingerprints
          format: '%<chi> %<cqu> JA3=%<jaxja3> JA4=%<jaxja4> JA4H=%<jaxja4h>'
      logs:
        - filename: fingerprints
          format: fingerprints
