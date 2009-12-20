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

  c_dns_resolver.ink
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;DNS Resolver Configurations</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.dns.search_default_domains>Local Domain Expansion</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.dns.search_default_domains" value="1" <@checked proxy.config.dns.search_default_domains\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.dns.search_default_domains" value="0" <@checked proxy.config.dns.search_default_domains\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables local domain expansion so that 
            <@record proxy.config.product_name> can attempt to resolve
            unqualified hostnames by expanding to the local domain.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
