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
      <p>&nbsp;&nbsp;NNTP Cacheability Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.cache_enabled>NNTP Caching</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.nntp.cache_enabled" value="1" <@checked proxy.config.nntp.cache_enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.nntp.cache_enabled" value="0" <@checked proxy.config.nntp.cache_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables <@record proxy.config.product_name> from caching responses to NNTP requests.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Expiration Timeouts</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.expire_group>Group</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.nntp.expire_group" value="<@record proxy.config.nntp.expire_group>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the default expiration time (in seconds) of groups.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.expire_overview>Overviews</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.nntp.check_cancels_every" value="<@record proxy.config.nntp.expire_overview>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the default expiration time (in seconds) of overviews.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.expire_list_active>Result for List (Active) Command</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.nntp.expire_list_active" value="<@record proxy.config.nntp.expire_list_active>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the expiration time (in seconds) of the list (active) command. 
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.expire_list_atimes>Result for List (Active) Times Command</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.nntp.check_pull_every" value="<@record proxy.config.nntp.expire_list_atimes>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the expiration time (in seconds) of the result of the list (active) times command.
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

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
