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
      <p>&nbsp;&nbsp;Congestion Control Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.http.congestion_control.enabled>Congestion Control</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.http.congestion_control.enabled" value="1" <@checked proxy.config.http.congestion_control.enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.http.congestion_control.enabled" value="0" <@checked proxy.config.http.congestion_control.enabled\0>>
        Disabled
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, instead of forwarding requests to congested hosts, <@record proxy.config.product_name> will instruct the requestor to retry at a later time. 
      </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.http.congestion_control.filename>Congestion Rules
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.http.congestion_control.filename>" file 
      specifies which hosts <@record proxy.config.product_name> should track the congestion status of.
    </td>
  </tr>
  <tr>
    <td align="center">
      <@file_edit proxy.config.http.congestion_control.filename>
    </td>
  </tr>
</table>


<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
