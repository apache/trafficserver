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

cd "${ATS_BUILD_BASEDIR}/build"
[ -d BUILDS ] && cd BUILDS

echo
echo -n "Unit tests started at " && date
${ATS_MAKE} -j 2 check VERBOSE=Y V=1 || exit 1
echo -n "Unit tests finished at " && date
${ATS_MAKE} install || exit 1

echo
echo -n "Regression tests started at " && date
"${ATS_BUILD_BASEDIR}/install/bin/traffic_server" -k -K -R 1
rval=$?
echo -n "Regression tests finished at " && date
exit $rval
