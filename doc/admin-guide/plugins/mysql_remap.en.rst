.. _admin-plugins-mysql-remap:

MySQL Remap Plugin
******************

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

.. Note::

    This plugin is *deprecated* as of v9.0.0, and should not be used! The issue
    is around the blocking APIs used here; If this functionality is needed, you
    will need to implement it using something non-blocking, such as REDIS.


This is a basic plugin for doing dynamic "remaps" from a database. It
essentially rewrites the incoming request's Host header / origin server
connection to one retrieved from a database.

The generic proxying setup is the following::

    UA ----> Traffic Server ----> Origin Server

Without the plugin a request like::

    GET /path/to/something HTTP/1.1
    Host: original.host.com

Ends up requesting ``http://original.host.com/path/to/something``

With this plugin enabled, you can easily change that to anywhere you
desire. Imagine the many possibilities....

We have benchmarked the plugin with ab at about 9200 requests/sec (1.7k
object) on a commodity hardware with a local setup of both, MySQL and
Traffic Server local. Real performance is likely to be substantially
higher, up to the MySQL's max queries / second.

Installation
============

This plugin is only built if the configure option ::

    --enable-experimental-plugins

is given at build time.

Configuration
=============

Import the default schema to a database you create::

    mysql -u root -p -e "CREATE DATABASE mysql_remap;"   # create a new database
    mysql -u root -p mysql_remap < schema/import.sql     # import the provided schema

insert some interesting values in mysql_remap.hostname &
mysql_remap.map

Traffic Server plugin configuration is done inside a global
configuration file: ``/etc/trafficserver/plugin.config``::

    mysql_remap.so /etc/trafficserver/mysql_remap.ini

The INI file should contain the following values::

    [mysql_remap]
    mysql_host     = localhost   #default
    mysql_port     = 3306        #default
    mysql_username = remap_user
    mysql_password =
    mysql_database = mysql_remap #default

To debug errors, start trafficserver manually using::

    traffic_server -T "mysql_remap"

And resolve any errors or warnings displayed.
