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

source /home/jenkins/bin/environment.sh
cd "${WORKSPACE}/src_in-tree"

if test "${JOB_NAME#*type=in_tree}" != "${JOB_NAME}"; then
    # Just use the configure from the snapshot.sh
    ${ATS_MAKE} -j4
    ${ATS_MAKE} check

    ${ATS_MAKE} clean
fi
