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

# Shouldn't have to tweak this
export ATS_SRC_HOME="/home/jenkins/src"

# Extract the current branch (default to master). ToDo: Can we do this better ?
ATS_BRANCH=master
test "${JOB_NAME#*-4.2.x}" != "${JOB_NAME}" && ATS_BRANCH=4.2.x
test "${JOB_NAME#*-5.0.x}" != "${JOB_NAME}" && ATS_BRANCH=5.0.x
export ATS_BRANCH

# Decide on compilers, gcc is the default
if test "${JOB_NAME#*compiler=clang}" != "${JOB_NAME}"; then
    export CC="clang"
    export CXX="clang++"
    export CXXFLAGS="-Qunused-arguments -std=c++11"
    export WITH_LIBCPLUSPLUS="yes"
fi
