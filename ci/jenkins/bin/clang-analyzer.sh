#!/bin/sh
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


checkers="-enable-checker alpha.unix.cstring.BufferOverlap \
         -enable-checker alpha.unix.PthreadLock\
         -enable-checker alpha.security.ArrayBoundV2 \
         -enable-checker alpha.core.BoolAssignment \
         -enable-checker alpha.core.CastSize \
         -enable-checker alpha.core.SizeofPtr"

# These shenanigans are here to allow it to run both manually, and via Jenkins
test -z "${ATS_MAKE}" && ATS_MAKE="make"
test ! -z "${WORKSPACE}" && cd "${WORKSPACE}/src"

# This disables LuaJIT for now, to avoid all the warnings from it. Maybe we need
# to talk to the author of it, or ideally, figure out how to get clang-analyzer to
# ignore them ?
autoreconf -fi
./configure --enable-experimental-plugins --enable-cppapi --disable-luajit
scan-build ${checkers} --status-bugs -o /home/jenkins/clang-analyzer --html-title="ATS master branch"  ${ATS_MAKE} -j5
status=$?

${ATS_MAKE} distclean

# Cleanup old reports (save the last 10 reports)
cd /home/jenkins/clang-analyzer || exit -1
for old in $(\ls -1t | tail -n +11); do
    rm -rf $old
done

# Setup the symlink to the latest report
rm -f latest
ln -s $(\ls -1t | head -1) latest

# Exit with the scan-build exit code (thanks to --status-bugs)
exit $status
