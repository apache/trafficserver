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

pushd $(dirname $0) > /dev/null
export PYTHONPATH=$(pwd):$PYTHONPATH
RED='\033[0;31m'
GREEN='\033[1;32m'
NC='\033[0m' # No Color
if [ ! -f ./env-test/bin/autest ]; then\
        echo -e "${RED}AuTest is not installed! Bootstrapping system...${NC}";\
		./bootstrap.py;\
        echo -e "${GREEN}Done!${NC}";\
	fi
# this is for rhel or centos systems
test -r /opt/rh/rh-python36/enable && . /opt/rh/rh-python36/enable
. env-test/bin/activate
./env-test/bin/autest -D gold_tests "$@"
ret=$?
popd > /dev/null
exit $ret
