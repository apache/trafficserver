#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

# Base ATS build dependencies.
package {[
    'gcc', 'g++', 'automake', 'autoconf', 'libtool', 'pkg-config',
    'libssl-dev', 'tcl-dev', 'libexpat1-dev', 'libpcre3-dev', 'libhwloc-dev',
    'libcurl3-dev', 'libncurses5-dev', 'libaio-dev',
    'libcap-dev', 'libcap2', 'bison', 'flex', 'make',
    'libmodule-install-perl', 'libunwind8-dev'
  ]:
  ensure => latest
}

# Development extras.
package {[
    'gdb', 'valgrind', 'git', 'ack-grep', 'curl', 'tmux', 'screen',
    'ccache', 'python-sphinx', 'doxygen',

    # For parsing Doxygen XML output, to add links from API descriptions
    # to the source code for that object
    'python-lxml'
  ]:
  ensure => latest
}

# if there is clang-3.4 available, install it:
if $::lsbdistcodename == 'saucy' {
  package {[
      'clang-3.4', 'clang-format-3.4'
    ]:
    ensure => latest,
  }
}
