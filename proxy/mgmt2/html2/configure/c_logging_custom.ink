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
      <p>&nbsp;&nbsp;Custom Logging Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.custom_logs_enabled>Custom Logging</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.custom_logs_enabled" value="1" <@checked proxy.config.log2.custom_logs_enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.custom_logs_enabled" value="0" <@checked proxy.config.log2.custom_logs_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables custom logging.
      </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
       <@submit_error_flg proxy.config.log2.xml_config_file>Custom Log File Definitions
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.log2.xml_config_file>" file lets you
      specify the format that <@record proxy.config.product_name>
      follows in logging.  It defines log files, their formats,
      filters, as well as processing options.
    </td>
  </tr>
  <tr>
    <td align="center">
      <@file_edit proxy.config.log2.xml_config_file>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
