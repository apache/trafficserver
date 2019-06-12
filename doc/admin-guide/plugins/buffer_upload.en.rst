.. _admin-plugins-buffer-upload:

Buffer Upload Plugin
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

The Buffer Upload plugin offers the following features

Installation
============

Configuration can be explicitly specified as a parameter in ``plugin.config`` ::

    buffer_upload.so /FOOBAR/upload.conf

Memory buffering (buffer the entire POST data in IOBuffer before connecting to OS)
==================================================================================

Memory buffer size is configured with "mem_buffer_size" in config file. Default and minimum value is 32K. You can
increase it in the config file. If the size of a request is larger than the "mem_buffer_size" value specified in the
config file, then the upload proxy feature will be disabled for this particular request

Disk buffering (buffer the entire POST data on disk before connecting to OS)
============================================================================

1. Disk async IO is used. AIO api call only involves certain amount of threads. The number of threads is configurable in
plugin's config file (default is 4)

2. Directories and files are generated on disk . Base directory is /FOOBAR/var/buffer_upload_tmp/ (configurable in
config file). Number of subdirectories is 64 (configurable in config file). Filename are randomly generated. Files will
be removed when the entire data have been sent out to OS . At startup time, dangling files are removed (left on disk due
to transaction interruption or traffic server crash)

3. Default chunk size when reading from disk is 16K, configurable in config file

Trigger POST buffering on certain URLs
======================================

1. Certain URLs will be provided in a plain text file (one URL each line)
2. Specify filename in config file by "url_list_file"
3. max length of each URL is 4096 (configurable in config file)
4. use exact match, don't support regex for now

Other Features
==============

1. Default buffering mode is disk aio buffering mode. To turn off disk buffering, add a "use_disk_buffer 0" line in
config file

2. All request headers including cookies plus the entire POST data will be buffered (either in memory or on disk)

Configuration File
==================

sample config file ::

    use_disk_buffer 1
    convert_url 1
    chunk_size 1024
    url_list_file /tmp/url_list.conf
    max_url_length 10000
    base_dir /tmp/test1
    subdir_num 100
    thread_num 10
    mem_buffer_size 40000

