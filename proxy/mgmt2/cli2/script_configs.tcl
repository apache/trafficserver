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

#
# This shell script fragment is intended to be sourced from within all
# other Linux scripts in this directory.  It contains those installation-
# dependent pathnames, or whatever, which might be used by the scripts.
# By putting all this in one place, we save endless pain later.
#
#

export TS_ETC_FILE="/etc/traffic_server"
export TS_PRODINFO="/etc/ts_product_info"

export VERS_SYMLINK="rubicon"
export INKTOMI_HOME="/home/inktomi"
export TS_TARBALL_PATH="${INKTOMI_HOME}/cdrom"
export TS_INSTALL_PATH="${INKTOMI_HOME}/${VERS_SYMLINK}"
export TS_BINARIES_PATH="${TS_INSTALL_PATH}/bin"
export SPI_BINARIES_PATH="${TS_BINARIES_PATH}"
export CONFIG_DIR="${TS_INSTALL_PATH}/etc/trafficserver"
export INTERNAL_CONFIG_DIR="${CONFIG_DIR}/internal"
export MAIN_CONFIG_FILE="records.config"
export PATCHES_DIR="/home/patches"

# We keep the SPI scripts in the general binaries directory (for now)
#
export SPI_SCRIPTS_PATH="$TS_BINARIES_PATH"


export PRIMARY_OS_DISK="/dev/sda1"
export BACKUP_OS_DISK="/dev/sdb1"
export UPGRADE_DIR="/upgrade"
export DISPOSABLE_CACHE_DISK="/dev/hdc"

export SERIAL_PORT_DEVICE="/dev/ttyS0"
export NETWORK_INTERFACE="eth0"
export NETWORK_MODULE_NAME="e100"
export SECOND_NETWORK_INTERFACE="eth1"
export INTERFACE_PARMSFILE="/etc/sysconfig/network-scripts/ifcfg-${NETWORK_INTERFACE}"
export SECOND_INTERFACE_PARMSFILE="/etc/sysconfig/network-scripts/ifcfg-${SECOND_NETWORK_INTERFACE}"
export NETWORK_P2FILE="/etc/sysconfig/network"

#  For now, code no secondary disk.
export TS_CONFIG_SECONDARY_DISK=""
export TS_CONFIG_SECONDARY_MTPT="/mnt/ts_secondary"

export SYSTEM_CONFIG_FILES="/etc/resolv.conf /etc/hosts ${NETWORK_P2FILE} ${NETWORK_PARMSFILE}"

export TS_CONFIG_FILES="logs.config storage.config socks.config proxy.pac lm.config vaddrs.config cache.config icp.config mgmt_allow.config ip_allow.config parent.config filter.config remap.config mgr.cnf update.config ${MAIN_CONFIG_FILE}"


export FLOPPY_DEVICE="/dev/fd0"
export FLOPPY_MTPT="/mnt/floppy"
export FLOPPY_STATUSFILE="/tmp/floppy_mounted"


#  Binaries.
#
export AWK="/bin/awk"
export CAT="/bin/cat"
export CP="/bin/cp"
export CRONTAB="/usr/bin/crontab"
export CUT="/usr/bin/cut"
export DATE="/bin/date"
export DD="/bin/dd"
export ECHO="/bin/echo"
export EXPR="/usr/bin/expr"
export FGREP="/bin/fgrep"
export GREP="/bin/grep"
export GZIP="/bin/gzip"
export HOSTNAME="/bin/hostname"
export IFCONFIG="/sbin/ifconfig"
export INSMOD="/sbin/insmod"
export KILLALL="/usr/bin/killall"
export LN="/bin/ln"
export LS="/bin/ls"
export MAIL="/bin/mail"
export MKDIR="/bin/mkdir"
export MKE2FS="/sbin/mke2fs"
export MOUNT="/bin/mount"
export MV="/bin/mv"
export PERL="/usr/bin/perl"
export PING="/bin/ping"
export RCP="/usr/bin/rcp"
export RM="/bin/rm"
export RMMOD="/sbin/rmmod"
export ROUTE="/sbin/route"
export SED="/bin/sed"
export SLEEP="/bin/sleep"
export SYNC="/bin/sync"
export TAR="/bin/tar"
export TOUCH="/bin/touch"
export UMOUNT="/bin/umount"
export ZCAT="/bin/zcat"


# redhat scripts
export NETWORK_DOWN="/etc/rc.d/init.d/network stop"
export NETWORK_UP="/etc/rc.d/init.d/network start"
