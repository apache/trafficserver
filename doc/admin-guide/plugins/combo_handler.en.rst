.. _admin-plugins-combo-handler:

Combo Handler Plugin
********************

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


This plugin provides an intelligent way to combine multiple URLs into a single
URL, and have Apache Traffic Server combine the components into one
response. This is useful for example to create URLs that combine multiple CSS
or Javascript files into one.

Installation
============

This plugin is only built if the configure option ::

    --enable-experimental-plugins

is given at build time. Note that this plugin is built and installed in
combination with the ESI module, since they share common code.

Configuration
=============

The arguments in the :file:`plugin.config` line in order represent

1. The path that should triggers combo handler (defaults to
   "admin/v1/combo")

2. The name of the key used for signature verification (disabled by
   default)

3. A colon separated list of headers which, if present on at least one response, will be
   added to the combo response.

4. The path of a config file with allowed content types of objects to be combined, one per
   line, without parameters. (Blank lines and comments starting with "#" are ignored.)
   Parameters in the Content-Type field value will be ignored when
   checking if they appear in the allowed types.  If the path does not start with "/", the
   config file must be located in the ATS config directory.  By default, all content types
   are allowed, but if this file is specified, it must contain at least one content type.

A "-" can be supplied as a value for any of these arguments to request
default value be applied.

Also, just like the original combohandler, this plugin generates URLs of
the form ``http://localhost/<dir>/<file-path>``. ``<dir>`` here defaults
to ``l`` unless specified by the file path in the query parameter using
a colon. For example::

    http://combo.com/admin/v1/combo?filepath1&dir1:filepath2&filepath3

Will result in these three pages being fetched::

    http://localhost/l/filepath1
    http://localhost/dir1/filepath2
    http://localhost/l/filepath3

Remap rules have to be specified to map the above URLs to desired
content servers.

The plugin also supports a prefix parameter. Common parts of successive
file paths can be extracted and specified separately using a 'p' query
parameter. Successive file path parameters are appended to this prefix
to create complete file paths. The prefix will remain active until
changed or cleared (set to an empty string). For example, the query ::

    "/file1&p=/path1/&file2&file3&p=&/file4&p=/dir:path2/&file5&file6"

results in these file paths being "reconstructed"::

    /file1
    /path1/file2
    /path1/file3
    /file4
    /dir:path2/file5
    /dir:path2/file6

Caching
=======
Combohandler follows a few rules for the "Cache-Control" header:

1) All requested documents must have "immutable" for the combo'd
   response to also have "immutable".

2) If one or more requested documents has "private" set, then the combo'd
   response will also have "private". If no requested documents have a
   publicity setting, then the default is "public".

3) The "max-age" value will be set to the smallest of all the requested "max-age"
   values. If no documents has "max-age" set, then the default is 10 years.
