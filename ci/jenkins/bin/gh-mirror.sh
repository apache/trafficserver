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

GIT=${GIT:-/usr/bin/git}
GREP=${GREP:-/usr/bin/grep}

# This should probably be configurable ...
cd /home/mirror/trafficserver.git || exit

# Check that we got a token for Jenkins access
if [ "" == "$1" ]; then
    echo "Must provide the auth token for Jenkins"
    exit 1
fi

# Prep the URLs
URL="https://ci.trafficserver.apache.org/view/master/job"
TOKEN="build?token=$1"

# Save away previous ref-specs
REF_4_2=$(${GIT} show-ref -s  refs/heads/4.2.x)
REF_5_3=$(${GIT} show-ref -s  refs/heads/5.3.x)
REF_6_2=$(${GIT} show-ref -s  refs/heads/6.2.x)
REF_master=$(${GIT} show-ref -s  refs/heads/master)

${GIT} remote update > /dev/null 2>&1
${GIT} update-server-info

# Now find the changes
DIFF_4_2=$(${GIT} log --name-only --pretty=format: ${REF_4_2}..refs/heads/4.2.x| ${GREP} -v '^$')
DIFF_5_3=$(${GIT} log --name-only --pretty=format: ${REF_5_3}..refs/heads/5.3.x | ${GREP} -v '^$')
DIFF_6_2=$(${GIT} log --name-only --pretty=format: ${REF_6_2}..refs/heads/6.2.x | ${GREP} -v '^$')
DIFF_master=$(${GIT} log --name-only --pretty=format: ${REF_master}..refs/heads/master | ${GREP} -v '^$')

# Check master, we have to diff twice, because some commits could trigger both
echo -n ${DIFF_master} | fgrep -e doc/ > /dev/null
if [ 0 == $? ]; then
    echo "Triggerd Docs build for master"
    curl -o /dev/null -s ${URL}/docs-master/${TOKEN}
fi

echo -n ${DIFF_master} | fgrep -v -e doc/
if [ 0 == $? ]; then
    echo "Triggered main build for master"
    curl -o /dev/null -s ${URL}/in_tree-master/${TOKEN}
    curl -o /dev/null -s ${URL}/out_of_tree-master/${TOKEN}
fi
