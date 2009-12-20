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

<form method=POST action="/submit_net_config.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Network Configurations</p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onClick='return confirm("You are trying to change the network\nparameters. By doing so, you may lose\nthe network connection to this server!");'>
    </td>
    <td>
      <input class="configureButton" type=submit name="cancel" value=" Cancel ">
    </td>
  </tr>
</table>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg HOSTNAME>Hostname</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="HOSTNAME" value="<@network HOSTNAME>">
    </td>
     <td class="configureHelp" valign="top" align="left">
      <ul>
        <li>Change the host name
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Gateway</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg GATEWAY>Default Gateway</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="GATEWAY" value="<@network GATEWAY>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the gateway for this box
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
    <td height="2" colspan="2" class="configureLabel">DNS</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">

              <tr>
                <td height="2" colspan="2" class="configureLabelSmall">Domain name</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="domain" value="<@network domain>">
                </td>
                 <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the  search domain for the DNS
                  </ul>
                </td>
              </tr>
              
	      <tr>  
		<td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg DNS1>IP Address of Primary DNS</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="DNS1" value="<@network DNS1>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the IP address for the primary DNS
                  </ul>
                </td>
              </tr>
              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg DNS2>IP Address of Secondary DNS</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="DNS2" value="<@network DNS2>">
                </td>
                 <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the IP address for the secondary DNS
                  </ul>
                </td>
              </tr>
              <tr>            
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg DNS3>IP Address of Third DNS</td>
              </tr>   
              <tr>  
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="DNS3" value="<@network DNS3>">
                </td>
                 <td class="configureHelp" valign="top" align="left">
                  <ul>
                    <li>Specifies the IP address for the tertiary DNS
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

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onClick='return confirm("You are trying to change the network\nparameters. By doing so, you may lose\nthe network connection to this server!");'>
    </td>
    <td>
      <input class="configureButton" type=submit name="cancel" value=" Cancel ">
    </td>
  </tr>
</table>

<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
