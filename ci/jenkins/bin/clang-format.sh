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

cd "${WORKSPACE}/src"
autoreconf -if && ./configure

# Test clang-format, copy our version of clang-format if needed
if [ ! -d .git/fmt ]; then
    if [ -z "${ghprbTargetBranch}" ]; then
	cp -rp /home/jenkins/clang-format/master .git/fmt
    else
	# This is for Github PR's, to make sure we use the right clang-format for the branch.
	# This is not an issue on normal branch builds, since they will have the right .git/fmt.
	cp -rp /home/jenkins/clang-format/${ghprbTargetBranch} .git/fmt
    fi
fi

${ATS_MAKE} -j clang-format
git diff --exit-code
[ "0" != "$?" ] && exit -1

# Normal exit
exit 0
