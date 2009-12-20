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
      <p>&nbsp;&nbsp;Mapping/Redirection Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.url_remap.remap_required>Serve Mapped Hosts Only</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.url_remap.remap_required" value="1" <@checked proxy.config.url_remap.remap_required\1>>
        Required <br>
      <input type="radio" name="proxy.config.url_remap.remap_required" value="0" <@checked proxy.config.url_remap.remap_required\0>>
        Not Required
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>If required, <@record proxy.config.product_name> 
            serves requests only from origin servers listed in the mapping 
            rules of the remap.config file.
        <li>If a request does not match, the browser will receive an error.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.url_remap.pristine_host_hdr>Retain Client Host Header</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.url_remap.pristine_host_hdr" value="1" <@checked proxy.config.url_remap.pristine_host_hdr\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.url_remap.pristine_host_hdr" value="0" <@checked proxy.config.url_remap.pristine_host_hdr\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, <@record proxy.config.product_name> retains
            the client host header in a request during remapping.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.header.parse.no_host_url_redirect>Redirect No-Host Header to URL</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.header.parse.no_host_url_redirect" value="<@record proxy.config.header.parse.no_host_url_redirect>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the URL to which to redirect requests with no host headers (reverse proxy).
      </ul>
    </td>
  </tr>

</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.url_remap.filename>URL Remapping Rules
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.url_remap.filename>" file lets you
      specify the URL mappings rules.
    </td>
  </tr>
  <tr>
   <td>
    <@config_table_object /configure/f_remap_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_mapping.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_remap_config.ink&fname=<@record proxy.config.url_remap.filename>&frecord=proxy.config.url_remap.filename', 'displayWin');">
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
