#!/bin/bash

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

# Different vresions of nc have different wait options,
# so falling back to shell

# Send part of the request and then stop
request() {
	printf "GET / HTTP/1.1"
}

nc_name="nc"
ncat
if [ $? -le 1 ]
then
	request | ncat -i 11 127.0.0.1 $1 &
	targetPID=$!
	nc_name="ncat"
else
	request | nc -w 11 127.0.0.1 $1 &
	targetPID=$!
fi

count=11
echo PID is *${targetPID}
while [ $count -gt 0 ]
do
	sleep 1
	output=`ps uax | grep $targetPID | grep "$nc_name "`
	output0=`ps uax | grep $targetPID`
	echo "Out for $targetPID is $output or $output0"
	if [ -z "$output" ] # process is gone
	then
		exit 0
	fi
	count=`expr $count - 1`
done
kill $targetPID

