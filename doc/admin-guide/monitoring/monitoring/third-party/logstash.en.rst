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

.. include:: ../../../../common.defs

.. _admin-monitoring-logstash:

Logstash
********

`Logstash <https://www.elastic.co/products/logstash>`_ is a powerful, open
source, unstructured data processing program that can accept text data from
many different sources (directly over TCP/UDP, via Unix sockets, or by reading
in files from disk for example), in many different formats and transform those
inputs into structured, searchable documents.

One of the most common use cases is to take text based system and application
logs, extract individual fields (e.g. host names, error codes, timing data, and
so on), and make the data available in Elasticsearch for searching and
reporting.

In this guide, we will cover the basics of getting your |TS| log data into
Logstash. Going the next step and building fancy Kibana dashboards on top of
that is currently left as an exercise for the reader.

|TS| Log Formats
================

|TS| provides a very flexible set of logging outputs. Almost any format can be
constructed. The full range of options is covered in the
:ref:`admin-monitoring-logging` chapter.

This guide will walk you through using the appropriate filters in Logstash for
the common logging formats in |TS|. If you have constructed your own custom log
formats, you will need to build upon these examples and refer to the Logstash
documentation to produce custom filters capable of parsing your own formats.

Logstash Input
==============

For the on-disk logs produced by |TS|, you will want to use Logstash's ``file``
input plugin. Note that your logs must be in ``ASCII`` format, not ``binary``,
for the plugin to work.

Assuming that your |TS| event logs are named ``access-<rotationtimestamp>.log``
and stored at ``/var/log/trafficserver/``, the following Logstash input
configuration should work::

    input {
      file {
        path => /var/log/trafficserver/access-*.log
      }
    }

Logstash provides some additional tweaking options, which are explained in the
`file plugin documentation <https://www.elastic.co/guide/en/logstash/current/plugins-inputs-file.html>`_
but the above provides the bare minimum required to have Logstash read log data
from local disks.

Logstash Filters
================

The `grok filter <https://www.elastic.co/guide/en/logstash/current/plugins-filters-grok.html>`_
in Logstash allows you to completely tailor the parsing of your source data and
extract as many or as few fields as you like.

Some patterns are already built and can be used very easily. If you have built
custom log formats for |TS|, you may need to write your own patterns, however.

Squid Compatible
----------------

The :ref:`admin-logging-format-squid` log format includes, unsurprisingly, a
few useful fields for proxy servers. Using the following grok pattern will
extract this information from your |TS| logs if you employ the Squid compatible
log format::

    filter {
      grok {
        match => { "message" => "%{NUMBER:timestamp} %{NUMBER:timetoserve} %{IPORHOST:clientip} %{WORD:cachecode}/%{NUMBER:response} %{NUMBER:bytes} %{WORD:verb} %{NOTSPACE:request} %{USER:auth} %{NOTSPACE:route} %{DATA:contenttype}" }
      }
      date {
        match => [ "timestamp", "UNIX" ]
      }
    }

The resulting structured document will contain the following fields:

=========== ===================================================================
Field       Description
=========== ===================================================================
timestamp   Date and time of the client request.
timetoserve Time, in seconds, from initial client connection to |TS| until the
            last byte has been sent back to client from |TS|.
clientip    Client IP address or hostname.
cachecode   :ref:`admin-monitoring-logging-cache-result-codes`.
response    HTTP response status code sent by |TS| to the client.
bytes       Length, in bytes, of the |TS| response to the client, including
            headers.
verb        HTTP method (e.g. ``GET``, ``POST``, etc.) of the client request.
request     URL specified by the client request.
auth        Authentication username supplied by the client, if present.
route       Proxy hierarchy route; the route used by |TS| to retrieve the cache
            object.
contenttype Content type of the response.
=========== ===================================================================

Netscape Common
---------------

If your |TS| instance is already outputting :ref:`admin-logging-format-common`
format logs, then Logstash's ``COMMONAPACHELOG`` pattern will handle your logs
out of the box. Add the following filter block to your Logstash configuration::

    filter {
      grok {
        match => { "message" => "%{COMMONAPACHELOG}" }
      }
    }

This will produce a structured document for each log entry with the following
fields:

=========== ===================================================================
Field       Description
=========== ===================================================================
clientip    Client IP address or hostname.
ident       Always a literal ``-`` character for |TS| logs.
auth        The authentication username for the client request. A ``-`` means
            no authentication was required (or supplied).
timestamp   The date and time of the client request.
verb        HTTP method used for the request (e.g. ``GET``, ``POST``, etc.).
request     URL specified by the client request.
httpversion HTTP version (e.g. ``1.1``) used by the client.
rawrequest  *See note below.*
response    HTTP status code used for |TS| response (not the origin's response
            code).
bytes       Length of |TS| response to client, in bytes.
=========== ===================================================================

.. note::

   ``rawrequest`` is populated when the usual ``"<verb> <request> http/<httpversion>"``
   pattern was not matched. In that event, those three fields will be missing
   from the document, and instead ``rawrequest`` will have the original string.

Netscape Extended
-----------------

The following pattern adds to Common Apache to support the additional fields
found in :ref:`admin-logging-format-extended`::

    filter {
      grok {
        match => { "message" => "%{COMMONAPACHELOG} %{NUMBER:originstatus} %{NUMBER:originrespbytes} %{NUMBER:clientreqbytes} %{NUMBER:proxyreqbytes} %{NUMBER:clienthdrbytes} %{NUMBER:proxyresphdrbytes} %{NUMBER:proxyreqhdrbytes} %{NUMBER:originhdrbytes} %{NUMBER:timetoserve}" }
      }
    }

Because this starts out with the ``COMMONAPACHELOG`` pattern, you will get all
of the fields mentioned in `Netscape Common`_ above, as well as the following:

================= =============================================================
Field             Description
================= =============================================================
originstatus      HTTP status code returned by origin server.
originrespbytes   Body length, in bytes, of origin's response to |TS|.
clientreqbytes    Body length, in bytes, of client request to |TS|.
proxyreqbytes     Body length, in bytes, of |TS| request to origin.
clienthdrbytes    Header length, in bytes, of client request to |TS|.
proxyresphdrbytes Header length, in bytes, of |TS| response to client.
proxyreqhdrbytes  Header length, in bytes, of |TS| request to origin.
originhdrbytes    Header length, in bytes, of origin's response to |TS|.
timetoserve       Time, in seconds, from initial client connection to |TS|
                  until the last byte has been sent back to client from |TS|.
================= =============================================================

Further Reading
===============

* `Logstash Documentation <https://www.elastic.co/guide/en/logstash/current/index.html>`_

* `Grok Patterns <https://github.com/logstash-plugins/logstash-patterns-core/tree/master/patterns>`_

* `Elasticsearch Documentation <https://www.elastic.co/guide/en/elasticsearch/reference/current/index.html>`_
