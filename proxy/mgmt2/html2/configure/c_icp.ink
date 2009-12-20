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

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;ICP Peering Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.icp.enabled>ICP Mode</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText"> 
      <input type="radio" name="proxy.config.icp.enabled" value="1" <@checked proxy.config.icp.enabled\1>>
      Only Receive Queries <br>
      <input type="radio" name="proxy.config.icp.enabled" value="2" <@checked proxy.config.icp.enabled\2>>
      Send/Receive Queries<br>
      <input type="radio" name="proxy.config.icp.enabled" value="0" <@checked proxy.config.icp.enabled\0>>
      Disabled</td>
    <td width="100%" class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Sets ICP mode for hierarchical caching.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.icp.icp_port>ICP Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText"> 
      <input type="text" size="6" name="proxy.config.icp.icp_port" value="<@record proxy.config.icp.icp_port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
	<li>Specifies the UDP port that is used for ICP messages.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.icp.multicast_enabled>ICP Multicast</td>
  </tr>
  <tr> 
    <td class="bodyText"> 
      <input type="radio" name="proxy.config.icp.multicast_enabled" value="1" <@checked proxy.config.icp.multicast_enabled\1>>
      Enabled <br>
      <input type="radio" name="proxy.config.icp.multicast_enabled" value="0" <@checked proxy.config.icp.multicast_enabled\0>>
      Disabled 
    </td>
    <td width="100%" class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables ICP multicast.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.icp.query_timeout>ICP Query Timeout</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.icp.query_timeout" value="<@record proxy.config.icp.query_timeout>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
	<li>Specifies the timeout limit for ICP queries.
     </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
	<@submit_error_flg proxy.config.icp.icp_configuration>ICP Peers
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left"> 
      This files defines the names and configuration information for
      the ICP peers (parent and sibling caches).
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_icp_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_icp.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_icp_config.ink&fname=<@record proxy.config.icp.icp_configuration>&frecord=proxy.config.icp.icp_configuration', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>


