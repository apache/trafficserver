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
      <p>&nbsp;&nbsp;General NNTP Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.server_port>NNTP Server Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.nntp.server_port" value="<@record proxy.config.nntp.server_port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port that <@record proxy.config.product_name> 
            uses to serve NNTP requests.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.logging_enabled>Logging</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.nntp.logging_enabled" value="1" <@checked proxy.config.nntp.logging_enabled\1>>
                    Enabled <br>
      <input type="radio" name="proxy.config.nntp.logging_enabled" value="0" <@checked proxy.config.nntp.logging_enabled\0>>
                    Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, NNTP activity will be logged.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.transparency_enabled>Transparency</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.nntp.transparency_enabled" value="1" <@checked proxy.config.nntp.transparency_enabled\1>>
                    Enabled <br>
      <input type="radio" name="proxy.config.nntp.transparency_enabled" value="0" <@checked proxy.config.nntp.transparency_enabled\0>>
                    Disabled
    </td>
      <td class="configureHelp" valign="top" align="left"> 
       <ul>
         <li>When enabled, transparent NNTP requests can be accepted and served. 
       </ul>
     </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Posting</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.posting_enabled>Allow Posting</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.nntp.posting_enabled" value="1" <@checked proxy.config.nntp.posting_enabled\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.nntp.posting_enabled" value="0" <@checked proxy.config.nntp.posting_enabled\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, users can post NNTP articles to parent NNTP servers.
                  </ul>
                </td>
              </tr>

              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.greeting>Posting Okay Message</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="36" name="proxy.config.nntp.posting_ok_message" value="<@record proxy.config.nntp.greeting>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the message that displays to news readers 
                        when they connect to the <@record proxy.config.product_name>
                        if posting is enabled.
                  </ul>
                </td>
              </tr>

              <tr>
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.nntp.greeting_nopost>Posting Not-Okay Message</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="36" name="proxy.config.nntp.greeting_nopost" value="<@record proxy.config.nntp.greeting_nopost>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the message that displays to news readers
                        when they connect to the
                        <@record proxy.config.product_name> if posting is <i>not</i>
                        enabled.
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
