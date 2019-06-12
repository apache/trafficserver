
# CurlHeader Tester

This tester tests for the output of curl. User should provide a reference python dictionary whose key-value pair should be (header, op_value) for each line of the expected curl output. For header field, user should provide a string that matches case-insensitively the expected header. For op_value field, which performs comparison operations against the actual value corresponding to that header, use can provide one of the following inputs:
 * **A string**: If provided a string, it will be compared with the actual value for an exact match.
 * **A dictionary**: If provided a dictionary, the key-value pair should be (op, value) or (op, [values]), where 'op' indicates the operation to perform with the actual value and the expected value. Following are the available operations:
    * **equal**: This operation tries to find an exact match. In case of given a single string, it checks for exact match between it and the actual value. In case of given a list of strings, it checks for exact match with any string of the list.
    * **equal_re**: This operation tries to find a regular expression match . In case of give a single python regular expression string, it checks if the actual value matches the expression. In case of given a list of regular expressions, it checks if the actual value can match any one of the expressions.
 * **NoneType**: If provided a None, the tester will ignore the value for this header

To use the CurlHeader tester when writing tests, which is not one of the default testers, user should invoke the tester in the following way:
 1. tr = Test.AddTestRun()
 2. tr.Processes.Default.Streams.stdout = Testers.CurlHeader(reference_dict)

 Examples:
 * To check for header 'X-Cache' that have value 'miss' and header 'cache-control' that have value starting with 'max', provide the following dictionary to tester:
    
        {    
            'X-Cache' : 'miss',   
            'cache-control' : {'equal_re' : 'max.*'}
        }

 * To check for header 'Age' that can have any value and header 'etag' that have value either matching 'myetag' or end with 'p', provide the following dictionary to tester:
 
        {
            'Age' : None,
            'etag' : {
                'equal' : 'myetag',
                'equal_re' : '.*p'
            }
        }