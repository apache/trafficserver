#! /bin/sh

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


# GetRecord - extract the entry in records.config and 
#             store the record value in $value
# $1 - name of variable in the records.config
#####################################################
GetRecord() {
  record="$1"
  value=""

  InstallDir="$ROOT"
  [ -z "$InstallDir" ] && InstallDir="$INST_ROOT"
  [ -z "$InstallDir" ] && InstallDir=`/usr/bin/head -1 /etc/traffic_server 2>/dev/null`
  [ -z "$InstallDir" ] && InstallDir="/home/trafficserver" 

  records_config=${InstallDir}/config/records.config
  if [ -f ${records_config} ]; then
    value=`grep "$record" ${records_config} | grep -v "^#" | cut -d' ' -f4-`
  fi
  return $?
}

# ArmEnableDisable_RNI - enable/disable rtsp 
#                        redirect/inarmpln plugins
#
#####################################################
ArmEnableDisable_RNI() {
  GetRecord "proxy.config.rni.proxy_restart_cmd"
  
  if [ "$value" != "NULL" ]; then
    real_proxy_bin=`echo $value | awk '{print $1}'`
    real_proxy_bin_dir=`dirname ${real_proxy_bin}`
    real_proxy_dir=`dirname ${real_proxy_bin_dir}`
    real_proxy_plg_dir=${real_proxy_dir}/Plugins
    real_proxy_disable_dir=${real_proxy_plg_dir}/disabled

    if [ -d ${real_proxy_plg_dir} ]; then
      if [ "$arm_enabled" = "1" ]; then
        # move RTSP redirect/inarmpln plugin out of disabled directory
        mv ${real_proxy_disable_dir}/redirect.so.* ${real_proxy_plg_dir} 2>/dev/null
        mv ${real_proxy_disable_dir}/inarmpln.so.* ${real_proxy_plg_dir} 2>/dev/null
      else
        # move RTSP redirect/inarmpln plugin to disabled directory
        if [ ! -d ${real_proxy_disable_dir} ]; then
          mkdir ${real_proxy_disable_dir} >/dev/null 2>&1
        fi
        mv ${real_proxy_plg_dir}/redirect.so.* ${real_proxy_disable_dir} 2>/dev/null
        mv ${real_proxy_plg_dir}/inarmpln.so.* ${real_proxy_disable_dir} 2>/dev/null
      fi
    fi
  fi
}

if [ $# -ne 1 ]; then
  exit 1
fi
arm_enabled=$1

# ArmEnableDisable_IpForwarding() - enable/disable 

# RNI handling
ArmEnableDisable_RNI
exit 0
