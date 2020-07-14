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

# This is funky delaying and backgrounding the client request, but I just
# could not get the command executing in the network space to go to background
# without blocking the autest.


if [ $# == 5 ]
then
./ssl-delay-server $1 $2 $3 server.pem 2> server${1}post.log  &
sleep 1
curl -H'Connection:close' -d "bob" -i http://127.0.0.1:$4/${5} --tlsv1.2
else
./ssl-delay-server $1 $2 $3 server.pem 2> server${1}get.log  &
sleep 1
curl -H'Connection:close' -i http://127.0.0.1:$4/${5} --tlsv1.2
fi

kill $(jobs -pr)

exit 0


