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

<form name="form1" method=POST action="/submit_update.cgi?<@link_query>">

<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;HTTP Dynamic Cacheability Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<!-- the dynamic caching record variables -->

<table width="100%" border="0" cellspacing="0" cellpadding="10">

<tr> 
  <td height="2" colspan="2" class="configureLabel">Behavior</td>
</tr>

<tr> 
   <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.cache_urls_that_look_dynamic>Dynamic Document Caching</td>
              </tr>

              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.cache_urls_that_look_dynamic" value="1" <@checked proxy.config.http.cache.cache_urls_that_look_dynamic\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.cache.cache_urls_that_look_dynamic" value="0" <@checked proxy.config.http.cache.cache_urls_that_look_dynamic\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables caching of HTTP documents based on URLs that look dynamic.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.cache.cache_responses_to_cookies>Caching Response to Cookies</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="3" <@checked proxy.config.http.cache.cache_responses_to_cookies\3>>
                    Cache All but Text <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="2" <@checked proxy.config.http.cache.cache_responses_to_cookies\2>>
                    Cache Only Image Types <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="1" <@checked proxy.config.http.cache.cache_responses_to_cookies\1>>
                    Cache Any Content-Type <br>
                  <input type="radio" name="proxy.config.http.cache.cache_responses_to_cookies" value="0" <@checked proxy.config.http.cache.cache_responses_to_cookies\0>>
                    No Cache on Cookies
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies how cookies are cached.
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
  <td height="2" colspan="2" class="configureLabel">Cache Configuration Rules</td>
</tr>

<tr>
 <td>
  <@config_table_object /configure/f_cache_config.ink>
 </td>
</tr>

<tr>
 <td colspan="2" align="right">
   <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_cache_config.ink&fname=<@record proxy.config.cache.control.filename>', 'displayWin');">
 </td>
</tr>


</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
   <td width="100%" nowrap>
     &nbsp;
   </td>
  </tr>
</table>

<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
