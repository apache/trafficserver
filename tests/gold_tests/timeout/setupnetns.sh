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

ip netns del testserver
ip link del veth0 type veth peer name veth1
ip netns add testserver
ip link add veth0 type veth peer name veth1
ip addr add 10.1.1.2/24 dev veth0
ip link set up dev veth0
ip link set veth1 netns testserver
ip netns exec testserver ip addr add 10.1.1.1/24 dev veth1
ip netns exec testserver ip link set up  dev veth1
ip netns exec testserver iptables -t filter -A INPUT -p tcp --dport $1 -m tcp --tcp-flags FIN,SYN,RST,ACK SYN -m comment --comment v4-new-connections -j DROP
ip netns exec testserver iptables -t filter -A INPUT -p tcp --dport $2 -j ACCEPT
ip netns exec testserver iptables -t filter -A OUTPUT -p tcp  -j ACCEPT
# Depending on your iptables policy, you may need to adjust to allow traffic to pass over the veth0 virtual connection

