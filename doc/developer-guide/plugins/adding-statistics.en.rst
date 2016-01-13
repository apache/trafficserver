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

.. _developer-plugins-add-statistics:

Adding Statistics
*****************

This chapter describes how to add statistics to your plugins. Statistics
can be coupled or uncoupled. *Coupled* statistics are quantities that
are related and must therefore be updated together. The Traffic Server
API statistics functions add your plugin's statistics to the Traffic
Server statistics system. You can view your plugin statistics as you
would any other Traffic Server statistic, using Traffic Line (Traffic
Server's command line interface). This chapter contains the following
topics:

Uncoupled Statistics
====================

A statistic is an object of type ``TSStat``. The value of the statistic
is of type ``TSStatType``. The possible ``TSStatTypes`` are:

-  ``TSSTAT_TYPE_INT64``

-  ``TSSTAT_TYPE_FLOAT``

There is *no* ``TSSTAT_TYPE_INT32``.

To add uncoupled statistics, follow the steps below:

1. Declare your statistic as a global variable in your plugin. For
   example:

   .. code-block:: c

      static TSStat my_statistic;

2. In ``TSPluginInit``, create new statistics using ``TSStatCreate``.
   When you create a new statistic, you need to give it an "external"
   name that the Traffic Server command line interface (Traffic Line)
   uses to access the statistic. For example:

   .. code-block:: c

      my_statistic = TSStatCreate ("my.statistic", TSSTAT_TYPE_INT64);

3. Modify (increment, decrement, or other modification) your statistic
   in plugin functions.

Coupled Statistics
==================

Use coupled statistics for quantities that are related and therefore
must be updated jointly.

As a very simple example, suppose you have three statistics: ``sum``,
``part_1``, and ``part_2``. They must always preserve the relationship
that ``sum = part_1  + part_2``. If you update ``part_1`` without
updating ``sum`` at the same time, then the equation becomes untrue.
Therefore, the statistics are said to be *coupled*.

The mechanism for updating coupled statistics jointly is to create local
copies of global coupled statistics in the routines that modifiy them.
When each local copy is updated appropriately, do a global update using
``TSStatsCoupledUpdate``. To specify which statistics are related to one
another, establish a coupled statistic category and make sure that each
coupled statistic belongs to the appropriate category. When it is time
to do the global update, specify the category to be updated.

.. note::

   The local statistic copy must have a duplicate set of statistics as that
   of the master copy. Local statistics must also be added to the local
   statistic category in the same order as their master copy counterparts
   were originally added.

Below are the steps you need to follow, along with a code example taken
from the ``redirect-1.c`` sample plugin.

To add coupled statistics
-------------------------

1. Declare the global category for your coupled statistics as a global
   ``TSCoupledStat`` variable in your plugin.

2. Declare your coupled statistics as global ``TSStat`` variables in
   your plugin.

3. In ``TSPluginInit``, create a new global coupled category using
   ``TSStatCoupledGlobalCategoryCreate``.

4. In ``TSPluginInit``, create new global coupled statistics using
   ``TSStatCoupledGlobalAdd``. When you create a new statistic, you need
   to give it an "external" name that the Traffic Server command line
   interface (Traffic Line) uses to access the statistic.

5. In any routine wherein you want to modify (increment, decrement, or
   other modification) your coupled statistics, declare local copies of
   the coupled category and coupled statistics.

6. Create local copies using ``TSStatCoupledLocalCopyCreate`` and
   ``TSStatCoupledLocalAdd``.

7. Modify the local copies of your statistics. Then call
   ``TSStatsCoupledUpdate`` to update the global copies jointly.

8. When you are finished, you must destroy all of the local copies in
   the category via ``TSStatCoupledLocalCopyDestroy``.

Example Using the redirect-1.c Sample Plugin
--------------------------------------------

.. code-block:: c

    static TSCoupledStat request_outcomes;

    static TSStat requests_all;
    static TSStat requests_redirects;
    static TSStat requests_unchanged;

    request_outcomes = TSStatCoupledGlobalCategoryCreate ("request_outcomes"); 

    requests_all = TSStatCoupledGlobalAdd (request_outcomes, "requests.all", TSSTAT_TYPE_FLOAT);
    requests_redirects = TSStatCoupledGlobalAdd (request_outcomes, "requests.redirects",
        TSSTAT_TYPE_INT64);
    requests_unchanged = TSStatCoupledGlobalAdd (request_outcomes, "requests.unchanged", 
        TSSTAT_TYPE_INT64);

    TSCoupledStat local_request_outcomes;
    TSStat local_requests_all;
    TSStat local_requests_redirects;
    TSStat local_requests_unchanged;

    local_request_outcomes = TSStatCoupledLocalCopyCreate("local_request_outcomes", 
        request_outcomes); 
    local_requests_all = TSStatCoupledLocalAdd(local_request_outcomes, "requests.all.local", 
        TSSTAT_TYPE_FLOAT);
    local_requests_redirects = TSStatCoupledLocalAdd(local_request_outcomes, 
        "requests.redirects.local", TSSTAT_TYPE_INT64);
    local_requests_unchanged = TSStatCoupledLocalAdd(local_request_outcomes, 
        "requests.unchanged.local", TSSTAT_TYPE_INT64);

    TSStatFloatAddTo( local_requests_all, 1.0 ) ; 
    ...
    TSStatIncrement (local_requests_unchanged); 
    TSStatsCoupledUpdate(local_request_outcomes); 

    TSStatCoupledLocalCopyDestroy(local_request_outcomes); 

Viewing Statistics Using Traffic Line
=====================================

.. XXX: This documentation seems to be duplicated from the admin docs.

To view statistics for your plugin, follow the steps below:

1. Make sure you know the name of your statistic (i.e., the name used in
   the ``TSStatCoupledGlobalAdd``, ``TSStatCreate``, or
   ``TSStatCoupledGlobalCategoryCreate`` call).

2. In your ``<Traffic Server>/bin`` directory, enter the following:

   ::

       ./traffic\_line -r the\_name

