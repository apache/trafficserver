################################################################################
#  Dockerfile to setup a Debian / Ubuntu type docker image, suitable
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
# These can (should?) be overridden from the command line with ----build-arg, e.g.
#    docker build --build-arg OS_VERSION=19.10 --build-arg OS_TYPE=ubuntu
#
ARG OS_VERSION=19.10
ARG OS_TYPE=ubuntu

# This does currently not work, e.g. this I'd expect to make it conditional:
#
#    RUN if [ "${ADD_JENKINS}" = "yes" ]; then apt-get ...
#
ARG ADD_JENKINS=no

# Alright, lets pull in the base image from docker.io
FROM ${OS_TYPE}:${OS_VERSION}

################################################################################
# All the Debian packages necessary for building ATS. you do not need all
# these if you only intend to run ATS!
#
# This runs all the DEB installations, starting with a system level update. tzdata
# needs to be installed, because TCL needs it, and we need this non-interactive.
# ToDo: Remove tzdata when TCL is fully gone
RUN apt-get update; apt-get -y dist-upgrade; \
    DEBIAN_FRONTEND=noninteractive apt-get -y install tzdata apt-utils; \
    # Compilers
    apt-get -y install ccache make pkgconf bison flex g++ clang gettext libc++-dev \
    # Autoconf
    autoconf automake libtool autotools-dev dh-apparmor debhelper \
    # Various other tools
    git distcc file wget openssl hwloc intltool-debian; \
    # Devel packages that ATS needs
    apt-get -y install libssl-dev libexpat1-dev libpcre3-dev libcap-dev \
    libhwloc-dev libhwloc5 libunwind8 libunwind8-dev \
    tcl-dev tcl8.6-dev libjemalloc-dev libluajit-5.1-dev liblzma-dev \
    libhiredis-dev libbrotli-dev libncurses-dev libgeoip-dev libmagick++-dev; \
    # Optional: This is for the OpenSSH server, and Jenkins account + access (comment out if not needed)
    apt-get -y install openssh-server openjdk-8-jre && mkdir /run/sshd; \
    groupadd  -g 665 jenkins && \
    useradd -g jenkins -u 989 -s /bin/bash -M -d /home/jenkins -c "Jenkins Continuous Build server" jenkins && \
    mkdir -p /var/jenkins && chown jenkins.jenkins /var/jenkins

# This is for autest stuff, skipping since it's pulling in a *lot* ...
# apt-get -y install python3 httpd-tools procps-ng nmap-ncat pipenv \
# python3-virtualenv python3-gunicorn python3-requests python3-httpbin; \
