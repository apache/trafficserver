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
      <p>&nbsp;&nbsp;FTP Cacheability Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ftp.cache_enabled>FTP Caching</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.ftp.cache_enabled" value="1" <@checked proxy.config.ftp.cache_enabled\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.ftp.cache_enabled" value="0" <@checked proxy.config.ftp.cache_enabled\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables FTP objects to be put in the cache.
        <li>If this option is disabled, <@record proxy.config.product_name> 
            always serves FTP objects from the FTP server.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Directory Caching</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.simple_directory_listing_cache_enabled>Simple</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ftp.simple_directory_listing_cache_enabled" value="1" <@checked proxy.config.ftp.simple_directory_listing_cache_enabled\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.ftp.simple_directory_listing_cache_enabled" value="0" <@checked proxy.config.ftp.simple_directory_listing_cache_enabled\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables caching of directory listings without arguments.
                    <li>For example, dir/ls
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.full_directory_listing_cache_enabled>Full</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ftp.full_directory_listing_cache_enabled" value="1" <@checked proxy.config.ftp.full_directory_listing_cache_enabled\1>>
                    Enabled <br>
                  <input type="radio" name="proxy.config.ftp.full_directory_listing_cache_enabled" value="0" <@checked proxy.config.ftp.full_directory_listing_cache_enabled\0>>
                    Disabled
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Enables/Disables caching of directory listings with arguments.
                    <li>For example, ls -al, ls *.txt
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
    <td height="2" colspan="2" class="configureLabel">Freshness</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.login_info_fresh_in_cache_time>Login Information</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.login_info_fresh_in_cache_time" value="<@record proxy.config.ftp.login_info_fresh_in_cache_time>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies how long (in seconds) the 220/230 responses (login messages)
                        can stay fresh in the cache.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.directory_listing_fresh_in_cache_time>Directory Listings</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.directory_listing_fresh_in_cache_time" value="<@record proxy.config.ftp.directory_listing_fresh_in_cache_time>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies how long (in seconds) directory listings can stay fresh in the cache.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ftp.file_fresh_in_cache_time>Files</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="6" name="proxy.config.ftp.file_fresh_in_cache_time" value="<@record proxy.config.ftp.file_fresh_in_cache_time>">
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies how long (in seconds) FTP files can stay fresh in the cache.
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

