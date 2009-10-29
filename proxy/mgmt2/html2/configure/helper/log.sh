#! /bin/sh -f

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


logfile=$1
action=$2
arg=$3
#echo "logfile: $logfile"
#echo "action: $action"
#echo "arg: $arg"

if [ "$action" = "view_all" ]; then
  cat $logfile 2>/dev/null

elif [ "$action" = "view_last" -a ! -z "$arg" ]; then
  tail -$arg $logfile 2>/dev/null

elif [ "$action" = "view_subset" -a ! -z "$arg" ]; then
  grep "$arg" $logfile 2>/dev/null

fi
exit 0
