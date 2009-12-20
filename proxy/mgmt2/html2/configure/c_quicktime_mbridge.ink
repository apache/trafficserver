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
      <p>&nbsp;&nbsp;QuickTime Media Bridge Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.qt.media_bridge.name>Media Bridge Name</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.qt.media_bridge.name" value="<@record proxy.config.qt.media_bridge.name>">
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the name of the Media Bridge node used for QuickTime.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.qt.media_bridge.port>Media Bridge Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.qt.media_bridge.port" value="<@record proxy.config.qt.media_bridge.port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port of the Media Bridge node used for QuickTime.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.qt.media_bridge.mount_point>Media Bridge Mount Point</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.qt.media_bridge.mount_point" value="<@record proxy.config.qt.media_bridge.mount_point>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the Media Bridge mount point for QuickTime.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.qt.media_bridge.monitor.name>Monitor Name</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.qt.media_bridge.monitor.name" value="<@record proxy.config.qt.media_bridge.monitor.name>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the name of the monitoring agent.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.qt.media_bridge.monitor.port>Monitor Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.qt.media_bridge.monitor.port" value="<@record proxy.config.qt.media_bridge.monitor.port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port of the monitoring agent.
      </ul>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>

