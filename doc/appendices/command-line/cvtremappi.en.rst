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

.. _cvtremappi:

cvtremappi
**********

Description
===========

To help convert your remapping configuration from pre-ATS9 to ATS9 and later.  It may be useful if you use any
of the core plugins regex_remap.so, header_rewrite.so or gzip.so.  (For this script to work, the python3
command has to be in your path.)  You can specify where your remap configuration file is with the option:

--filepath FILEPATH

If this parameter is omitted, it defaults to ``./remap.config`` .  The script will make necessary modifications
to this file, and any files it includes with ``.include`` .  It will change `@plugin=gzip.so` to its new name,
`@plugin=compress.so` .  When regex_remap.so is invoked as the first remap plugin, it will add the parameter
@pparam=pristine .  (This makes it work the same as in pre-9 ATS, where the request URL is the pre-remapping
URL for the first plugin for a remap rule.)  When `header_rewrite.so` is used as a remap plugin, no changes
are needed in the remap configuration line invoking it.  However, changes may be necessary to the
configuration files passed to it as parameters.  If a header rewrite configuration file is used for both the
invocation of header rewrite as the first plugin for remap rules, and for other invocations, it may be
necessary to generate two new versions of it.  In these cases, the prefix `1st-` is added to file's name,
for the version used with header rewrite as the first plugin.  If you prefer that a different prefix be added,
you can specify it with this option:

--prefix PREFIX

If you are also using header rewrite as a global plugin, you should also provide the filepath of the global
plugin configuration file with this option:

--plugin PLUGIN

(Note that, if the PLUGIN filepath is relative, it should be relative to the directory containing the remap
configuration file, not relative to the directory the script is run from.  Note also that, if relative paths
for include files for header rewrite config files appear in the configuration files, they are assumed to be
relative to the directory containing the remap configuration file.)

Header rewrite previously had some logic that has been eliminated in ATS9.  If a line in a header rewrite
configuration file relies on this deprecated logic, an error message will be output to standard error.  The
text `ERROR:` will be prepended to the line in the configuration file causing the error.

The script writes, one per line, a list of the files it is changing or creating to the standard output.  But
both new and changed files will be written into entirely new files with the suffix `.new` added to the filepath.
For example, if `remap.config` is changed by the script, it will put the changed version of the file in
`remap.config.new` .  This gives you a chance to review the changes the script has made.  You can then put the
changed files into effect with the tool script `insnew`.  This script reads a list of filepaths, one per line,
from the standard input.  For each filepath `FP`, if it specifies an existing file, it will rename it to
`FP.old`.  It will then rename the file `FP.new` to `FP`.  This second script should be run from the same
current directory as the first script was run from.
