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

# This should probably be configurable ...
cd /home/mirror/trafficserver.git || exit

# Check that we got a token for Jenkins access
if [ "" == "$1" ]; then
	echo "Must provide the auth token for Jenkins"
	exit 1
fi
token="build?token=$1"

# Optional second argument is the base URL, no trailing slash
BASE_URL=${2:-"https://ci.trafficserver.apache.org"}

# Some environment overridable defines
GIT=${GIT:-/usr/bin/git}
GREP=${GREP:-/usr/bin/grep}
CURL=${CURL:-/usr/bin/curl}

# Get the ref in the current version of the tree
function getRef() {
	local branch="$1"

	${GIT} show-ref -s refs/heads/${branch}
}

# Check the diff, and trigger builds as appropriate
function checkBuild() {
	local ref="$1"
	local branch="$2"
	local diff

	# Do the actual diff from the previous ref to current branch head
	diff=$(${GIT} log --name-only --pretty=format: ${ref}..refs/heads/${branch} | ${GREP} -v '^$')

	# Check if commits have doc/ changes
	echo -n "$diff" | ${GREP} -F -e doc/ >/dev/null
	if [ 0 == $? ]; then
		echo "Triggerd Docs build for ${branch}"
		${CURL} -o /dev/null -s ${BASE_URL}/view/${branch}/job/docs-${branch}/${token}
	fi

	# Check if commits have non doc/ changes
	echo -n "$diff" | ${GREP} -F -v -e doc/ >/dev/null
	if [ 0 == $? ]; then
		echo "Triggered main build for ${branch}"
		${CURL} -o /dev/null -s ${BASE_URL}/view/${branch}/job/start-${branch}/${token}
	fi
}

# Save away previous ref-specs, you must save all branches
REF_6_2=$(getRef "6.2.x")
REF_7_1=$(getRef "7.1.x")
REF_8_0=$(getRef "8.0.x")
REF_8_1=$(getRef "8.1.x")
REF_master=$(getRef "master")

# Do the updates
${GIT} remote update --prune >/dev/null 2>&1
${GIT} update-server-info

# Check the branches, this makes assumptions that the Jenkins build are named after the branches
checkBuild "$REF_6_2" "6.2.x"
checkBuild "$REF_7_1" "7.1.x"
checkBuild "$REF_8_0" "8.0.x"
checkBuild "$REF_8_1" "8.1.x"
checkBuild "$REF_master" "master"
