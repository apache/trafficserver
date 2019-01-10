################################################################################
#  Dockerfile to setup a CentOS / Fedora type docker image, suitable
#  for building ATS, perhaps as part of a Jenkins CI. Note that some
#  of the features in here are specific to the official ATS Jenkins
#  setup, see comment below.
#
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

################################################################################
# These can (should?) be overriden from the command line with ----build-arg, e.g.
#    docker build --build-arg OS_VERSION=7 --build-arg OS_TYPE=centos
#
ARG OS_VERSION=28
ARG OS_TYPE=fedora

# This does currently not work, e.g. this I'd expect to make it conditional:
#
#    RUN if [ "${ADD_JENKINS}" = "yes" ]; then yum ...
#
ARG ADD_JENKINS=no

# Alright, lets pull in the base image from docker.io
FROM ${OS_TYPE}:${OS_VERSION}

################################################################################
# All the DNF / YUM packages necessary for building ATS. you do not need all
# these if you only intend to run ATS, see the .spec file for the required
#runtime packages.

# This runs all the yum installation, starting with a system level update
RUN yum -y update; \
    # Compilers
    yum -y install ccache make pkgconfig bison flex gcc-c++ clang \
    # Autoconf
    autoconf automake libtool \
    # Various other tools
    git rpm-build distcc-server file wget openssl hwloc; \
    # Devel packages that ATS needs
    yum -y install openssl-devel expat-devel pcre-devel libcap-devel hwloc-devel libunwind-devel \
    xz-devel libcurl-devel ncurses-devel jemalloc-devel GeoIP-devel kyotocabinet-devel luajit-devel \
    brotli-devel ImageMagick-devel ImageMagick-c++-devel \
    perl-ExtUtils-MakeMaker perl-Digest-SHA perl-URI; \
    # This is for autest stuff
    yum -y install python3 python3-virtualenv python-virtualenv httpd-tools procps-ng nmap-ncat; \
    # Optional: This is for the OpenSSH server, and Jenkins account + access (comment out if not needed)
    yum -y install java openssh-server && ssh-keygen -A; rm -f /run/nologin; \
    groupadd  -g 665 jenkins && \
    useradd -g jenkins -u 989 -s /bin/bash -M -d /home/jenkins -c "Jenkins Continuous Build server" jenkins && \
    mkdir -p /var/jenkins && chown jenkins.jenkins /var/jenkins

# Check if devtoolset-7 is required
RUN  if [ ! -z "$(grep -i centos /etc/redhat-release)" ]; then \
    yum -y install centos-release-scl; \
    yum -y install devtoolset-7 devtoolset-7-libasan-devel; \
    fi
