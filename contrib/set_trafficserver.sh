#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Basic Dev base settings script.
# Desc: This script is meant for EC2 'like' Instances, however it works
#       perfectly for non-ec2 installs.  This script is intended as a guide and
#       makes use of many of the default settings for Apache Traffic Server.
#       There are a few comments which offer help for performance, please
#       read them.
# Author: Jason Giedymin
# Version Information:
#	v1.0.0   - Initial Release
#	v1.0.1   - thread limit set to 1
#	v1.0.2   - VM specific settings, getting ready for auto benchmarking.
#       v1.0.3   - Check to force /mnt based cache.db

REMAP_FILE=/usr/local/etc/trafficserver/remap.config
STORAGE_FILE=/usr/local/etc/trafficserver/storage.config
EC2_CACHE_LOC=/mnt/trafficserver_cache

# Base settings to use for testing and benchmarking
function recordsConfig() {
    startServer

    traffic_ctl config set proxy.config.reverse_proxy.enabled 1
    traffic_ctl config set proxy.config.exec_thread.autoconfig.enabled 1

    # Good default on a dedicated box or SMP VM.
    #traffic_ctl config set proxy.config.exec_thread.autoconfig.scale 3.000000

    # Good for a VM.
    traffic_ctl config set proxy.config.exec_thread.autoconfig.scale 1.000000

    traffic_ctl config set proxy.config.accept_threads 1
    traffic_ctl config set proxy.config.log.logging_enabled 0
    traffic_ctl config set proxy.config.http.server_port 8080
    traffic_ctl config set proxy.config.url_remap.pristine_host_hdr 1

    # Good for a VM.
    traffic_ctl config set proxy.config.exec_thread.limit 1

    # Good default on a dedicated box or SMP VM.
    traffic_ctl config set proxy.config.exec_thread.limit 2
}

function sampleRemap() {
    echo "Modifying $REMAP_FILE ..."

    #This is purely for testing.  Please supply your own ports and hosts.
    echo "map http://localhost:8080    http://localhost:80" >> $REMAP_FILE
    echo "map https://localhost:8443    http://localhost:443" >> $REMAP_FILE
}

function ec2Cache() {
    echo "Modifying $STORAGE_FILE ..."

    if [ ! -d $EC2_CACHE_LOC ]; then
        echo "Creating $EC2_CACHE_LOC and Chown-ing $STORAGE_FILE"
        mkdir -p $EC2_CACHE_LOC
        chown nobody:nobody $STORAGE_FILE
    fi

    sed -i 's/.\/var\/trafficserver 150994944/\/mnt\/trafficserver_cache 1073741824/g' $STORAGE_FILE
}

function startServer() {
    # If installed with the install script, use the init.d file created
    if [ -x /etc/init.d/trafficserver ]; then
        echo "Starting by forcing a restart of Apache TrafficServer..."
        /etc/init.d/trafficserver restart
        sleep 3
    fi
}

function start() {
	echo "Modifying configs..."

    sampleRemap
    ec2Cache

	recordsConfig

	echo "Complete."
}

start
