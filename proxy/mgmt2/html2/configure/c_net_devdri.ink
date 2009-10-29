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

