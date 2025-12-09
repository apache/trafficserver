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

.. _admin-plugins-filter_body:

Filter Body Plugin
******************

Description
===========

The ``filter_body`` plugin provides streaming request and response body content
inspection with configurable pattern matching and actions. It can be used to
detect and mitigate security threats such as CVE exploits, XXE (XML External
Entity) attacks, SQL injection patterns, and other malicious content.

The plugin uses a streaming transform approach with a lookback buffer to handle
patterns that may span buffer boundaries, avoiding the need to buffer the entire
request or response body.

Features
--------

- YAML-based configuration with flexible rule definitions.
- Header-based filtering with AND/OR logic.
- Case-insensitive header matching, case-sensitive body patterns.
- Configurable actions per rule: ``log``, ``block``, ``add_header``.
- Support for both request and response body inspection.
- Streaming transform with lookback buffer for cross-boundary pattern matching.
- Optional ``max_content_length`` to skip inspection of large bodies.
- Configurable HTTP methods to match (GET, POST, PUT, etc.).
- Optional ``status`` codes to match for response rules.
- Per-rule metrics counters for monitoring match activity.

Installation
============

The ``filter_body`` plugin is an experimental plugin. To build it, either pass
``-DENABLE_FILTER_BODY=ON`` to ``cmake`` when configuring the build::

    cmake -DENABLE_FILTER_BODY=ON ...

Or enable all experimental plugins with ``-DBUILD_EXPERIMENTAL_PLUGINS=ON``::

    cmake -DBUILD_EXPERIMENTAL_PLUGINS=ON ...

Configuration
=============

The plugin is configured as a remap plugin with a YAML configuration file::

    map http://example.com/ http://origin.example.com/ @plugin=filter_body.so @pparam=filter_body.yaml

The configuration file path can be relative to the |TS| configuration directory
or an absolute path.

Configuration File Format
-------------------------

The configuration file uses YAML format with a list of rules. Each rule has a
``name``, a ``filter`` section containing all filtering criteria, and an
``action`` section specifying what to do when a match occurs::

    rules:
      - name: rule_name
        filter:
          direction: request    # optional, defaults to request
          methods:              # for request rules only
            - POST
            - PUT
          max_content_length: 1048576
          headers:
            - name: Content-Type
              patterns:
                - "application/xml"
                - "text/xml"
          body_patterns:
            - "<!ENTITY"
            - "<!DOCTYPE"
        action:
          - log
          - block
          - add_header:
              X-Security-Match: <rule_name>
              X-Another-Header: some-value

For response rules, use ``status`` instead of ``methods`` within the ``filter``
section::

    rules:
      - name: response_rule
        filter:
          direction: response
          status:               # for response rules only
            - 200
            - 201
          body_patterns:
            - "sensitive_data"
        action:
          - log

Rule Options
------------

``name`` (required)
    A unique name for the rule. Used in log messages and metrics when the rule
    matches. The special placeholder ``<rule_name>`` can be used in header values
    to substitute the rule's name dynamically.

``filter`` (required)
    A section containing all filtering criteria that determine which requests or
    responses the rule applies to. This section separates the "what to match"
    from the "what to do" (action).

Filter Options
--------------

The following options are valid within the ``filter`` section:

``direction`` (optional)
    Specifies whether to inspect request or response bodies.
    Valid values: ``request``, ``response``. Default: ``request``.

``methods`` (optional)
    List of HTTP methods to match. If not specified, all methods are matched.
    Only valid for request rules. Example: ``[GET, POST, PUT]``.

``status`` (optional)
    List of HTTP status codes to match. If not specified, all status codes are
    matched. Only valid for response rules. Example: ``[200, 201]``.

``max_content_length`` (optional)
    Maximum content length in bytes for body inspection. Bodies larger than
    this value will not be inspected. If set to 0 or not specified, all bodies
    are inspected regardless of size.

``headers`` (optional)
    List of header conditions that must all match (AND logic) for body
    inspection to occur. Each header can have multiple patterns (OR logic
    within a single header).

    - ``name``: Header name (case-insensitive matching).
    - ``patterns``: List of patterns to match against the header value.

``body_patterns`` (required)
    List of patterns to search for in the body content. Pattern matching is
    case-sensitive. If any pattern matches, the configured actions are executed.

Action Options
--------------

``action`` (optional)
    List of actions to take when a pattern matches. Default is ``[log]``.
    Valid values:

    - ``log``: Log the match to the Traffic Server log.
    - ``block``: Block the request/response (see Block Action below for details).
    - ``add_header``: Add custom headers to the request/response. This action
      takes a map of header names to values. Use ``<rule_name>`` in header
      values to substitute the rule's name dynamically. Example::

          action:
            - log
            - add_header:
                X-Security-Match: <rule_name>
                X-Custom-Flag: detected

Matching Logic
==============

Header Matching
---------------

Headers are matched using the following logic:

1. All configured headers must match (AND logic between headers).
2. Within each header, any pattern can match (OR logic between patterns).
3. Header name matching is case-insensitive.
4. Header value matching is case-insensitive.

For example, with this configuration::

    filter:
      headers:
        - name: Content-Type
          patterns:
            - "application/xml"
            - "text/xml"
        - name: X-Custom-Header
          patterns:
            - "value1"

A request must have:

- A ``Content-Type`` header containing either "application/xml" OR "text/xml", AND
- An ``X-Custom-Header`` header containing "value1".

Body Pattern Matching
---------------------

Body patterns are matched using simple substring search:

- Matching is case-sensitive.
- Any pattern match triggers the configured actions.
- The plugin uses a streaming approach with a lookback buffer to handle patterns
  that may span buffer boundaries.

Actions
=======

Log Action
----------

When the ``log`` action is configured, pattern matches are logged to the
Traffic Server error log (``diags.log``). No special debug configuration is
required - log messages are always written when a pattern matches.

Log messages include the rule name and matched pattern in the format::

    NOTE: [filter_body] Matched rule: <rule_name>, pattern: <pattern>

To also log the headers for debugging, you can configure access logging to
include request and response headers. See :ref:`admin-logging` for details
on configuring access logs.

Block Action
------------

When the ``block`` action is configured, the request or response is blocked. The
connection to the origin is closed and no further data is forwarded.

.. warning::

    Because the plugin uses streaming body inspection, a malicious pattern may
    not be detected until after some (or all) of the body has already been sent
    to the origin. The ``block`` action stops further transmission but cannot
    recall data already sent. For maximum protection, consider using
    ``max_content_length`` to limit inspection to smaller bodies that can be
    buffered, or use header-based filtering to reduce the attack surface.

.. note::

    For request body transforms, the plugin cannot send a custom error response
    (such as 403 Forbidden) because the request headers have already been sent
    to the origin by the time the body is inspected. Instead, ATS closes the
    connection. Depending upon timing, the client may receive a 502 status
    response.

Add Header Action
-----------------

When the ``add_header`` action is configured, custom headers are added:

- For request rules: Headers are added to the server request (proxy request
  going to the origin). This header modification occurs during body inspection,
  after the initial request headers have been read but before they are sent
  to the origin.

- For response rules: Headers are added to the client response. Since body
  inspection occurs during response streaming, headers are added before the
  response body is sent to the client.

The ``add_header`` action takes a map of header names and values::

    action:
      - add_header:
          X-Security-Match: <rule_name>
          X-Custom-Flag: detected

Use the special placeholder ``<rule_name>`` in header values to substitute the
rule's name dynamically. Multiple headers can be specified in a single
``add_header`` action.

.. note::

    To verify that headers are being added correctly, you can configure access
    logging to include the server request headers (for request rules) or client
    response headers (for response rules). Use log fields like ``{Server-Request}``
    or ``{Client-Response}`` in your log format. See :ref:`admin-logging` for
    details.

Example Configurations
======================

XXE Attack Detection
--------------------

Block XML requests containing XXE patterns::

    rules:
      - name: xxe_detection
        filter:
          direction: request
          methods:
            - POST
            - PUT
          headers:
            - name: Content-Type
              patterns:
                - "application/xml"
                - "text/xml"
                - "application/xhtml+xml"
          body_patterns:
            - "<!ENTITY"
            - "<!DOCTYPE"
            - "SYSTEM"
            - "PUBLIC"
        action:
          - log
          - block

SQL Injection Detection (Log Only)
----------------------------------

Log potential SQL injection attempts without blocking::

    rules:
      - name: sql_injection_detection
        filter:
          direction: request
          methods:
            - POST
            - GET
          max_content_length: 65536
          body_patterns:
            - "' OR '"
            - "'; DROP"
            - "UNION SELECT"
            - "1=1"
        action:
          - log
          - add_header:
              X-Security-Warning: sql-injection-detected

Sensitive Data Detection in Responses
-------------------------------------

Add a header when response contains sensitive patterns::

    rules:
      - name: sensitive_data_leak
        filter:
          direction: response
          headers:
            - name: Content-Type
              patterns:
                - "application/json"
                - "text/html"
          body_patterns:
            - "password"
            - "ssn"
            - "credit_card"
        action:
          - log
          - add_header:
              X-Data-Classification: sensitive
              X-Rule-Matched: <rule_name>

Metrics
=======

The plugin creates a metrics counter for each configured rule. The counter is
incremented each time the rule matches a pattern in a request or response body.

Metric names follow this format::

    plugin.filter_body.rule.<rule_name>.matches

For example, a rule named ``xxe_detection`` would have a metric named::

    plugin.filter_body.rule.xxe_detection.matches

You can query these metrics using ``traffic_ctl``::

    traffic_ctl metric get plugin.filter_body.rule.xxe_detection.matches

Or list all filter_body metrics::

    traffic_ctl metric match plugin.filter_body

Debugging
=========

To enable debug output for the plugin, configure debug tags in records.yaml::

    records:
      proxy.config.diags.debug.enabled: 1
      proxy.config.diags.debug.tags: filter_body

Debug output includes:

- Configuration loading and rule parsing.
- Header matching results.
- Pattern match notifications.
- Action execution.

Limitations
===========

1. **Request blocking**: When blocking request bodies, the connection to the
   origin is closed and the client receives a 502 Bad Gateway response. The
   plugin cannot send a custom error response (such as 403 Forbidden) because
   the request headers have already been sent to the origin by the time the
   body is inspected. This is a limitation of request body transforms in |TS|.

2. **Pattern matching**: The plugin uses simple substring matching. Regular
   expressions are not currently supported.

3. **Memory usage**: The lookback buffer size is determined by the longest
   body pattern configured. Very long patterns may increase memory usage.

4. **Cross-boundary pattern search**: When searching for patterns that may span
   buffer block boundaries, the plugin uses a two-phase search. The boundary
   search copies only a small region (at most 2 * max pattern length bytes) to
   detect patterns spanning boundaries. The main block search is zero-copy.

5. **Performance**: Body inspection adds processing overhead. Use
   ``max_content_length`` to limit inspection to smaller bodies when appropriate.

See Also
========

- :doc:`header_rewrite.en` for header-based request/response modification.
- :doc:`access_control.en` for access control based on various criteria.
