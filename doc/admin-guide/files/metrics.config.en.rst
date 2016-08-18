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

Support Functions
-----------------

Several supporting functions are defined in the default configuration file.
Existing dynamic metrics shipped with :file:`metrics.config` make extensive use
of these functions, and your own custom metrics may as necessary, too.

cluster(name)
~~~~~~~~~~~~~

Returns the sum of metric ``name`` for the entire cluster of which the current
node is a member. Memoization is used to avoid additional cost from calling
this function multiple times within a single metrics pass. The ``name`` must be
a metric within the node scope.

mbits(bytes)
~~~~~~~~~~~~

Converts and returns ``bytes`` as megabits (``bytes * 8 / 1000000``).

mbytes(bytes)
~~~~~~~~~~~~~

Converts and returns ``bytes`` as mebibytes (``bytes / (1024 * 1024)``).

now()
~~~~~

Returns the current node's time in milliseconds-from-epoch.

rate_of(msec, key, fn)
~~~~~~~~~~~~~~~~~~~~~~

Returns the rate of change over a period of ``msec`` milliseconds for the
metric value of ``key`` (obtained by invoking the function ``fn``).

This is accomplished by effectively snapshotting the value of the metric at the
beginning and end of the given period expressed by ``msec``, multiplying their
difference by 1,000 and dividing that by ``msec``.

rate_of_10s(key, fn)
~~~~~~~~~~~~~~~~~~~~

Returns the rate of change for the past 10 seconds for the metric ``key``, as
calculated by function ``fn``. This function simply wraps ``rate_of`` and
supplies an ``msec`` value of ``10 * 1000``.

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

Returning a rate-of-change
~~~~~~~~~~~~~~~~~~~~~~~~~~

Slightly more involved than just returning a point-in-time value from a given
statistic is calculating the rate of change:

.. code:: lua

    integer 'proxy.node.dns.lookups_per_second' [[
      local self = ...

      return rate_of_10s(self,
        function() return proxy.process.dns.total_dns_lookups end
      )
    ]]

Similar to the previous example, we are returning another metric's value, but
in this case we do so within a function that we're passing into
``rate_of_10s``. This function, explained earlier, wraps ``rate_of`` which
tracks the given metric over a specific interval and returns the average
per-second rate of change, obtaining the values it uses to calculate this rate
by invoking the function passed to it.

Calculating a rate-of-change's delta
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A more complicated example involves calculating the variance in the rate of
change of an underlying statistic over a given period of time. This is not an
average of a statistic, nor is it just the raw delta between two samplings of
that statistic, and while inappropriate to know *how much* of an event has
occurred, it is useful to know how erratic or unstable the frequency of that
event occurring is.

In other words, a large absolute value indicates a deviance from the usual
pattern of behavior/activity. For example, if your |TS| cache (using the
example dynamic metric function below) sees between 10,000 and 10,250 HostDB
hits every 10 seconds, the value returned by this metric will remain fairly
small. If all of a sudden 50,000 hits make it to HostDB in the span of that
same averaging interval, this value will increase significantly. This could
then be used to trigger various alerts that something might be up with HostDB
lookups on the |TS| cluster.

.. code:: lua

    integer 'proxy.node.hostdb.total_hits_avg_10s' [[
      local self = ...

      return interval_delta_of_10s(self,
        function() return proxy.process.hostdb.total_hits end
      )
    ]]

The catch is that if the dramatic increase is actually the new norm, the metric
will return to emitting small absolute values again - even though the statistic
underneath is now consistently and significantly higher or lower than it used
to be. If what you are trying to measure, though, is the stability of a metric
that's, long-term, a good thing.

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

Summing across a cluster
~~~~~~~~~~~~~~~~~~~~~~~~

When running a |TS| cluster of multiple nodes, there are many metrics which are
useful to see at both the node and cluster level. Dynamic metrics make it very
easy to collect the metric's value for every node in the cluster and return the
sum, as seen here with cache connections:

.. code:: lua

    counter 'proxy.cluster.http.cache_current_connections_count' [[
      return cluster('proxy.node.http.cache_current_connections_count')
    ]]

Further Reading
===============

The following resources may be useful when writing dynamic metrics:

* `Lua Documentation <https://www.lua.org/docs.html>`_
