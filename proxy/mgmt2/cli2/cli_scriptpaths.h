/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

// This file contains path entries for various scripts that the spi programs
// uses. .. Better to have these in a file by themselves.

#ifndef __SPI_SCRIPT_PATH__H
#define __SPI_SCRIPT_PATH__H

#define INKTOMI_HOME "/home/inktomi"
#if (HOST_OS == solaris)
#define DEFAULTROUTER_PATH "/etc/defaultrouter"
#define DEFAULT_DOMAIN_PATH "/etc/defaultdomain"
#define NETMASK_PATH "/etc/inet/netmasks"

// not used by solaris
#define NIC_IDENTIFIER_STRING ""

#define NIC_SETTINGS_FILE "/kernel/drv/iprb.conf"
#define NIC_CHANGES_STRING "Changes Take Effect on Reboot"
#define NIC_CONFIG_LABEL "ForceSpeedDuplex="
#define SET_ROUTER_SCRIPT "./cli_setrouter.tcl"
#define GATEWAY_MARKER "GATEWAY=" // FIXME:

#elif (HOST_OS == linux)
#define DEFAULTROUTER_PATH "/etc/sysconfig/network"
#define DEFAULT_DOMAIN_PATH "/etc/sysconfig/network"
#define DEFAULT_NAMESERVER_PATH "/etc/resolv.conf"
#define DOMAINNAME_PATH "/etc/HOSTNAME"
#define GATEWAY_MARKER "GATEWAY="
#define NAMESERVER_MARKER "nameserver "
#define NISDOMAIN_MAKER "NISDOMAIN"
#define TIMEZONE_MARKER "ZONE="
#define NIC_IDENTIFIER_STRING "eth0"
#define NIC_CHANGES_STRING "Changes Take Effect Immediately"
#define NIC_SETTINGS_EXECUTABLE INKTOMI_HOME "/rubicon/bin/mii-diag"
#elif (HOST_OS == darwin)
#define DEFAULTROUTER_PATH "/tmp/tsrouterpath-fixme" // FIXME:
#define GATEWAY_MARKER "GATEWAY=" // FIXME:
#elif (HOST_OS == freebsd)
#define DEFAULTROUTER_PATH "/tmp/tsrouterpath-fixme" // FIXME:
#define GATEWAY_MARKER "GATEWAY=" // FIXME:
#endif

#define NAMESERVER_PATH "/etc/resolv.conf"
#define HOST_IP_PATH "/etc/hosts"
#define TS_INSTALL_DIR 	INSTALL_HOME
#define TS_HOME 	INKTOMI_HOME "/rubicon"
#define UPGRADE_INFO_PATH "/tmp/upgrade_info"
#define NAMESERVER_MARKER "nameserver "



/* The following seems to have been hardcoded into the SPI.  This
 * might want to be changed some time. */
#define ADMIN_USER "admin"

/* NOTE: all scripts should have two defines one for the script and
*        the other for the trailing args */
#define SPI_DOMAIN_NAME    "/usr/bin/domainname"


#define START_TRAFFIC_SERVER_SCRIPT INKTOMI_HOME "/rubicon/bin/start_traffic_server"
#define START_TRAFFIC_SERVER_ARGS "1>/usr/tmp/start_ts.log 2>&1"

#define STOP_TRAFFIC_SERVER_SCRIPT INKTOMI_HOME "/rubicon/bin/stop_traffic_server"
#define STOP_TRAFFIC_SERVER_ARGS "1>/usr/tmp/stop_ts.log 2>&1"

#define CLEAR_TRAFFIC_SERVER_SCRIPT INKTOMI_HOME "/rubicon/bin/spi_clearcache.sh"
#define CLEAR_TRAFFIC_SERVER_ARGS "1>/usr/tmp/clear_ts.log 2>&1"


#define SET_HOSTNAME_SCRIPT "./cli_sethostname.tcl"
#define SET_HOSTNAME_ARGS "1>/usr/tmp/spi_sethostname.log 2>&1"

#define SET_ROUTER_SCRIPT "./cli_setrouter.tcl"
#define SET_ROUTER_ARGS "1>/usr/tmp/spi_setrouter.log 2>&1"

#define SPI_TRAFFIC_LINE  "spi_traffic_line.sh"
#define SPI_TRAFFIC_LINE_ARGS  "1>/usr/tmp/spi_traffic_line.log 2>&1"

#define RECORDS_CONFIG_FILE  INKTOMI_HOME "/rubicon/etc/trafficserver/records.config"




#define DATE_BINARY "/usr/bin/date"

#define SET_TIMEZONE_SCRIPT INKTOMI_HOME "/rubicon/bin/spi_settimezone.sh"
#define SET_TIMEZONE_ARGS "1>/usr/tmp/spi_settimezone.log 2>&1"
#if (HOST_OS == solaris)
#define TIMEZONE_FILE "/etc/default/init"
#elif (HOST_OS == linux)
#define TIMEZONE_FILE "/etc/sysconfig/clock"
#elif (HOST_OS == darwin)
#define TIMEZONE_FILE "/tmp/tstzonefile-fixme" // FIXME:
#elif (HOST_OS == freebsd)
#define TIMEZONE_FILE "/tmp/tstzonefile-fixme" // FIXME:
#else
#error No file set for this OS.
#endif


#define SET_DOMAIN_SCRIPT "./cli_setdomain.tcl"
#define SET_DOMAIN_ARGS "1>/usr/tmp/spi_setdomain.log 2>&1"


#define SET_NETMASKS_SCRIPT INKTOMI_HOME "/rubicon/bin/spi_setnetmasks.sh"
#define SET_NETMASKS_ARGS "1>/usr/tmp/spi_setnetmasks.log 2>&1"




#endif // __SPI_SCRIPT_PATH__H
