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
      <p>&nbsp;&nbsp;SOCKS Server Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

  <tr> 
    <td height="2" colspan="2" class="configureLabel">SOCKS Server</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.socks.default_servers>Default Servers</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.socks.default_servers" value="<@record proxy.config.socks.default_servers>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the names and the ports of the default SOCKS servers which <@record proxy.config.product_name> communicates to.
                  </ul>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.socks.socks_config_file>SOCKS Server Rules
    </td>
  </tr>
  <tr>
    <td height="2" colspan="2" class="configureHelp" valign="top" align="left"> 
      The "<@record proxy.config.socks.socks_config_file>" file lets
      you specify how <@record proxy.config.product_name> uses SOCKS servers.
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_socks_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_socks_server.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_socks_config.ink&fname=<@record proxy.config.socks.socks_config_file>&frecord=proxy.config.socks.socks_config_file', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
