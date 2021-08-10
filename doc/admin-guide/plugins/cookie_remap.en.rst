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

.. _admin-plugins-cookie_remap:

Cookie Based Routing Inside TrafficServer Using cookie_remap
************************************************************


* `Cookie Based Routing Inside TrafficServer Using cookie_remap <#cookie-based-routing-inside-trafficserver-using-cookie_remap>`_

  * :ref:`Features <features>`
  * :ref:`Limitations <limitations>`
  * :ref:`Setup <setup>`
  * :ref:`Operations <operations>`

    * :ref:`Comments <comments>`
    * :ref:`cookie: X|X.Y <cookie-xxy>`
    * :ref:`target: puri <purl>`
    * :ref:`operation: exists|not exists|string|regex|bucket <operation-existsnot-existsstringregexbucket>`
    * :ref:`match: str <match-str>`
    * :ref:`regex: str <regex-str>`
    * :ref:`bucket|hash: X/Y <buckethash-xy>`
    * :ref:`sendto|url: url <sendtourl-url>`
    * :ref:`status: HTTP status-code <status-http-status-code>`
    * :ref:`else: url [optional] <else-url-optional>`
    * :ref:`connector: and <connector-and>`

  * :ref:`Reserved path expressions <reserved-path-expressions>`

    * :ref:`$cr_req_url <cr_req_url-v-15>`
    * :ref:`$cr_urlencode() <cr_urlencode-v-15>`
    * :ref:`$path <path>`
    * :ref:`$unmatched_path <unmatched_path>`

  * :ref:`An example configuration file <an-example-configuration-file>`
  * :ref:`Debugging things <debugging-things>`

    * :ref:`Initial output <initial-output>`

This remap plugin makes decisions about where to send your request based on properties present (or absent) within the HTTP Cookie header.  It can also make decisions based on your uri (url path + query.)

.. _features:

Features
--------

* Also supports sub-level cookies

  * K indicates top-level cookie "K"
  * K.l indicates top-level cookie "K", subfield "l"

* Cookie exists / Cookie doesn't exist
* Cookie or uri matches string
* Cookie or uri matches regex (with match replacement in the sendto like `http://foo.com/$1/$2 <http://foo.com/$1/$2>`_\ )
* Cookie falls into a hash/bucket range
* Can url encode dynamic data

.. _limitations:

Limitations
-----------

* Does not support :ref:`remap-config-plugin-chaining`

.. _setup:

Setup
-----

The plugin is specified in remap.config using a syntax similar to:


.. code-block::

   map http://foo.com http://bar.com @plugin=/usr/bin/trafficserver/libexec64/cookie_remap.so @pparam=/home/trafficserver/conf/cookie_remap/cookie_remap.txt

.. _operations:

Operations
----------

All operations are specified in a YAML configuration file you pass as @pparam on the plugin configuration line. YAML is very simple syntax. See the example configuration files below.

Each operation results in a sendto action. That means that, if matched, cookie_remap forwards the request to the given ``sendto`` url. It can be proxied or redirected. An ``else`` sendto can be specified, in which case a failure to match will forward the request also. Once a sendto is invoked:


* cookie_remap will not process any more "operations" from the configuration
* the default destination specified in the remap.config file will not be used; it will be replaced by the ``sendto``

Each ``operation`` can have multiple "sub-operations", connected with an conjunction operator.  Currently, only the ``and`` operator is supported. So, you can say, "if cookie exists, ``and`` uri is ``x``\ , then redirect."

.. _comments:

Comments
~~~~~~~~


Comments are allowed in the configuration file if they begin with '#'

.. _cookie-xxy:

cookie: X|X.Y
~~~~~~~~~~~~~


This sub-operation is testing against the X cookie or X.Y cookie where X.Y denotes the X cookie, sub cookie Y
e.g

.. code-block:: text

   A=ACOOKIE;B=data&f=fsub&z=zsub;

A will operate on ``ACOOKIE``

B will operate on ``data&f=fsub&z=zsub``

B.f will operate on ``fsub``

.. _purl:

target: puri
~~~~~~~~~~~~


When the cookie key is omitted, the operation is applied to the request uri instead.  If this key and value
is specified, the uri for the operation will be the pre-remapped, rather than the remapped, uri.

.. _operation-existsnot-existsstringregexbucket:

operation: exists|not exists|string|regex|bucket
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


This keyword actually denotes a "suboperation" - it can be specified multiple times, connected with ``and``\ , and send the user to a given destination.


* exists: Test for the existence of a cookie
* not exists: Test for the non-existence of a cookie
* string: string match
* regex: regex matching with $1 - $9 replaced in ``sendto``
* bucket: Hash the cookie and check if it falls in a bucket

The ``cookie`` operator must be specified in the YAML file just before the suboperation that will apply to the cookie.

The ``string``, ``regex`` and ``bucket`` operate on the specified cookie, or if no cookie is specified, they will operate on the uri of the request.

.. _match-str:

match: str
~~~~~~~~~~

Match the cookie data to str.  If no cookie is specified, match to the request uri.

.. _regex-str:

regex: str
~~~~~~~~~~

Sets the regex to str. Matching is enabled with $1-$9 (replaced in sendto).  Note that only the final regex match in an operation will be used to populate the substitutions $1-$9.  If no cookie is specified, match to the request uri.

.. _buckethash-xy:

bucket|hash: X/Y
~~~~~~~~~~~~~~~~

Hashes (bucketizes) the data in Y buckets. If you fall into the first X of them, this suboperation passes. If no cookie is specified, match to the request uri.


.. code-block::

   bucket: 1/100
   will effectively bucketize 1% of your users

.. _sendtourl-url:

sendto|url: url
~~~~~~~~~~~~~~~

if the sub-operation(s) all match, send to url

.. _status-http-status-code:

status: HTTP status-code
~~~~~~~~~~~~~~~~~~~~~~~~

if the sub-operation(s) all match, set the status code (e.g. set it to 302). In the case of a redirect, the sendto URL becomes the redirect URL.

.. _else-url-optional:

else: url [optional]
~~~~~~~~~~~~~~~~~~~~

If one of the sub-operations fails, send to url. This is optional. If there is no 'else', we will continue to process operations until either one succeeds, there is an else in one of the operations or we fall through to the default mapping from remap.config

.. _connector-and:

connector: and
~~~~~~~~~~~~~~

'and' is the only supported connector

.. _reserved-path-expressions:

Reserved path expressions
-------------------------


The following expressions can be used in either the sendto **or** ``else`` URLs, and will be expanded.

.. _cr_req_url-v-15:

$cr_req_url
~~~~~~~~~~~

Replaced with the full original url.

Therefore, a rule like:


.. code-block::

   op:
     cookie: K
     operation: exists
     sendto: http://foo.com/?.done=$cr_req_url


and a request that matches, e.g.


.. code-block::

   http://bar.com/hello?fruit=bananas


will become


.. code-block::

   http://foo.com/?.done=http://bar.com/hello?fruit=bananas


.. _cr_urlencode-v-15:

$cr_urlencode()
~~~~~~~~~~~~~~~

Replaced with a urlencoded version of its argument.  The url argument is aggressively encoded such that all non-alphanumeric characters are converted to % hex notation.  The simple algorithm could probably be refined in the future to be less aggressive (encode fewer characters.)

Therefore, a rule like:


.. code-block::

   op:
     cookie: B
     operation: exists
     sendto: http://foo.com/?.done=$cr_urlencode($cr_req_url)


and a request that matches, e.g.


.. code-block::

   http://bar.com/hello?fruit=bananas


will become


.. code-block::

   http://foo.com/?.done=http%3A%2F%2Fbar%2Ecom@2Fhello%3Ffruit%3Dbananas

.. _path:

$path
~~~~~

$path can be used to replace in either the sendto **or** else URLs the original request path. Therefore a rule like:


.. code-block::

   op:
     cookie: K
     operation: exists
     sendto: http://foo.com/$path/x/y/z


and a request like `http://finance.yahoo.com/photos/what/ever/ <http://foo.com/photos/what/ever/>`_ that matches the rule


.. code-block::

   map http://finance.yahoo.com/photos/ http://newfinance.yahoo.com/1k.html @plugin=cookie_remap.so @pparam=foo.txt


will become `http://foo.com/photos/what/ever/x/y/z <http://foo.com/photos/what/ever/x/y/z>`_

.. _unmatched_path:

$unmatched_path
~~~~~~~~~~~~~~~

$unmatched_path can be used to gather the URL arguments **beyond** what was matched by the remap rule. Therefore a rule like:


.. code-block::

   op:
     cookie: K
     operation: exists
     sendto: http://foo.com/$unmatched_path/x/y/z


and a request like `http://finance.yahoo.com/photos/what/ever/ <http://foo.com/photos/what/ever/>`_ that matches the rule


.. code-block::

   map http://finance.yahoo.com/photos/ http://newfinance.yahoo.com/1k.html @plugin=cookie_remap.so @pparam=foo.txt


will become `http://foo.com/what/ever/matches/x/y/z <http://foo.com/what/ever/matches/x/y/z>`_

Alternatives using pre-remapped URL
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

$cr_req_url, $path and $unamatched_path are based on the remapped URL.  To use the pre-remapped
URL, instead use $cr_req_purl, $ppath and $unmatched_ppath, respectively.

.. _an-example-configuration-file:

An example configuration file
-----------------------------

* first match "wins" (top down)


.. code-block::

   #comments
   #are allowed
   op:
     cookie: K
     operation: exists
     url: http://www.yahoo.com

   op:
     cookie: K
     operation: exists
     connector: and
     cookie: K.l
     operation: not exists
     sendto: http://www.yahoo.com

   op:
     cookie: Y
     operation: bucket
     bucket: 1/1000
     connector: and
     cookie: Y.l
     operation: regex
     regex: (.*)
     connector: and
     cookie: Y.n
     operation: string
     match: foobar
     sendto: http://cnn.com/$1
     else: http://yahoo.com

.. _debugging-things:

Debugging things
----------------

The easiest way to debug problems with this plugin is to run in the following manner:


.. code-block::

   bin/traffic_server -T cookie_remap


which will produce output on startup, and for each request. Be aware that this mode of running trafficserver is **extremely** inefficient and should only be used for debugging.

.. _initial-output:

Initial output
~~~~~~~~~~~~~~

Initially, you will notice output describing each operation and how trafficserver interpreted the information from your configuration file:


.. code-block::

   [Jul  8 13:08:34.183] Server {3187168} DIAG: (cookie_remap) loading cookie remap configuration file from /homes/ebalsa/dev/yts_mods/cookie_remap/example_config.txt
   [Jul  8 13:08:34.199] Server {3187168} DIAG: (cookie_remap) ++++operation++++
   [Jul  8 13:08:34.199] Server {3187168} DIAG: (cookie_remap) sending to: http://finance.yahoo.com/2k.html
   [Jul  8 13:08:34.199] Server {3187168} DIAG: (cookie_remap) if these operations match:
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)     +++subop+++
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)         cookie: B
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)         operation: bucket
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)         bucket: 1/100
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)         taking: 1
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)         out of: 100
   [Jul  8 13:08:34.200] Server {3187168} DIAG: (cookie_remap)     +++subop+++
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap)         cookie: Y.l
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap)         operation: exists
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap) ++++operation++++
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap) sending to: http://X.existed.com
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap) if these operations match:
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap)     +++subop+++
   [Jul  8 13:08:34.201] Server {3187168} DIAG: (cookie_remap)         cookie: PH.l
   [Jul  8 13:08:34.202] Server {3187168} DIAG: (cookie_remap)         operation: exists
   [Jul  8 13:08:34.202] Server {3187168} DIAG: (cookie_remap) # of ops: 2


each operation is enumerated and is more-or-less human readable from the top-down.  From now on, each request has information spit out to the console with debugging information on how the cookie_remap is treating this request.

