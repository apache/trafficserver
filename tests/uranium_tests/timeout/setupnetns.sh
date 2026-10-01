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

namespace=${3:-testserver}
host_interface=${4:-veth0}
namespace_interface=${5:-veth1}

ip netns del "$namespace" 2>/dev/null || true
ip link del "$host_interface" 2>/dev/null || true

set -e
ip netns add "$namespace"
ip link add "$host_interface" type veth peer name "$namespace_interface"
ip addr add 10.1.1.2/24 dev "$host_interface"
ip link set up dev "$host_interface"
ip link set "$namespace_interface" netns "$namespace"
ip netns exec "$namespace" ip addr add 10.1.1.1/24 dev "$namespace_interface"
ip netns exec "$namespace" ip link set up dev "$namespace_interface"
ip netns exec "$namespace" iptables -t filter -A INPUT -p tcp --dport "$1" \
  -m tcp --tcp-flags FIN,SYN,RST,ACK SYN \
  -m comment --comment v4-new-connections -j DROP
ip netns exec "$namespace" iptables -t filter -A INPUT -p tcp --dport "$2" -j ACCEPT
ip netns exec "$namespace" iptables -t filter -A OUTPUT -p tcp -j ACCEPT
# Depending on your iptables policy, you may need to adjust to allow traffic to pass over the veth0 virtual connection
