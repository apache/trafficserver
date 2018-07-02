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

# This script updates any working copies created by the git-jenkins-setup.sh
# script. First we pull all the branches from the master clone, then we update
# all the working copies. this script runs from crontab every few minutes.

set -e # exit on error
#set -x # verbose

MASTER=trafficserver

(
    cd trafficserver

    # Pick up any new release branches ...
    for branch in $(git branch -r | egrep 'origin/[0-9.x]+'); do
        git checkout -b $(echo $branch | sed -es'|origin/||') $branch || true
        git checkout master
    done

    git pull --all --verbose
)

for repo in /Users/jenkins/git/trafficserver*; do
    (cd $repo && git pull)
done

# vim: set sw=2 ts=2 sw=2 :
