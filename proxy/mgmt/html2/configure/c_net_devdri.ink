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
    <td height="2" colspan="2" class="configureLabel">Device Driver for Interface netdev</td>
</tr>
<tr> 
  <td colspan="2">
    <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC  width="100%">
      <tr><td>
      <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr>
          <td height="2" colspan="2" class="configureLabelSmall">Auto-Negotiation</td>
         </tr>
         <tr>
           <td nowrap class="bodyText">
             <input type="checkbox" name="driver_netdev_auto" value="1">
               Auto-Negotiation
             </td>
           <td class="configureHelp" align="left">
             <ul>
               <li>Set up the driver through auto negotiation
             </ul>
           </td>
         </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall">Speed</td>
         </tr>
         <tr> 
           <td nowrap class="bodyText">
             <input type="radio" name="driver_netdev_speed" value="0">
               10 Mbps <br>
             <input type="radio" name="driver_netdev_speed" value="1">
               100 Mbps
             </td>
           <td class="configureHelp" valign="top" align="left"> 
             <ul>
               <li>Set the data transfer speed
             </ul>
           </td>
         </tr>
        <tr>
          <td height="2" colspan="2" class="configureLabelSmall">Transfer Mode
          </td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="driver_netdev_mode" value="0">
               Half-Duplex <br>
             <input type="radio" name="driver_netdev_mode" value="1">
               Full-Duplex
             </td>
           <td class="configureHelp" valign="top" align="left">
             <ul>
               <li>Set the communication mode
             </ul>
           </td>
         </tr>
      </table>
      </td></tr>
    </table>
  </td>
</tr>

