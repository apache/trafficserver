/////////////////////////////////////////////////////////////////////
// bypass.config form and rule contains following:
//                              (Input Type Names)
//    Rule Type                      rule_type
//    Source IP Address(es)	     src_ip
//    Destination IP Address(es)     dest_ip
// ** NOTE: the input type names must match those created in 
//          writeBypassConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(rule_type, src_ip, dest_ip) {
 this.rule_type = rule_type; 
 this.src_ip = src_ip;
 this.dest_ip = dest_ip;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var index = document.form1.rule_type.selectedIndex;
  var rule_type = document.form1.rule_type.options[index].value;

  var src_ip = document.form1.src_ip.value;
  var dest_ip = document.form1.dest_ip.value;

  var rule = new Rule(rule_type, src_ip, dest_ip);

  return rule;
}

//
// This function displays the Rule object's information in the format
// that's used in the select list.
//
function textFormat(rule, write)
{
  var text = "";
  var delim = "";
  var eq = "=";  
  var space = "";
  if (write == 1) {
     space = "&nbsp;&nbsp;";
  } else {  
     space = "  ";
  }
  delim = space + "," + space;

  text = "Rule Type" + eq + rule.rule_type;
  if (rule.src_ip != "") text += delim + "Source IP" + eq + rule.src_ip;
  if (rule.dest_ip != "") text += delim + "Destination IP" + eq + rule.dest_ip;

  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 
  var text = rule.rule_type + delim + rule.src_ip + delim + rule.dest_ip + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel = document.form1.rule_type.selectedIndex; 
  ruleList[index].rule_type =  document.form1.rule_type.options[sel].value;
  ruleList[index].src_ip = document.form1.src_ip.value;
  ruleList[index].dest_ip = document.form1.dest_ip.value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values stored in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;

  for (var i=0; i < document.form1.rule_type.length; i++) {
     if (document.form1.rule_type.options[i].value == ruleList[index].rule_type)
       document.form1.rule_type.selectedIndex = i;
  }
  document.form1.src_ip.value = ruleList[index].src_ip;
  document.form1.dest_ip.value = ruleList[index].dest_ip;	
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.rule_type.value = "bypass"; 
  document.form1.src_ip.value = "";
  document.form1.dest_ip.value = "";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
   var rule_index = document.form1.rule_type.selectedIndex;

  if (rule_index < 0 || document.form1.rule_type.options[rule_index].value == ""){
    alert("Need to specify 'Rule Type'.");
    return false;
  }

  if (document.form1.src_ip.value == "" && document.form1.dest_ip.value == "") {
    alert("Need to specify at least a source or destination IP addresses"); 
    return false;
  } 

  return true;   	
}
