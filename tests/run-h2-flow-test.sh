#!/bin/bash

# a simple script to exercise the http2 flow control test N times, with N being
# the input argument

# check for input argument
if [ $# -ne 1 ]; then
    echo "Usage: $0 <number of times to run test>"
    exit 1
fi
times=$1

for i in $(seq $times); do
    echo "Running test $i"
    # test for return value of 0
    ./autest.sh --ats-bin ~/build/ts-issue-http2-flow-test/bin --clean=none --sandbox=/tmp/sb -f http2_flow_control
    retVal=$?
    if [ $retVal -ne 0 ]; then
        echo "Test $i failed"
        exit $retVal
    fi
done
echo "All ${times} tests passed"
