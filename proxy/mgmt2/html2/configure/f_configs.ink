<!-------------------------------------------------------------------------
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
  ------------------------------------------------------------------------->

<html>
<head>

  <title>Configuration Files</title>
  <meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
  <script language="JavaScript1.2" src="/sniffer.js" type="text/javascript"></script>
  <script language="JavaScript1.2">
  if(!is_win && !is_mac)
    document.write("<link rel='stylesheet' type='text/css' href='/inktomiLarge.css' title='Default'>");
  else
    document.write("<link rel='stylesheet' type='text/css' href='/inktomi.css' title='Default'>");
  </script>

</head>

<body bgcolor="#FFFFFF">
<h1>Configuration Files</h1>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 


  <tr>
    <td class="blackLabel">
      <@record proxy.config.cache.control.filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_cache_config.ink>
   </td>
  </tr>
  <tr>
   <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_cache_config.ink&fname=<@record proxy.config.cache.control.filename>&frecord=proxy.config.cache.control.filename', 'displayWin');">
   </td>
  </tr>


  <tr>
    <td class="blackLabel">
      <@record proxy.config.content_filter.filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_filter_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_filter_config.ink&fname=<@record proxy.config.content_filter.filename>&frecord=proxy.config.content_filter.filename', 'displayWin');">
    </td>
  </tr>


  <tr>
    <td class="blackLabel">
      <@record proxy.config.ftp.reverse_ftp_remap_file_name>
    </td>
  </tr>
  <tr>
   <td width="100%" class="configureHelp" valign="top" align="left">
    <@config_table_object /configure/f_ftp_remap_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_ftp_remap_config.ink&fname=<@record proxy.config.ftp.reverse_ftp_remap_file_name>&frecord=proxy.config.ftp.reverse_ftp_remap_file_name', 'displayWin');">
    </td>
  </tr>


  <tr>
    <td class="blackLabel">
      <@record proxy.config.cache.hosting_filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_hosting_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_hosting_config.ink&fname=<@record proxy.config.cache.hosting_filename>&frecord=proxy.config.cache.hosting_filename', 'displayWin');">
    </td>
  </tr>

  <tr>
    <td class="blackLabel">
      <@record proxy.config.icp.icp_configuration>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_icp_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_icp_config.ink&fname=<@record proxy.config.icp.icp_configuration>&frecord=proxy.config.icp.icp_configuration', 'displayWin');">
    </td>
  </tr>

  <tr>
    <td class="blackLabel">
      <@record proxy.config.cache.ip_allow.filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_ip_allow_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_ip_allow_config.ink&fname=<@record proxy.config.cache.ip_allow.filename>&frecord=proxy.config.cache.ip_allow.filename', 'displayWin');">
    </td>
  </tr>


  <tr>
    <td class="blackLabel">
      <@record proxy.config.admin.ip_allow.filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_mgmt_allow_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_mgmt_allow_config.ink&fname=<@record proxy.config.admin.ip_allow.filename>&frecord=proxy.config.admin.ip_allow.filename', 'displayWin');">
    </td>
  </tr>

  <tr>
    <td class="blackLabel">
      <@record proxy.config.http.parent_proxy.file>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_parent_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
      <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_parent_config.ink&fname=<@record proxy.config.http.parent_proxy.file>&frecord=proxy.config.http.parent_proxy.file', 'displayWin');">
    </td>
  </tr>

 <tr>
    <td class="blackLabel">
      <@record proxy.config.cache.partition_filename>
    </td>
  </tr>
  <tr>
  <tr>
   <td>
    <@config_table_object /configure/f_partition_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_partition_config.ink&fname=<@record proxy.config.cache.partition_filename>&frecord=proxy.config.cache.partition_filename', 'displayWin');"> 
    </td>
  </tr>

  <tr>
    <td class="blackLabel">
      <@record proxy.config.url_remap.filename>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_remap_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_remap_config.ink&fname=<@record proxy.config.url_remap.filename>&frecord=proxy.config.url_remap.filename', 'displayWin');">
    </td>
  </tr>


  <tr>
    <td class="blackLabel">
      <@record proxy.config.socks.socks_config_file>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_socks_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_socks_config.ink&fname=<@record proxy.config.socks.socks_config_file>&frecord=proxy.config.socks.socks_config_file', 'displayWin');">
    </td>
  </tr>

 <tr>
    <td class="blackLabel">
      <@record proxy.config.dns.splitdns.filename>
    </td>
  </tr>
  <tr>
    <td>
     <@config_table_object /configure/f_split_dns_config.ink>
    </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_split_dns_config.ink&fname=<@record proxy.config.dns.splitdns.filename>&frecord=proxy.config.dns.splitdns.filename', 'displayWin');">
    </td>
  </tr>

  <tr>
    <td class="blackLabel">
      <@record proxy.config.update.update_configuration>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_update_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_update_config.ink&fname=<@record proxy.config.update.update_configuration>&frecord=proxy.config.update.update_configuration', 'displayWin');">
    </td>
  </tr>


  <tr id="<@record proxy.config.vmap.addr_file>">
    <td class="blackLabel">
      <@record proxy.config.vmap.addr_file>
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_vaddrs_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/f_configs.ink'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_vaddrs_config.ink&fname=<@record proxy.config.vmap.addr_file>&frecord=proxy.config.vmap.addr_file', 'displayWin');">
    </td>
  </tr>

</table>
</body>
</html>
