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
      <p>&nbsp;&nbsp;NNTP Authentication Configurations</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.access_control_enabled>NNTP Authentication</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.nntp.access_control_enabled" value="1" <@checked proxy.config.nntp.access_control_enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.nntp.access_control_enabled" value="0" <@checked proxy.config.nntp.access_control_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, you can control user access to news articles
            cached by <@record proxy.config.product_name> according
            to the access privileges set in the 'nntp_access.config'
            file below.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.v2_authentication>NNTP v2 Authentication Support</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.nntp.v2_authentication" value="1" <@checked proxy.config.nntp.v2_authentication\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.nntp.v2_authentication" value="0" <@checked proxy.config.nntp.v2_authentication\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables support for NNTP v2.  Enable this option only if you are certain that all your
            client authentication support NNTP v2.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">NNTP Authentication Server</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.run_local_authentication_server>Local Authentication Server</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.nntp.run_local_authentication_server" value="1" <@checked proxy.config.nntp.run_local_authentication_server\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.nntp.run_local_authentication_server" value="0" <@checked proxy.config.nntp.run_local_authentication_server\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables the local authentication server, 'nntp_auth'.  The server
                    will be managed by <@record proxy.config.product_name>, enabling
                    it to be automatically restarted in case of a failure.
                    <li>The local authentication server will listen on the port
                    specified below.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.authorization_hostname>Hostname</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.nntp.authorization_hostname" value="<@record proxy.config.nntp.authorization_hostname>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the NNTP authentication server
                    hostname if the server is running on a remote machine.
                    <li>An empty hostname defaults to 'localhost'.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.authorization_port>Port</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.nntp.authorization_port" value="<@record proxy.config.nntp.authorization_port>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the NNTP authentication server port.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.nntp.access_filename>NNTP Access Privileges
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.nntp.access_filename>" file lets you
      control user access to news articles cached by
      <@record proxy.config.product_name>.  Each line in this file
      describes the access privileges for a particular group of
      clients.
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_nntp_access_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_nntp_auth.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_nntp_access_config.ink&fname=<@record proxy.config.nntp.access_filename>&frecord=proxy.config.nntp.access_filename', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>

