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


..  XXX Discribe what the heck this plugin actually does.

Upload proxy specs for phase I:

1. Memory buffering (buffer the entire POST data in IOBuffer before
   connecting to OS) 1.1. Memory buffer size is configured with
   "mem_buffer_size" in config file. Default and minimum value is 32K
   You can increase it in the config file. If the size of a request is
   larger than the "mem_buffer_size" value specifiied in the config
   file, then the upload proxy feature will be disabled for this
   particular request

2. Disk buffering (buffer the entire POST data on disk before connecting
   to OS) 2.1. Use disk async IO. This involved some changes in ATS core
   . new APIs wrapping around ink_aio_read() and ink_aio_write() .
   change to distinguish between api call's AIO and cache's AIO .
   guarantee api call's AIO only involves certain amount of threads .
   the number of threads is configurable in plugin's config file
   (default is 4)

3. 

   2. Directories and files generated on disk . base directory:
      FOOBAR/var/buffer_upload_tmp/ (configurable in config file) .
      number of subdirectories: 64 (configurable in config file) .
      filename are randomly generated . files will be removed when the
      entire data have been sent out to OS . remove dangling files (left
      on disk due to transaction interruption or traffic server crash)
      at startup time

4. 

   3. Default chunk size when reading from disk: 16K, configurable in
      config file

5. Default buffering mode: disk aio buffering mode 3.1. to turn off disk
   buffering, add a "use_disk_buffer 0" line in config file

6. Trigger POST buffering on certain URLs 4.1. certain URLs will be
   provided in a plain text file (one URL each line) 4.2. specify
   filename in config file by "url_list_file" 4.3. max length of each
   URL: 4096 (configurable in config file) 4.4. use exact match, don't
   support regex for now

7. URL conversion for Mail's specific URL format 5.1. for now check if
   the "host" part in the URL is same as the proxy server name, then
   will do this conversion 5.2. To turn on URL conversion feature, set
   "convert_url 1" in config file

8. All request headers inlcuding cookies plus the entire POST data will
   be buffered (either in memory or on disk)

9. Config file can be explicitly sepcified as a parameter in command
   line (in plugin.config file)

a sample config file:

use_disk_buffer 1 convert_url 1 chunk_size 1024 url_list_file
/tmp/url_list.conf max_url_length 10000 base_dir /tmp/test1
subdir_num 100 thread_num 10 mem_buffer_size 40000

default config file: FOOBAR/etc/upload.conf

default config values: use_disk_buffer 1 convert_url 0 chunk_size
16384 url_list_file none max_url_length 4096 base_dir
FOOBAR/var/buffer_upload_tmp subdir_num 64 thread_num 4
mem_buffer_size 32768

