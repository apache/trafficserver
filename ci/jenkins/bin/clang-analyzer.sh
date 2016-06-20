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


# This disables LuaJIT for now, to avoid all the warnings from it. Maybe we need
# to talk to the author of it, or ideally, figure out how to get clang-analyzer to
# ignore them ?

# Where are our LLVM tools?
LLVM_BASE=${LLVM:-/opt/llvm}

# Options
options="--status-bugs --keep-empty"
configure="--enable-experimental-plugins --enable-cppapi"

# Additional checkers
# Phil says these are all FP's: -enable-checker alpha.security.ArrayBoundV2
checkers="-enable-checker alpha.unix.cstring.BufferOverlap \
          -enable-checker alpha.unix.PthreadLock\
          -enable-checker alpha.core.BoolAssignment \
          -enable-checker alpha.core.CastSize \
          -enable-checker alpha.core.SizeofPtr"


# These shenanigans are here to allow it to run both manually, and via Jenkins
test -z "${ATS_MAKE}" && ATS_MAKE="make"
test ! -z "${WORKSPACE}" && cd "${WORKSPACE}/src"

# Where to store the results, special case for the CI
output="/tmp"
test -d "/home/jenkins/clang-analyzer" && output="/home/jenkins/clang-analyzer"

autoreconf -fi
#scan-build ./configure ${configure}
./configure ${configure} CC=${LLVM_BASE}/bin/clang CXX=${LLVM_BASE}/bin/clang++
${LLVM_BASE}/bin/scan-build ${checkers} ${options} -o ${output} --html-title="ATS master branch"  ${ATS_MAKE} -j4
status=$?

${ATS_MAKE} distclean

# Cleanup old reports (save the last 10 reports), but only for the CI
if [ "/tmp" !=  "$output" ]; then
    cd ${output} || exit -1
    for old in $(/usr/bin/ls -1t | tail -n +11); do
	rm -rf $old
    done

    # Setup the symlink to the latest report
    rm -f latest
    ln -s $(/usr/bin/ls -1t | head -1) latest

    # Purge the cached URL
    #curl -o /dev/null -k -s -X PURGE https://ci.trafficserver.apache.org/files/clang-analyzer/latest/
fi

# Exit with the scan-build exit code (thanks to --status-bugs)
exit $status
