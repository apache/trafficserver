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
      <p>&nbsp;&nbsp;General FTP Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<script language="JavaScript1.2">
function checkRange()
{
	if(window.document.forms[0].elements[8].checked == false)
	  {
		window.document.forms[0].elements[9].blur();
	    window.document.forms[0].elements[10].blur();
	  }
}

</script>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ftp.proxy_server_port>FTP 
      Proxy Server Port</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText" width="25%"> 
      <input type="text" size="6" name="proxy.config.ftp.proxy_server_port" value="<@record proxy.config.ftp.proxy_server_port>">
    </td>
    <td class="configureHelp" valign="top" align="left" width="75%"> 
      <ul>
        <li>Specifies the port used for FTP connections. 
      </ul>
    </td>
  </tr>
  <tr> 
    <td nowrap class="configureLabel" colspan="2">Listening Port Configuration</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText" height="31" width="25%"> 
      <p> 
        <input type="radio" name="proxy.config.ftp.open_lisn_port_mode" value="1" <@checked proxy.config.ftp.open_lisn_port_mode\1>>
        Default Settings</p>
    </td>
    <td class="configureHelp" valign="top" align="left" height="31" width="75%"> 
      <ul>
        <li>Let the OS automatically pick an available port for use.
      </ul>
    </td>
  </tr>
  <tr> 
    <td nowrap class="bodyText" height="5" width="25%">
       <p>
         <input type="radio" name="proxy.config.ftp.open_lisn_port_mode" onClick ="checkRange();" value="2" <@checked proxy.config.ftp.open_lisn_port_mode\2>>
         Specify Range</p>
       <p> 
         <input type="text" name="proxy.config.ftp.max_lisn_port" onFocus="checkRange()" value="<@record proxy.config.ftp.max_lisn_port>">
         Listening Port (Max)<br>
         <input type="text" name="proxy.config.ftp.min_lisn_port" onFocus="checkRange()" value="<@record proxy.config.ftp.min_lisn_port>">
         Listening Port (Min)</p>
    </td>
    <td class="configureHelp" valign="top" align="left" height="5" width="75%"> 
      <ul>
        <li>Specify a range of values for the listening ports that traffic server 
            should use.
      </ul>
    </td>
  </tr>
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ftp.server_data_default_pasv>Default 
      Data Connection Method</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText" width="25%"> 
      <input type="radio" name="proxy.config.ftp.server_data_default_pasv" value="1" <@checked proxy.config.ftp.server_data_default_pasv\1>>
      Proxy Sends PASV <br>
      <input type="radio" name="proxy.config.ftp.server_data_default_pasv" value="0" <@checked proxy.config.ftp.server_data_default_pasv\0>>
      Proxy Sends PORT </td>
    <td class="configureHelp" valign="top" align="left" width="75%"> 
      <ul>
        <li>Specifies the default method used to set up server side data connections. 
      </ul>
    </td>
  </tr>
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ftp.share_ftp_server_ctrl_enabled>Shared 
      Server Connections</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText" width="25%"> 
      <input type="radio" name="proxy.config.ftp.share_ftp_server_ctrl_enabled" value="1" <@checked proxy.config.ftp.share_ftp_server_ctrl_enabled\1>>
      Enabled <br>
      <input type="radio" name="proxy.config.ftp.share_ftp_server_ctrl_enabled" value="0" <@checked proxy.config.ftp.share_ftp_server_ctrl_enabled\0>>
      Disabled </td>
    <td class="configureHelp" valign="top" align="left" width="75%"> 
      <ul>
        <li>Enables/Disables sharing of server control connections among multiple 
            anonymous FTP clients. 
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>

