/////////////////////////////////////////////////////////////////////
// partition.config form and rule contains following:
//    Partition #                   part_num
//    Scheme Type (http/mixt)       scheme
//    partition size                size
//    Size Format (absolute/%)      size_format
// ** NOTE: the input type names must match those created in 
//          writePartitiononfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(part_num , scheme, size, size_format) {
 this.part_num = part_num;
 this.scheme = scheme;
 this.size = size;
 this.size_format = size_format;
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var index;

  var part_num = document.form1.part_num.value;

  index = document.form1.scheme.selectedIndex; 
  var scheme = document.form1.scheme.options[index].value;

  var size = document.form1.size.value;
  
  index = document.form1.size_format.selectedIndex; 
  var size_format = document.form1.size_format.options[index].value;

  var rule = new Rule(part_num, scheme, size, size_format);

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

  text = "Partition" + eq + rule.part_num + delim + "Scheme" + eq + rule.scheme + delim + "Size" + eq + rule.size;
   
  if (rule.size_format == "absolute") 
	text +=  " MB"
  else if (rule.size_format == "percent")
	text += " %" 
  	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^"; 

  var text = rule.part_num + delim + rule.scheme + delim + rule.size + delim + rule.size_format + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel;

  ruleList[index].part_num = document.form1.part_num.value;
  
  sel = document.form1.scheme.selectedIndex; 
  ruleList[index].scheme =  document.form1.scheme.options[sel].value;

  ruleList[index].size = document.form1.size.value;

  sel = document.form1.size_format.selectedIndex; 
  ruleList[index].size_format =  document.form1.size_format.options[sel].value;  
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
 
  var i;

  document.form1.part_num.value = ruleList[index].part_num;	

  for (i=0; i < document.form1.scheme.length; i++) {
     if (document.form1.scheme.options[i].value == ruleList[index].scheme)
       document.form1.scheme.selectedIndex = i;
  }

  document.form1.size.value = ruleList[index].size;	

  for (i=0; i < document.form1.size_format.length; i++) {
     if (document.form1.size_format.options[i].value == ruleList[index].size_format)
       document.form1.size_format.selectedIndex = i;
  }
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.part_num.value = "";
  document.form1.size.value = "";
  document.form1.scheme.value = "http";
  document.form1.size_format.value = "absolute";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var scheme_index = document.form1.scheme.selectedIndex;
  var size_index = document.form1.size_format.selectedIndex;

  if (scheme_index < 0 || size_index < 0 || 
	document.form1.part_num.value == "" ||
	document.form1.size.value == "") {
	alert("Need to specify all fields");
	return false;
  }

  if (document.form1.part_num.value < 1 || document.form1.part_num.value > 255) {
	alert ("'Partition Number' must be between 1 and 255");
	return false; 
  }

  if (document.form1.size_format.options[size_index].value == "percent") {
    if (document.form1.size.value > 100) {
	alert ("Partition size cannot exceed 100%");
	return false; 
    }
  } 

  return true;   	
}
