.. _sdk-preface:

Preface
********

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

.. toctree::
   :maxdepth: 2

   preface/how-to-use-this-book.en
   preface/typographical-conventions.en

The *Traffic Server Software Developer's Kit* is a reference for
creating plugins. **Plugins** are programs that add services (such as
filtering or content transformation) or entire features (such as new
protocol support) to Traffic Server. If you are new to writing Traffic
Server plugins, then read the first two chapters, :ref:`sdk-getting-started`
and :ref:`how-to-create-trafficserver-plugins`, and use the
remaining chapters as needed. :ref:`header-based-plugin-examples` provides details about
plugins that work on HTTP headers, while :ref:`http-transformation-plugin` explains how to write a
plugin that transforms or scans the body of an HTTP response. If you
want to support your own protocol on Traffic Server, then reference :ref:`new-protocol-plugins`.

Audience
--------

This manual is intended for programmers who want to write plugin
programs that add services or features to Traffic Server. It assumes a
cursory knowledge of the C programming language, Hyper-Text Transfer
Protocol (HTTP), and Multipurpose Internet Mail Extensions (MIME).

