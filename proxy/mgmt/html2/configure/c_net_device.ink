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

<tr> 
    <td height="2" colspan="2" class="configureLabel">Interface netdev Parameters</td>
</tr>
<tr> 
  <td colspan="2">
    <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC  width="100%">
      <tr><td>
      <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg NIC_netdev_enabled>Enable/Disable</td>
         </tr>
         <tr> 
           <td nowrap class="bodyText">
             <input type="radio" name="NIC_netdev_enabled" value="1" <@network NIC_netdev_up>>
               Enabled <br>
             <input type="radio" name="NIC_netdev_enabled" value="0" <@network NIC_netdev_down>>
               Disabled
             </td>
           <td class="configureHelp" valign="top" align="left"> 
             <ul>
               <li>Up/Down the network interface
             </ul>
           </td>
         </tr>
        <tr>
          <td height="2" colspan="2" class="configureLabelSmall">Onboot
          </td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="NIC_netdev_ONBOOT" value="1" <@network NIC_netdev_boot_enable>>
               Yes <br>
             <input type="radio" name="NIC_netdev_ONBOOT" value="0" <@network NIC_netdev_boot_disable>>
               No
             </td>
           <td class="configureHelp" valign="top" align="left">
             <ul>
               <li>Start the interface on boot.
             </ul>
           </td>
         </tr>
         <tr> 
           <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg NIC_netdev_IPADDR>IP Address</td>
         </tr>
         <tr>
           <td nowrap class="bodyText">
           <input type="text" size="18" name="NIC_netdev_IPADDR" value="<@network NIC_netdev_IPADDR>">
           </td>
           <td class="configureHelp" valign="top" align="left"> 
             <ul>
               <li>Specifies the ip address for this interface
             </ul>
           </td>
         </tr>

         <tr>
           <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg NIC_netdev_NETMASK>Netmask</td>
         </tr>
              
	 <tr>
           <td nowrap class="bodyText">
             <input type="text" size="18" name="NIC_netdev_NETMASK" value="<@network NIC_netdev_NETMASK>">
           </td>
           <td class="configureHelp" valign="top" align="left">
             <ul>
               <li>Specifies the netmask for this interface
             </ul>
           </td>
         </tr>

         <tr>
           <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg NIC_netdev_GATEWAY>Gateway</td>
         </tr>

         <tr>
           <td nowrap class="bodyText">
             <input type="text" size="18" name="NIC_netdev_GATEWAY" value="<@network NIC_netdev_GATEWAY>">
           </td>
           <td class="configureHelp" valign="top" align="left">
             <ul>
               <li>Specifies the gateway for this interface
             </ul>
           </td>
         </tr>
      
      </table>
      </td></tr>
    </table>
  </td>
</tr>

