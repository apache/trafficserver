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


name=$1
ipaddr=$2
mask=$3




${RM} -f /etc/hosts

${CAT} > /etc/hosts <<EOF
127.0.0.1       localhost       
$ipaddr       $name   loghost
EOF



# Use 'hostname' to change host name
${HOSTNAME} $name


# Change the values for the next reboot
#
${PERL} -pi -e 's|IPADDR=.*|IPADDR='$ipaddr'|; s|NETMASK=.*|NETMASK='$mask'|;' ${INTERFACE_PARMSFILE}
${PERL} -pi -e 's|HOSTNAME=.*|HOSTNAME='$name'|; ' ${NETWORK_P2FILE}

# Invoke changes
/bin/sh /etc/rc.d/init.d/network restart 1>/usr/tmp/cli_sethostname.log 2>&1



# -------------------------- Traffic Server --------------
#We also have to go and change the ipnat.conf file because
#it has the ip address stored where it will translate stuff

#Remove the existing file
IPNAT_CONF=${CONFIG_DIR}/ipnat.conf

if [ -f $IPNAT_CONF -a -r $IPNAT_CONF ]
then
    ${RM} -f $IPNAT_CONF
    ${CAT} >> $IPNAT_CONF << EOF
rdr ${NETWORK_INTERFACE} 0.0.0.0/0 port 80 -> $ipaddr port 8080 tcp
rdr ${NETWORK_INTERFACE} 0.0.0.0/0 port 119 -> $ipaddr port 119 tcp 
EOF
    ${TS_BINARIES_PATH}/ipnat -C
    ${TS_BINARIES_PATH}/ipnat -f $IPNAT_CONF
fi

#Change records.config to reflect new hostname
RECORDS_CONFIG=${CONFIG_DIR}/${MAIN_CONFIG_FILE}
RECORDS_CONFIG_NEW=${RECORDS_CONFIG}_new

if [ -f $RECORDS_CONFIG ]
then
    ${RM} -f $RECORDS_CONFIG_NEW
    ${SED} "s!proxy.config.proxy_name.*!proxy.config.proxy_name STRING $name!" $RECORDS_CONFIG > $RECORDS_CONFIG_NEW
    if [ $? = 0 ]
    then
	${CP} $RECORDS_CONFIG_NEW $RECORDS_CONFIG
    fi
fi

#We need to restart TrafficManager and Server again
TRAFFIC_LINE=${TS_BINARIES_PATH}/traffic_line
TF=${CONFIG_DIR}/cli

if [ -x $TRAFFIC_LINE ]
then
    $TRAFFIC_LINE -p $TF -L
fi

exit 0
