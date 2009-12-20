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
      <p>&nbsp;&nbsp;HTTP Custom Response Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.body_factory.enable_customizations>Custom Responses</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.body_factory.enable_customizations" value="2" <@checked proxy.config.body_factory.enable_customizations\2>>
	Enabled Language-Targeted Response<br>
      <input type="radio" name="proxy.config.body_factory.enable_customizations" value="1" <@checked proxy.config.body_factory.enable_customizations\1>>
	Enabled in "default" Directory Only <br>
      <input type="radio" name="proxy.config.body_factory.enable_customizations" value="0" <@checked proxy.config.body_factory.enable_customizations\0>>
	Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies whether custom response pages are enabled or
            disabled and which response pages are used.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.body_factory.enable_logging>Custom Response Logging</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.body_factory.enable_logging" value="1" <@checked proxy.config.body_factory.enable_logging\1>>
	Enabled <br>
      <input type="radio" name="proxy.config.body_factory.enable_logging" value="0" <@checked proxy.config.body_factory.enable_logging\0>>	
	Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables logging for custom response pages.
        <li>When enabled, <@record proxy.config.product_name> records
            a message in the error log each time a customized response
            page is used or modified.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.body_factory.template_sets_dir>Custom Response Template Directory</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.body_factory.template_sets_dir" value="<@record proxy.config.body_factory.template_sets_dir>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the directory in which the custom response pages are stored.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
