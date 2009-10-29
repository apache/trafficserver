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
      <p>&nbsp;&nbsp;Windows Media Multicast Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.mixt.wmtmcast.enabled>Multicast</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.mixt.wmtmcast.enabled" value="1" <@checked proxy.config.mixt.wmtmcast.enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.mixt.wmtmcast.enabled" value="0" <@checked proxy.config.mixt.wmtmcast.enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables <@record proxy.config.product_name> multicasting for Windows Media.
      </ul>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>

