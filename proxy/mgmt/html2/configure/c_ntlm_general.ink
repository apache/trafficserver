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

  c_ntlm_general.ink
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;General NTLM Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ntlm.dc.list>Domain Controller Hostnames</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ntlm.dc.list" value="<@record proxy.config.ntlm.dc.list>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the comma-separated hostnames of domain controllers.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ntlm.nt_domain>NT Domain Name</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ntlm.nt_domain" value="<@record proxy.config.ntlm.nt_domain>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the NT domain name that <@record proxy.config.product_name> should authenticate against.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ntlm.dc.load_balance>Load Balancing</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.ntlm.dc.load_balance" value="1" <@checked proxy.config.ntlm.dc.load_balance\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.ntlm.dc.load_balance" value="0" <@checked proxy.config.ntlm.dc.load_balance\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, <@record proxy.config.product_name> would balance
            the load on sending authentication requests to the domain controllers.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
