#!/bin/sh

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

#
# This is a simple tool to show the commits, and author (by default) since
# a particular date. This is useful to get activity reports on the git repo.
#
# Example:
#
#   commits_since.sh "April 17" | grep -v commit|sort|uniq -c|sort -nr
#
DATE=${1:-}
COMMIT=${2:-HEAD}
PRETTY=${3:-format:%an}

CMD="git rev-list --no-merges --pretty=${PRETTY}"

if [ "$DATE" = "" ]; then
    $CMD ${COMMIT}
else
    $CMD --after="${DATE}" ${COMMIT}
fi
