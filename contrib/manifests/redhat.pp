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

# These packages really need to be installed before anything happens.
package {['nfs-utils', 'redhat-lsb']:
  ensure =>latest
}

$source = $lsbmajdistrelease ? {
  '5' => 'http://mirror.pnl.gov/epel/5/i386/epel-release-5-4.noarch.rpm',
  '6' => 'http://mirror.pnl.gov/epel/6/i386/epel-release-6-8.noarch.rpm',
  default => 'http://mirror.pnl.gov/epel/7/i386/epel-release-7-2.noarch.rpm',
}

package {'epel-release':
  source => $source,
  provider => rpm,
  ensure => present,
}

# Base ATS build dependencies.
package {[
    'gcc', 'gcc-c++', 'automake', 'autoconf', 'libtool', 'pkgconfig',
    'openssl-devel', 'tcl-devel', 'expat-devel', 'pcre-devel',
    'ncurses-devel', 'libaio-devel', 'libunwind-devel',
    'hwloc-devel', 'libcap-devel', 'bison', 'flex', 'make',
  ]:
  ensure => latest
}

package { $lsbmajdistrelease ? { '5' => 'curl-devel', default => 'libcurl-devel'}:
  ensure => latest
}

# RHEL/CentOS 5 doesn't have MakeMaker and I don't know what package to use instead :(
if $lsbmajdistrelease != '5' {
  package{'perl-ExtUtils-MakeMaker':
    ensure => latest
  }
}

# development extras.
package {[
    'gdb', 'valgrind', 'git', 'curl', 'screen', 'ccache'
  ]:
  ensure => latest,
  require => Package['epel-release']
}
