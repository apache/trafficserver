#! /usr/bin/env bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# This script sets up a set of git repositories for the
# ci.trafficserver.apache.org jenkins server. We create local branches for all
# the origin release branches, then make linked clones for all the branches
# that we want working copies of.

set -e # exit on error
#set -x # verbose

MASTER=trafficserver

branch() {
    local dname="$1" # directory name
    local bname="$2" # branch name

    git clone --local $MASTER ${dname}
    (cd ${dname} && git checkout -b ${bname} origin/${bname})
}

if [ ! -d $MASTER ]; then
    git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git $MASTER
fi

(
    cd $MASTER
    for branch in $(git branch -r | egrep 'origin/[0-9.x]+'); do
        git checkout -b $(echo $branch | sed -es'|origin/||') $branch
        git checkout master
    done
)

# The directory names corresponsing to the branches should match the names
# used by jenkins; see jobs.yaml.
branch trafficserver_3.2 3.2.x
branch trafficserver_4 4.1.x
branch trafficserver_5 5.0.x

# vim: set sw=2 ts=2 sw=2 :
