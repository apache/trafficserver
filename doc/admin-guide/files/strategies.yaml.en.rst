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

===============
strategies.yaml
===============

.. configfile:: strategies.yaml

.. include:: ../../common.defs

The :file:`strategies.yaml` file identifies the next hop proxies used in an
cache hierarchy and the algorithms used to select the next hop proxy. Use
this file to perform the following configuration:

-  Set up next hop cache hierarchies, with multiple parents and parent
   failover

Traffic Server uses the :file:`strategies.yaml` file only when one or more
remap lines in remap.config specifies the use of a strategy with the @strategy tag.
remap.config Example::

  map http://www.foo.com http://www.bar.com @strategy='mid-tier-north'

After you modify the :file:`strategies.yaml` file, run the :option:`traffic_ctl config reload`
command to apply your changes.

Format
======

The `strategies.yaml` is a YAML document with three top level namespaces: **hosts**, **groups**
and **strategies**.  These name spaces may be in separate files.  When in separate files, use
**#include filename** in the `strategies.yaml` so that they are concatenated by the strategy
factory into a single YAML document in the order, **hosts**, **groups**, and the **strategies**.

Alternatively if the config parameter `proxy.config.url_remap.strategies.filename` refers to
a directory, the NextHopStrategyFactory will alphanumerically concatenate all files in that directory that end in `.yaml` by name into a single document stream for parsing.  The final document must be a valid `YAML` document with single `strategies` node and optionally a single `hosts` and `groups` node.  Any **#include filename** strings are ignored when reading `.yaml` files in a directory.

Hosts definitions
=================

The **hosts** definitions is a **YAML** list of hosts.  This list is **optional** but if not used, the
**groups** list **must** include complete definitions for hosts.  See the **group** examples below.

In the example below, **hosts** is a **YAML** list of hosts.  Each host entry  uses a **YAML** anchor,
**&p1** and **&p2** that may be used elsewhere in the **YAML** document to refer to hosts **p1** and **p2**.

- **host**: the host value is a hostname string
- **protocol**: a list of schemes, ports, and health check urls for  the host.
- **healthcheck**: health check information with the **url** used to check
  the hosts health by some external health check agent.

Example::

  hosts:
    - &p1
      host: p1.foo.com
      protocol:
        - scheme: http
          port: 80
          health_check_url: http://192.168.1.1:80
        - scheme: https
          port: 443
          health_check_url: https://192.168.1.1:443
  	- &p2
      host: p2.foo.com
      protocol:
        - scheme: http
          port: 80
          health_check_url: http://192.168.1.2:80

Groups definitions
==================

The **groups** definitions is a **YAML** list of host groups.  host groups are used as the primary and secondary groups used by nexthop to choose hosts from.  The first group is the **primary** group next hop chooses hosts from.  The remaining groups are used failover.  The **strategies** **policy** specifies how the groups are used.

Below are examples of group definitions.  The first example is using **YAML** anchors and references.
When using **references**, the complete **YAML** document must include the **anchors** portion of the document first.

The second example shows a complete **groups** definition without the use of a **hosts** name space and it's **YAML** anchors.

The field definitions in the examples below are defined in the **hosts** section.

Example using **YAML** anchors and references::

  groups:
  	- &g1
      - <<: *p1
      	weight: 1.5
      - <<: *p2
      	weight: 0.5
  	- &g2
      - <<: *p3
      	weight: 0.5
      - <<: *p4
        weight: 1.5

Explicitly defined Example, no **YAML** references::

  groups:
    - &g1
      - p1
        host: p1.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.1:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.1:443
        weight: 0.5
      - p2
        host: p2.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.2:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.2:443
        weight: 0.5
    - &g2
      - p3
        host: p3.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.3:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.3:443
        weight: 0.5
      - p4
        host: p4.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.4:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.4:443
        weight: 0.5

Strategies definitions
======================

The **strategies** namespace defines a **YAML** list of strategies that may be applied to a **remap**
entry using the **@strategy** tag in remap.config.

Each **strategy** in the list may using the following parameters::

- **strategy**: The value is the name of the strategy.
- **policy**: The algorithm the **strategy** uses to select hosts. Currently one of the following:

   #. **rr_ip**: round robin selection using the modulus of the client IP
   #. **rr_strict**: strict round robin over the list of hosts in the primary group.
   #. **first_live**: always selects the first host in the primary group.  Other hosts are selected when the first host fails.
   #. **latched**:  Same as **first_live** but primary selection sticks to whatever host was used by a previous transaction.
   #. **consistent_hash**: hosts are selected using a **hash_key**.

- **hash_key**: The hashing key used by the **consistent_hash** policy. If not specified, defaults to **path** which is the
  same policy used in the **parent.config** implementation. Use one of:

   #. **hostname**: Creates a hash using the **hostname** in the request URL.
   #. **path**: (**default**) Creates a hash over the path portion of the request URL.
   #. **path+query**: Same as **path** but adds the **query string** in the request URL.
   #. **path+fragment**: Same as **path** but adds the fragment portion of the URL.
   #. **cache_key**: Uses the hash key from the **cachekey** plugin.  defaults to **path** if the **cachekey** plugin is not configured on the **remap**.
   #. **url**: Creates a hash from the entire request url.

- **go_direct** - A boolean value indicating whether a transaction may bypass proxies and go direct to the origin. Defaults to **true**
- **parent_is_proxy**: A boolean value which indicates if the groups of hosts are proxy caches or origins.  **true** (default) means all the hosts used in the remap are |TS| caches.  **false** means the hosts are origins that the next hop strategies may use for load balancing and/or failover.
- **scheme** Indicates which scheme the strategy supports, *http* or *https*
  - **failover**: A map of **failover** information.
  - **max_simple_retries**: Part of the **failover** map and is an integer value of the maximum number of retries for a **simple retry** on the list of indicated response codes.  **simple retry** is used to retry an upstream request using another upstream server if the response received on from the original upstream request matches any of the response codes configured for this strategy in the **failover** map.  If no failover response codes are configured, no **simple retry** is attempted.

  - **ring_mode**: Part of the **failover** map. The host ring selection mode.  Use either **exhaust_ring** or **alternate_ring**

   #. **exhaust_ring**: when a host normally selected by the policy fails, another host is selected from the same group.  A new group is not selected until all hosts on the previous group have been exhausted
   #. **alternate_ring**: retry hosts are selected from groups in an alternating group fashion.

  - **response_codes**: Part of the **failover** map.  This is a list of **http** response codes that may be used for **simple retry**.
  - **health_check**: Part of the **failover** map.  A list of health checks. **passive** is the default and means that the state machine marks down **hosts** when a transaction timeout or connection error is detected.  **passive** is always used by the next hop strategies.  **active** means that some external process may actively health check the hosts using the defined **health check url** and mark them down using **traffic_ctl**.


Example:
::

  #include unit-tests/hosts.yaml
  #
  strategies:
    - strategy: 'strategy-1'
      policy: consistent_hash
      hash_key: cache_key
      go_direct: false
      groups:
        - *g1
        - *g2
      scheme http
      failover:
        ring_mode: exhaust_ring
        response_codes:
          - 404
          - 503
        health_check:
          - passive
    - strategy: 'strategy-2'
      policy: rr_strict
      hash_key: cache_key
      go_direct: true
      groups:
        - *g1
        - *g2
      scheme http
      failover:
        ring_mode: exhaust_ring
        response_codes:
          - 404
          - 503
        health_check:
          - passive
