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

- **host**: the host value is a Fully Qualified Domain Name string
- **protocol**: a list of schemes, ports, and health check urls for the host. The **scheme** is optional; strategies with no scheme will match hosts with no scheme. Note the scheme is only used to match the strategy, the actual scheme used in the upstream request will be the scheme of the remap target, regardless of the strategy or host scheme.
- **healthcheck**: health check information with the **url** used to check
  the hosts health by some external health check agent.
- **hash_string**: a string to use for this host's entry in the strategy. By default, the ``host`` is used (notably without a port).
   - This is currently only used by ``consistent_hash`` policy strategies, but may be used by other policies in the future.
   - There's generally no benefit to giving any host any particular hash string, but this may be useful, for example:
      - If multiple host objects share the same ``host`` FQDN, possibly on different ports
      - If a parent server's FQDN changes, to prevent changing a host's position on the hash ring, and thus breaking the cache and sending different requests to different parents
      - To force a change in the order of the hash ring for debugging purposes

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
      host: p2.new.foo.com
      hash_string: p2.original.foo.com
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
      - host: p1.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.1:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.1:443
        weight: 0.5
      - host: p2.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.2:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.2:443
        weight: 0.5
    - &g2
      - host: p3.foo.com
        protocol:
          - scheme: http
            port: 80
            health_check_url: http://192.168.1.3:80
          - scheme: https
            port: 443
            health_check_url: https://192.168.1.3:443
        weight: 0.5
      - host: p4.foo.com
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

Each **strategy** in the list may using the following parameters:

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

- **go_direct**: A boolean value indicating whether a transaction may bypass proxies and go direct to the origin. Defaults to **true**
- **parent_is_proxy**: A boolean value which indicates if the groups of hosts are proxy caches or origins.  **true** (default) means all the hosts used in the remap are |TS| caches.  **false** means the hosts are origins that the next hop strategies may use for load balancing and/or failover.
- **cache_peer_result**: A boolean value that is only used when the **policy** is 'consistent_hash' and a **peering_ring** mode is used for the strategy. When set to true, the default, all responses from upstream and peer endpoints are allowed to be cached.  Setting this to false will disable caching responses received from a peer host. Only responses from upstream origins or parents will be cached for this strategy.
- **scheme**: Indicates which scheme the strategy supports, *http* or *https*. Note this is only used to match hosts to strategies; the actual scheme used in the upstream request will be the scheme of the remap.config target, regardless of this value. The **scheme** is optional, and strategies without a scheme will match hosts without a scheme. This allows for omitting the scheme if hosts are not shared between strategies, or if all strategies using a given host use the same scheme.
- **failover**: A map of **failover** information.

  - **max_simple_retries**: Part of the **failover** map and is an integer value of the maximum number of retries for a **simple retry** on the list of indicated response codes.  **simple retry** is used to retry an upstream request using another upstream server if the response received on from the original upstream request matches any of the response codes configured for this strategy in the **failover** map.  If no failover response codes are configured, no **simple retry** is attempted.
  - **max_unavailable_retries**: Part of the **failover** map and is an integer value of the maximum number of retries for a **unavailable retry** on the list of indicated markdown response codes.  **unavailable retry** is used to retry an upstream request using another upstream server if the response received on from the original upstream request matches any of the markdown response codes configured for this strategy in the **failover** map.  If no failover markdown response codes are configured, no **unavailable retry** is attempted.  **unavailable retry** differs from **simple retry** in that if a failover for retry is done, the previously retried server is marked down for rety.

  - **ring_mode**: Part of the **failover** map. The host ring selection mode.  Use either **exhaust_ring**,  **alternate_ring** or **peering_ring**

   #. **exhaust_ring**: when a host normally selected by the policy fails, another host is selected from the same group.  A new group is not selected until all hosts on the previous group have been exhausted
   #. **alternate_ring**: retry hosts are selected from groups in an alternating group fashion.
   #. **peering_ring**: This mode is only implemented for a policy of **consistent_hash** and requires that one or two
      host groups are defined. The first host group is a list of peer caches and "this" host itself, the (optional) second
      group is a list of upstream caches. Parents are always selected from the peer list however, if the selected parent is
      "this" host itself a new parent from the upstream list is chosen. If the second group is omitted, and **go_direct**
      is **true**, the upstream "list" has one element,
      the host in the remapped URL. In addition, if any peer host is unreachable or times out, a host from the upstream
      list is chosen for retries. Because the peer hosts may at times not have consistent up/down markings for the other
      peers, requests may be looped sometimes. So it's best to use :ts:cv:`proxy.config.http.insert_request_via_str` and :ts:cv:`proxy.config.http.max_proxy_cycles` to stop looping.

  - **response_codes**: Part of the **failover** map.  This is a list of **http** response codes that may be used for **simple retry**.
  - **markdown_codes**: Part of the **failover** map.  This is a list of **http** response codes that may be used for **unavailable retry** which will cause a parent markdown.
  - **health_check**: Part of the **failover** map.  A list of health checks. **passive** is the default and means that the state machine marks down **hosts** when a transaction timeout or connection error is detected.  **passive** is always used by the next hop strategies.  **active** means that some external process may actively health check the hosts using the defined **health check url** and mark them down using **traffic_ctl**.
  - **self**: Part of the **failover** map.  This can only be used when **ring_mode** is **peering_ring**.  This is the hostname of the host in the (first) group of peers that is the local host |TS| runs on.
    (**self** should only be necessary when the local hostname can only be translated to an IP address
    with a DNS lookup.)

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
      scheme: http
      failover:
        ring_mode: exhaust_ring
        response_codes:
          - 404
        markdown_codes:
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
      scheme: http
      failover:
        ring_mode: exhaust_ring
        response_codes:
          - 404
        markdown_codes:
          - 503
        health_check:
          - passive
