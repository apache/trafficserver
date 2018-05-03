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

.. configfile:: metrics.config

metrics.config
**************

This configuration file is used to define dynamic metrics on |TS| activity.
Metrics defined here are available through all normal means of metrics
reporting, including :program:`traffic_line` and :ref:`admin-plugins-stats-over-http`.

Format
======

The configuration file itself is a Lua script. As with normal Lua code, comments
begin with ``--``, you may declare your own functions, and you may define global
variables.

Metric Definitions
==================

Metrics are defined by calling the supplied metric generator functions. There
is one for each supported type, and their parameters are identical::

    <typefn> '<name>' [[
      <metric generating function body>
    ]]

In practice, this will look like:

.. code:: lua

    float 'proxy.node.useful_metric' [[
        return math.random()
    ]]

With perhaps something more useful in the body of the metric generator. The
string containing the metric generating function's body (everything between
``[[`` and ``]]``, which is a multiline literal string in Lua) is stored and
then evaluated as an anonymous function, which will receive a single argument:
the name of the metric (in the example above: ``proxy.node.useful_metric``). If
necessary, you can capture this parameter using the ``...`` operator, which
returns the remaining parameters of the enclosing function.

Metric Types
------------

float
~~~~~

A gauge style metric which will return floating point numbers. Floating point
gauge metrics are appropriate for values which may increase or decrease
arbitrarily (e.g. disk usage, cache hit ratios, average document sizes, and so
on).

integer
~~~~~~~

A gauge style metric which will return integers. Integer gauge metrics are
appropriate for values which may increase or descrease arbitrarily, and do not
need any decimal components.

counter
~~~~~~~

A metric which will supply integer only values used almost exclusively to
report on the number of events, whatever they may be, that have occurred.
Frequent uses are the number of requests served, responses by specific HTTP
status codes, the number of failed DNS lookups, and so on.

Metric Scopes
-------------

All dynamic metrics, like their built-in counterparts, exist within a scope
which determines whether they reflect the state of the current |TS| node, or
the state of the entire |TS| cluster for which the current node is a member.

The scope of a metric is derived from its name. All metric names begin with
``proxy.`` followed by either ``node.`` or ``cluster.``.

Thus, ``proxy.node.active_origin_connections`` might be used for the number of
open connections to origin servers on just the current node, whereas
``proxy.cluster.active_origin_connections`` would be the counterpart for the
total open connections to origin servers from all |TS| nodes in the cluster,
including the current node. (Note that these names are contrived, and you are
advised to both pick as clear and detailed a metric name as possible and also
to ensure there is no conflict with existing metric names).

Definition Examples
-------------------

For illustrative purposes, a few of the dynamic metric definitions you may find
in your |TS| installation's default :file:`metrics.config` are explained here.
The actual file will contain many more definitions, and of course you may add
your own, as well.

Returning a single value
~~~~~~~~~~~~~~~~~~~~~~~~

The simplest example is a dynamic node metric which does nothing but return the
current value for an underlying process metric:

.. code:: lua

    counter 'proxy.node.http.user_agents_total_documents_served' [[
      return proxy.process.http.incoming_requests
    ]]

This uses the built-in function ``counter``, which takes two parameters: the
name of the dynamic metric to create followed by the function used to calculate
the value. In this case, the function body is just a ``return`` of the named,
underlying process statistic. No calculations, aggregates, or other processing
are performed.

Converting a metric to a ratio
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Using a very simplified version of the |TS| cache hit reporting, we can
demonstrate taking a metric which expresses the occurrence of one type of event
within a set of possibilities and converting its absolute value into a ratio
of that set's total.

In this example, we assume we have three cache hit states (misses, hits, and
revalidates) and they are tracked in the metrics ``proxy.node.cache.<state>``.
These are not the real metric names in |TS|, and there are much finer grained
reporting states available, but we'll use these for brevity.

.. code:: lua

    float 'proxy.node.cache.hits_ratio' [[
      return
        proxy.node.cache.hits /
        ( proxy.node.cache.hits +
          proxy.node.cache.misses +
          proxy.node.cache.revalidates
        )
    ]]

Further Reading
===============

The following resources may be useful when writing dynamic metrics:

* `Lua Documentation <https://www.lua.org/docs.html>`_
