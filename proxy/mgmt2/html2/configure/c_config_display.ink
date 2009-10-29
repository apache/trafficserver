<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<html>
<head>
  <title><@record proxy.config.manager_name> UI</title>
  <meta http-equiv="pragma" content="no-cache">
  <meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
  <script language="JavaScript1.2" src="/sniffer.js" type="text/javascript"></script>
  <script language="JavaScript1.2">
  if(!is_win && !is_mac)
    document.write("<link rel='stylesheet' type='text/css' href='/inktomiLarge.css' title='Default'>");
  else
    document.write("<link rel='stylesheet' type='text/css' href='/inktomi.css' title='Default'>");
  </script>
  <!--<link rel="stylesheet" type="text/css" href="/inktomi.css" title="Default">-->
</head>

<script language="JavaScript1.2" src="/resize.js" type="text/javascript"></script>

<body class="primaryColor">

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr> 
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="whiteColor" width="100%" align="left" valign="top">

<form name="form1" method=POST action="/submit_update_config.cgi?fname=<@query fname>">
<script language="JavaScript">
<!--

// this writes the JS regarding the Rule object and ruleList
<@dynamic_javascript>

//
// sets the number of rules in the list
//
function setLength() { 
 var k=0; 
 while(ruleList[k] != null) k++; 
 ruleList.length = k; 
}  
setLength();


// ----------------- Generic ruleList Functions ------------------ 

//
// this removes the rule at "index" from ruleList and shifts all the other rules up
//
function removeRule(index)
{
  if (index < 0) {
    alert("No rule selected. Cannot delete.");
    return false;
  }

  ruleList[index] = null;
  for (var i=index; i < ruleList.length-1; i++) {
     ruleList[i] = ruleList[i+1]; 
  }
  ruleList.length--;

  // set last rule to null ??
  ruleList[ruleList.length] = null; 

  // update the UI
  if (ruleList.length == 0) { // deleted last element
     clearForm();
     document.form1.list1.options[0].text= " No rules specified                                        "; 
  } else if (index == 0) {
	//document.form1.list1.selectedIndex = index+1; 
  } else {
	document.form1.list1.selectedIndex = index-1; 
  }
}

//
// creates the rule, inserts it and shifts all other rules in ruleList down
// 
function insertRule(index)
{
  var rule = createRuleFromForm();
  
  for (var i=ruleList.length; i > index; i--) {
    ruleList[i] = ruleList[i-1]; 
  }

  ruleList[index] = rule;
  ruleList.length++; 
}

// ----------------- SELECT LIST FUNCTIONS -------------------
// (None of these functions should be specific to a config file)

// 
// adds the information in the rule's fields to the select list
// 
function append() {
  if (ruleList.length == MAX_RULES-1) {
     alert("Please commit your changes now by hitting 'Apply'"); 
  } 

  if (ruleList.length >= MAX_RULES) {
     alert ("Rule cannot be added until you commit your changes by hitting 'Apply'"); 
     return false;
  }

  if (!validInput()) {
    return false;
  }

  var list = document.form1.list1; 

  // create a Rule object from values in the form fields
  var rule = createRuleFromForm();

  // add the Rule object to ruleList array
  var index = ruleList.length;
  ruleList[index] = rule;
  ruleList.length++;

  // add the rule to the select form element list
  var item = new Option();
  item.value = index;
  item.text = textFormat(ruleList[index], 0);
  list.options[index] = item;

  list.selectedIndex = index;  
}

//
// this inserts a new Rule (created from current data in the form fields)
// below the index of the selected item
//
function insert() {
  if (!validInput()) {
    return false;
  }
  
  var list = document.form1.list1; 
  var index = list.selectedIndex; 

  // update the ruleList 
  insertRule(index);
 
  // insert the item in the select list
  // first shift all items down
  list.options[ruleList.length-1] = new Option(); 
  for (var i=ruleList.length-1; i > index; i--) {
    list.options[i].value = list.options[i-1].value;
    list.options[i].text = list.options[i-1].text;
  }

  list.options[index].value = ruleList[index].url; 
  list.options[index].text = textFormat(ruleList[index],0); 
 
  list.selectedIndex = index;   
}

//
// This function is actually implemented such that it will 
// remove mutlipe-selected items on the list; need to also
// remove the rule from ruleList
//
function remove() {
  var index = document.form1.list1.selectedIndex; 
  if (index < 0) {
    alert("No rule selected. Cannot delete.");
    return false;
  }

  if (index >= ruleList.length || ruleList.length == 0) {
    return false;
  }

  var list = document.form1.list1;
  var numDel = 0;
  for (var i=0; i<list.options.length; i++) {
    if (list.options[i].selected && list.options[i] != "") {
      list.options[i].value = "";
      list.options[i].text = "";
      removeRule(i-numDel);
      numDel++;
    }
  }
  shiftUp(list);

  updateForm(-1);	
}

// 
// Shifts all non-empty options up so that all empty options 
// end up at the bottom; recursive. 
// 
function shiftUp(list){
  if (ruleList.length == 0)
	return false;

  for(var i =0; i<list.options.length; i++) {
    if(list.options[i].value=="") {
      for (var j=i; j<list.options.length-1; j++) {
         list.options[j].value = list.options[j+1].value;
         list.options[j].text = list.options[j+1].text; 
       }
       var ln = i;
       break;
    }
  }
  if (ln < list.options.length) {
    list.options.length -= 1;
    shiftUp(list);
  }
}

//
// Moves the selected rule up one. 
// list is list of options from the select form element
//
function moveUp()
{
  var list = document.form1.list1; 
  var index = list.selectedIndex;

  // check if any item is selected 
  if (index < 0) {
    alert("No rule selected. Cannot shift rule up.");
    return false; 
  }

  // cannot shift top-most element up or if it's a blank value
  if (list.options[index] == "" || list.options[index].value == "" || index == 0) 
    return false;

  // switch the order in the select form element
  var tmpVal = list.options[index].value;
  var tmpText = list.options[index].text;
  list.options[index].value = list.options[index-1].value;
  list.options[index].text = list.options[index-1].text;
  list.options[index-1].value = tmpVal;
  list.options[index-1].text = tmpText;

  // switch the ruleList array  
  var tmpRule = ruleList[index];
  ruleList[index] = ruleList[index-1];
  ruleList[index-1] = tmpRule;

  list.selectedIndex = index-1;
  updateForm(-1);
}

//
// Moves the selected rule down one
// 
function moveDown(list)
{
  var list = document.form1.list1; 
  var index = list.selectedIndex;

  // check if any item is selected 
  if (index < 0){
    alert("No rule selected. Cannot shift rule down."); 
    return false;
  }

  // cannot shift bottom-most element; or if bottom-most element is blank
  if (list.options[index] == "" || index == list.options.length-1 || list.options[index+1].value == "") 
    return false;

  // switch the order in the select form element
  var tmpVal = list.options[index].value;
  var tmpText = list.options[index].text;
  list.options[index].value = list.options[index+1].value;
  list.options[index].text = list.options[index+1].text;
  list.options[index+1].value = tmpVal;
  list.options[index+1].text = tmpText;

  // switch the ruleList array  
  var tmpRule = ruleList[index];
  ruleList[index] = ruleList[index+1];
  ruleList[index+1] = tmpRule;

  list.selectedIndex = index+1;
  updateForm(-1);
}


// 
// updates the select list and the ruleList with the 
// values entered in the form fields
//
function modify() 
{
   var index = document.form1.list1.selectedIndex;
   if (index < 0) {
     alert("No rule selected. Can only modify a selected rule from the rule display box."); 
     return false;    
   }

   if (document.form1.list1.options[index].value == "") {
     return false; 
   }

   if (!validInput()) {
    updateForm(-1);
    return false;
   }

   // update ruleList first
   updateRule(index);

   // update the select list
   document.form1.list1.options[index].text = textFormat(ruleList[index], 0);
}

function updateCheck() {
  var index = document.form1.list1.selectedIndex;
  if (document.form1.list1.options[index].value == "") {
    clearForm();
    return false; 
  }
  updateForm(index); 
} 


//------------------ Generic Write HTML functions --------------------

// set the maximum number of rules that can be in the table;
// the number of additional rules users can add must equal
// MAX_ADD_RULES in WebHttp.cc 
var MAX_RULES = ruleList.length + 50; 


function writeSelectList()
{
  var text = "";
  var size;
  if (ruleList.length < MAX_RULES)
     size = MAX_RULES;
  else
     size = ruleList.length;

  var spaces_tmp = "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
  var spaces = spaces_tmp + spaces_tmp + spaces_tmp + spaces_tmp;
  document.write('<select class="bodyText" size=' + 10  + ' name="list1" onChange="updateCheck()">');
  if (ruleList.length == 0) {
     document.write('<option class="bodyText" value=0> &nbsp; No rules specified' + spaces_tmp +  '</option>');
  } else {
   for(var i=0; i < ruleList.length; i++) {
     text = textFormat(ruleList[i], 1);
     if (i==0) {
       document.write('<option class="bodyText" value="' + i + '">' + text + '</option>');
     } else { 
       document.write('<option class="bodyText" value="' + i + '">' + text + '</option>');
     }
   } 
  }

  // need to make a blank rule that will expand initial size of select box
  document.write('<option value="">');
  for (i=0; i < 150; i++) {
    document.write('&nbsp;');
  }
  document.write('</option>'); 
  document.write('</select>'); 
}

function writeSelectTable()
{
  document.write('<table cellspacing="0" cellpadding="0" width="100%" height="100%" border="0">\n');
  document.write('  <tr>\n');
  document.write('    <td height="100%" align="right" valign="top">\n'); 
  writeSelectButtons();
  document.write('    </td>\n');
  document.write('    <td width="100%">\n');
  writeSelectList();
  document.write('    </td>\n');
  document.write('  </tr>\n');
  document.write('</table>');
}  

function writeSelectButtons()
{
  document.write('<table cellspacing="0" cellpadding="1" height="100%" width="100%">');
  document.write('<tr><td height="100%" align="right" valign="top">');
  document.write('<a href="" onclick="moveUp(); return false;">');
  document.write('<img src="/images/arrow_up.gif" alt="move the selected rule up" width="20" border=0></a><br>');
  document.write('<a href="" onclick="remove(); return false;">');
  document.write('<img src="/images/arrow_cross.gif" alt="delete the selected rule" width="20" border=0></a><br>');
  document.write('<a href="" onclick="moveDown(); return false;">');
  document.write('<img src="/images/arrow_down.gif" alt="move the selected rule down" width="20" border=0></a><br>');
  document.write('</td></tr></table>');
}

//
// NEED TO MAKE SURE THESE HIDDEN TAGS ARE FIRST FORMS OF AN ELEMENT!!!
// MAX_RULES = the # of rules currently in the config file + # (# of
// config rules user can add) ? 
// 
// when user wants to submit changes, need to rewrite the document form so that
// the Rule objects in ruleList are written as hidden elements in the form; so 
// first need to write placeholders for hidden tags 

function writeHiddenTags()
{
  // write a bunch of "empty" hidden tags for any new rules added
  for (i=0; i < MAX_RULES; i++) {
    document.write('<input type="hidden" name="rule' + i+ '" value="">');
  }
}

//
// When submitting changes to file,need to update the hidden tag values so they reflect
// the data in the ruleList
// 
function updateHiddenTags()
{
  if (ruleList.length > MAX_RULES) { 
	alert ("Too many rules. Cannot 'Apply' changes.");
	return false;
  } 

  for (var i=0; i < ruleList.length; i++) {
    document.form1.elements[i].value = hiddenFormat(ruleList[i]);
  }
  document.form1.elements[ruleList.length].value = ""; // end marker
}

// MUST WRITE THIS FIRST ON FORM!!! 
writeHiddenTags();

// -->
</script>

<!--
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>
-->

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
  <tr class="hilightColor"> 
    <td class="hilightColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td width="100%" height="10"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="hilightColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
  <tr> 
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
    <td class="tertiaryColor" width="2" height="2"><img src="/images/dot_clear.gif" width="2" height="2"></td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Configuration File Editor - <@query fname> </p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onclick="updateHiddenTags()">
    </td>
    <td>
      <input class="configureButton" type=submit name="close" value=" Close " onclick="window.close()" >
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">	
  <tr>
    <td align="left" colspan=2>
      <@submit_error_msg>
    </td>
    <td class="secondaryFont" align="right" nowrap colspan=1>
      <a class="help" href="<@help_config_link>" target="help_window">Get Help!&nbsp;&nbsp;</a>
    </td>
  </tr>
  <tr>
    <td>
      <script language="JavaScript">
        <!-- 
          writeSelectTable();  
        // --> 
      </script>
      <br>

      <table border="1" cellspacing="0" cellpadding="0" width="100%" bordercolor=#CCCCCC>
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>
                <td colspan="3">
                  <a href="" onclick="append(); return false;">
                    <img width="30" height="20" border=0
                     src="/images/arrow_add.gif"
                     alt="Append this rule to the display box above">
                  </a>
                  <a href="" onclick="modify(); return false;">
                    <img width="30" height="20" border=0
                     src="/images/arrow_set.gif"
                     alt="Update the change to the display box above">
                  </a>
                </td>
              </tr>

              <@config_input_form>

              <tr>
                <td align="right" colspan="3">
                  <input class="configureButton" type=button value="Clear Fields" onclick="clearForm()">
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onclick="updateHiddenTags()">
    </td>
    <td>
      <input class="configureButton" type=submit name="close" value=" Close " onclick="window.close()" >
    </td>
  </tr>
</table>

<@include /monitor/m_footer.ink>
</form>

</body>
<@include /include/html_footer.ink>
