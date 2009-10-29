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



source ./script_configs.tcl


new_router=$1

#First change the router info on the disk

${PERL} -pi -e 's|^GATEWAY=.*|GATEWAY='$new_router'| ;' ${NETWORK_P2FILE}


#Now change the router we are currently using
${ROUTE} del default 
${ROUTE} add default gw $new_router 1>/dev/null 2>&1

 
