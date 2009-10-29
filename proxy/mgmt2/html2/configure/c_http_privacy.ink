<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;HTTP Privacy Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10">

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Insert Headers</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_insert_client_ip>Client-IP</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_insert_client_ip" value="1" <@checked proxy.config.http.anonymize_insert_client_ip\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_insert_client_ip" value="0" <@checked proxy.config.http.anonymize_insert_client_ip\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> inserts
                        the "Client-IP" header into the outgoing request to retain the
                        client's IP address.
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
    <td height="2" colspan="2" class="configureLabel">Remove Headers</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_remove_client_ip>Client-IP</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_remove_client_ip" value="1" <@checked proxy.config.http.anonymize_remove_client_ip\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_remove_client_ip" value="0" <@checked proxy.config.http.anonymize_remove_client_ip\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> removes
                        the "Client-IP" header from the outgoing request for more
                        privacy.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_remove_cookie>Cookie</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_remove_cookie" value="1" <@checked proxy.config.http.anonymize_remove_cookie\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_remove_cookie" value="0" <@checked proxy.config.http.anonymize_remove_cookie\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> removes
                        the "Cookie" header from the outgoing request that
                        accompanies transactions to protect the privacy of your
                        site and users.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_remove_from>From</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_remove_from" value="1" <@checked proxy.config.http.anonymize_remove_from\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_remove_from" value="0" <@checked proxy.config.http.anonymize_remove_from\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> removes
                        the "From" header from the outgoing request that
                        accompanies transactions to protect the privacy of your
                        users.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_remove_referer>Referer</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_remove_referer" value="1" <@checked proxy.config.http.anonymize_remove_referer\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_remove_referer" value="0" <@checked proxy.config.http.anonymize_remove_referer\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> removes
                        the "Referer" header from the outgoing request that
                        accompanies transactions to protect the privacy of your
                        site and users.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_remove_user_agent>User-Agent</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.http.anonymize_remove_user_agent" value="1" <@checked proxy.config.http.anonymize_remove_user_agent\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.http.anonymize_remove_user_agent" value="0" <@checked proxy.config.http.anonymize_remove_user_agent\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>When enabled, <@record proxy.config.product_name> removes
                        the "User-Agent" header from the outgoing request
                        that accompanies transactions to protect the privacy of
                        your site and users.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.http.anonymize_other_header_list>Remove Others</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.http.anonymize_other_header_list" value="<@record proxy.config.http.anonymize_other_header_list>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the headers that <@record proxy.config.product_name> 
                        will remove from outgoing requests.
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





