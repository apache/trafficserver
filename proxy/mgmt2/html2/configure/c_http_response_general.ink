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
      <p>&nbsp;&nbsp;General HTTP Response Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.body_factory.response_suppression_mode>Response Suppression Mode</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.body_factory.response_suppression_mode" value="1" <@checked proxy.config.body_factory.response_suppression_mode\1>>
	Always Suppressed <br>
      <input type="radio" name="proxy.config.body_factory.response_suppression_mode" value="2" <@checked proxy.config.body_factory.response_suppression_mode\2>>
	Intercepted Traffic Only <br>
      <input type="radio" name="proxy.config.body_factory.response_suppression_mode" value="0" <@checked proxy.config.body_factory.response_suppression_mode\0>>
	Never Suppressed
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies when <@record proxy.config.product_name>
            suppresses response pages.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
