.. _header-rewrite-plugin:

Header Rewrite Plugin
*********************

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


This is a plugin for Apache Traffic Server that allows you to
modify various headers based on defined rules (operations) on a request or
response. Currently, only one operation is supported.

Using the plugin
----------------

This plugin can be used as both a global plugin, enabled in plugin.config::

  header_rewrite.so config_file_1.conf config_file_2.conf ...

These are global rules, which would apply to all requests. Another option is
to use per remap rules in remap.config::

  map http://a http://b @plugin=header_rewrite.so @pparam=rules1.conf ...

In the second example, hooks which are not to be executed during the remap
phase (the default) causes a transaction hook to be instantiated and used
at a later time. This allows you to setup e.g. a rule that gets executed
during the origin response header parsing, using READ_RESPONSE_HDR_HOOK.
Note that inorder to setup the plugin with rules that are not to be executed
during the remap phase (e.g. SEND_REQUEST_HDR_HOOK, READ_RESPONSE_HDR_HOOK etc),
global hooks must be setup via the below entry in plugin.config ::

  header_rewrite.so

Configuration filenames without an absolute paths are searched for in the
default configuration directory. This is typically where your main
configuration files are, e.g. ``/usr/local/etc/trafficserver``.

Operators
---------

The following operators are available::

  rm-header header-name                      [operator_flags]
  add-header header <value>                  [operator_flags]
  set-header header <value>                  [operator_flags]
  set-status <status-code>                   [operator_flags]
  set-destination [qual] <value>             [operator_flags]
  set-redirect <status-code> <destination>   [operator_flags]
  set-timeout-out <value>                    [operator_flags]
  set-status-reason <value>                  [operator_flags]
  set-config overridable-config <value>      [operator_flags]
  set-conn-dscp <value>                      [operator_flags]
  counter counter-name                       [operator_flags]
  no-op                                      [operator_flags]


Where qual is one of the support URL qualifiers::

  HOST
  PORT
  PATH
  QUERY
  SCHEME
  URL

For example (as a remap rule)::

  cond %{HEADER:X-Mobile} = "foo"
  set-destination HOST foo.mobile.bar.com [L]

Operator flags
--------------

The operator flags are optional, and must not contain whitespaces inside
the brackets. Currently, only one flag is supported::

  [L]   Last rule, do not continue
  [QSA] Append query string

Variable expansion
------------------
Currently only limited variable expansion is supported in add-header. Supported
substitutions include::

  %<proto>      Protocol
  %<port>       Port
  %<chi>        Client IP
  %<cqhl>       Client request length
  %<cqhm>       Client HTTP method
  %<cquup>      Client unmapped URI

Conditions
----------
The conditions are used as qualifiers: The operators specified will
only be evaluated if the condition(s) are met::

  cond %{STATUS} operand                        [condition_flags]
  cond %{RANDOM:nn} operand                     [condition_flags]
  cond %{ACCESS:file}                           [condition_flags]
  cond %{TRUE}                                  [condition_flags]
  cond %{FALSE}                                 [condition_flags]
  cond %{HEADER:header-name} operand            [condition_flags]
  cond %{COOKIE:cookie-name} operand            [condition_flags]
  cond %{CLIENT-HEADER:header-name} operand     [condition_flags]
  cond %{PROTOCOL} operand                      [condition_flags]
  cond %{HOST} operand                          [condition_flags]
  cond %{TOHOST} operand                        [condition_flags]
  cond %{FROMHOST} operand                      [condition_flags]
  cond %{PATH} operand                          [condition_flags]
  cond %{QUERY} operand                         [condition_flags]
  cond %{INTERNAL-TRANSACTION}                  [condition_flags]
  cond %{CLIENT-IP}                             [condition_flags]
  cond %{INCOMING-PORT}                         [condition_flags]
  cond %{METHOD}                                [condition_flags]

The difference between HEADER and CLIENT-HEADER is that HEADER adapts to the
hook it's running in, whereas CLIENT-HEADER always applies to the client
request header. The %{TRUE} condition is also the default condition if no
other conditions are specified.

These conditions have to be first in a ruleset, and you can only have one in
each rule. This implies that a new hook condition starts a new rule as well.::

  cond %{READ_RESPONSE_HDR_HOOK}   (this is the default hook)
  cond %{READ_REQUEST_HDR_HOOK}
  cond %{READ_REQUEST_PRE_REMAP_HOOK}
  cond %{SEND_REQUEST_HDR_HOOK}
  cond %{SEND_RESPONSE_HDR_HOOK}

For remap.config plugin instanations, the default hook is named
REMAP_PSEUDO_HOOK. This can be useful if you are mixing other hooks in a
configuration, but being the default it is also optional.

---------------
Condition flags
---------------

The condition flags are optional, and you can combine more than one into
a comma separated list of flags. Note that whitespaces are not allowed inside
the brackets::

  [NC]  Not case sensitive condition (when applicable) [NOT IMPLEMENTED!]
  [AND] AND with next condition (default)
  [OR]  OR with next condition
  [NOT] Invert this condition

Operands to conditions
----------------------
::

  /string/  # regular expression
  <string   # lexically lower
  >string   # lexically greater
  =string   # lexically equal

The absence of a "matcher" means value exists).

Values
------
Setting e.g. a header with a value can take the following formats:

- Any of the cond definitions, that extracts a value from the request
- $N 0 <= N <= 9, as grouped in a regular expression
- string (which can contain the above)
- null

Examples
--------
::

  cond %{HEADER:X-Y-Foobar}
  cond %{COOKIE:X-DC}  =DC1
  add-header X-Y-Fiefum %{HEADER:X-Y-Foobar}
  add-header X-Forwarded-For %<chi>
  rm-header X-Y-Foobar
  rm-header Set-Cookie
  counter plugin.header_rewrite.x-y-foobar-dc1
  cond %{HEADER:X-Y-Foobar} "Some string" [AND,NC]
