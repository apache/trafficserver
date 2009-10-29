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
      <p>&nbsp;&nbsp;General Cache Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.cache.permit.pinning>Allow Pinning</td>
  </tr>
  <tr> 
    <td nowrap width="30%" class="bodyText"> 
      <input type="radio" name="proxy.config.cache.permit.pinning" value="1" <@checked proxy.config.cache.permit.pinning\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.cache.permit.pinning" value="0" <@checked proxy.config.cache.permit.pinning\0>>
        Disabled
    </td>
    <td width="70%" class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables cache pinning.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.cache.ram_cache.size>Ram Cache Size</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="10" name="proxy.config.cache.ram_cache.size" value="<@record proxy.config.cache.ram_cache.size>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the size of the RAM cache in Bytes.<br>
	<li>-1 = the RAM cache is automatically sized at approximately one MB per GB of disk.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.cache.max_doc_size>Maximum Object Size</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.cache.max_doc_size" value="<@record proxy.config.cache.max_doc_size>">
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the maximum size of documents in the cache.
        <li>0 = no size limit.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
