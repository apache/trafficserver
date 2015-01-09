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

OK=()
FAIL=()
EXCLUDE=()
STATUS=0

# Produce a help page
do_help() {
    echo "run_all.sh: Run all TSQA tests"
    echo
    echo "Options:"
    echo "	-e	Exclude the given test"
    echo "	-h	Show this help page"
}

# Parse the arguments
while getopts "e:" opt; do
  case $opt in
      e)
	  EXCLUDE+=($OPTARG)
	  ;;
      \?|h)
	  do_help
	  exit 1
	  ;;
  esac
done


# Run all tests, record the results
for test in test-*; do
    run_it=1
    for ex in ${EXCLUDE[@]}; do
	echo $ex
	if [ "$ex" == "$test" ]; then
	    run_it=0
	    break
	fi
    done
    if [ $run_it -ne 0 ]; then
	echo "--> Starting test: $test"
	./${test}
	res=$?
	if [ $res != 0 ]; then
	    echo "Failure: ${test}"
	    FAIL+=(${test})
	    STATUS=1
	else
	    echo "Success: ${test}"
	    OK+=(${test})
	fi
    fi
done


# Print out a results summary
echo
echo
echo "RESULT SUMMARY"
echo "=============="
for t in ${OK[@]}; do
    echo "$t	...OK"
done
for t in ${FAIL[@]}; do
    echo "$t	...FAIL"
done

exit ${STATUS}
