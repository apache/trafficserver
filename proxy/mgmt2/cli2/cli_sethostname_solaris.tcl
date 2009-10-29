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

PATH=/usr/bin:/bin:/sbin:/usr/sbin:/etc:/usr/ucb:/usr/bsd:/usr/etc:/usr/loca
l/bin; export PATH

# determine network interface device from /etc/ts_product_info
network_dev=`grep "TS_NET_DEVICE=" /etc/ts_product_info | awk -F= '{print $2
}'`

name=$1
ipaddr=$2
mask=$3
rm -f /etc/inet/hosts

cat >> /etc/inet/hosts <<EOF
127.0.0.1       localhost
$ipaddr       $name   loghost
EOF

rm -f /etc/net/ticlts/hosts
rm -f /etc/net/ticots/hosts
rm -f /etc/net/ticotsord/hosts
rm -f /etc/nodename

# do we need to replace these??

cat >> /etc/net/ticlts/hosts << EOF
#ident  "@(#)hosts      1.2     92/07/14 SMI"   /* SVr4.0 1.2   */
# RPC Hosts
$name   $name
EOF

cat >> /etc/net/ticots/hosts << EOF
#ident  "@(#)hosts      1.2     92/07/14 SMI"   /* SVr4.0 1.2   */
# RPC Hosts
$name   $name
EOF

cat >> /etc/net/ticotsord/hosts << EOF
#ident  "@(#)hosts      1.2     92/07/14 SMI"   /* SVr4.0 1.2   */
# RPC Hosts
$name   $name
EOF

cat >> /etc/nodename << EOF
$name
EOF

rm -f /etc/hostname.${network_dev}
echo $name > /etc/hostname.${network_dev}

# Use 'hostname' to change host name
/usr/bin/hostname $name

#Also change the existing IP address being used by the device
/usr/sbin/ifconfig $network_dev plumb
/usr/sbin/ifconfig $network_dev $ipaddr netmask $mask
/usr/sbin/ifconfig $network_dev up

# -------------------------- Traffic Server --------------
#We also have to go and change the ipnat.conf file because
#it has the ip address stored where it will translate stuff

#Remove the existing file
IPNAT_CONF=/export/home/inktomi/rubicon/conf/yts/ipnat.conf
if [ -f $IPNAT_CONF -a -r $IPNAT_CONF ]; then
  rm -f $IPNAT_CONF
  cat >> $IPNAT_CONF << EOF
rdr $network_dev 0.0.0.0/0 port 80 -> $ipaddr port 8080 tcp
rdr $network_dev 0.0.0.0/0 port 119 -> $ipaddr port 119 tcp
EOF
/export/home/inktomi/rubicon/bin/ipnat -C
/export/home/inktomi/rubicon/bin/ipnat -f $IPNAT_CONF
fi

#Change records.config to reflect new hostname
RECORDS_CONFIG=/export/home/inktomi/rubicon/conf/yts/records.config
RECORDS_CONFIG_NEW=/export/home/inktomi/rubicon/conf/yts/records.config_new
if [ -f $RECORDS_CONFIG ]; then
 /bin/rm -f $RECORDS_CONFIG_NEW
 sed "s!proxy.config.proxy_name.*!proxy.config.proxy_name STRING $name!" $RE
CORDS_CONFIG > $RECORDS_CONFIG_NEW
 if [ $? = 0 ]; then
   cp $RECORDS_CONFIG_NEW $RECORDS_CONFIG
 fi
fi

#We need to restart TrafficManager and Server again
TRAFFIC_LINE=/export/home/inktomi/rubicon/bin/traffic_line
TF=/export/home/inktomi/rubicon/conf/yts/cli
if [ -x $TRAFFIC_LINE ]; then
  $TRAFFIC_LINE -p $TF -L
fi

exit 0
 
