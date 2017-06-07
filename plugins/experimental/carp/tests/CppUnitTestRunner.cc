// CppUnitTestRunner.cc
//
// Contains a main() that runs all unit tests registered
// in the same executable.  Register your
// CppUnit::TestFixture-derived class with macro:
// CPPUNIT_TEST_SUITE_REGISTRATION(MyClassXUnitTest)
//
// If you'd like XML output to a file, instead of just simple
// success/failure status sent to stdout, set environment variable
// "XUNIT_XML_FILE" to the path and filename to write to.

#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/XmlOutputter.h>
#include <cppunit/TextOutputter.h>
//#include <cppunit/extensions/TestFactoryRegistry.h>

#include <unistd.h>  // getopt()
#include <sstream>   // cout <<
#include <fstream>   // ofstream
#include <string>
#include <stdexcept> // exception, runtime_error
#include <memory>    // auto_ptr

using namespace std;

/**
 * Handy when writing results to an XML file.
 * Also writes "text" (not XML) results to stdout.
 */
class ComboOutputter : public CppUnit::Outputter
{
private:
    // Test run results will be available here.  (Don't delete,
    // this object does not own this object.)
    CppUnit::TestResultCollector* pResults;
    // XML file to write to.
    ofstream xmlFile;
    // Responsible for writing XML results to xmlFile.
    auto_ptr<CppUnit::XmlOutputter> xmlOutputterPtr;
    // Responsible for writing text results to stdout.
    auto_ptr<CppUnit::TextOutputter> textOutputterPtr;

// Construction
public:
    /**
     * IN   pResults  Pointer to the results object that write()
     *      will need to read.  (This results object won't be
     *      filled in yet, at the time this constructor is called.)
     * IN   xmlFilename  Filename (may include a path) to write
     *      XML results to, when write() is called.
     */
    ComboOutputter(CppUnit::TestResultCollector* pResults, const string& xmlFilename)
    {
        if (xmlFilename.empty()) {
            throw runtime_error("ComboOutputter::ComboOutputter() - Empty xmlFilename.");
        }

        // Create XML output file.
        xmlFile.exceptions(ofstream::failbit | ofstream::badbit);
        xmlFile.open(xmlFilename.c_str(), ofstream::out | ofstream::trunc);

        // Create the "outputters" we'll need.
        xmlOutputterPtr.reset(new CppUnit::XmlOutputter(pResults, xmlFile));
        textOutputterPtr.reset(new CppUnit::TextOutputter(pResults, cout));
    }

    virtual ~ComboOutputter()
    {
        // Flush/close output file.
        try {
            xmlFile.close();
        } catch (...) {}
    }

// Public API.
public:
    // Called by Runner, after it is done running the tests.
    // Writes XML results to a file, and text results to stdout.
    virtual void write()
    {
        xmlOutputterPtr->write();
        textOutputterPtr->write();
    }
};

/**
 * Output a command-line "usage" message.
 * IN   cmd  Name of this executable.
 */
static void
usage(const string& cmd)
{
    cout
    << "Usage: " << cmd << " [-x filename]" << endl
    << "Runs all unit tests registered in this executable." << endl
    << endl
    << "Register your CppUnit::TestFixture-derived class" << endl
    << "'ExampleTest', with macro:" << endl
    << "CPPUNIT_TEST_SUITE_REGISTRATION(ExampleTest)" << endl
    << endl
    << "By default, a 'text' summary of test results is" << endl
    << "written to stdout/stderr." << endl
    << endl
    << "args:" << endl
    << "-x filename  (optional)" << endl
    << "  If specified, test results are written to the specified" << endl
    << "  filename (may include a directory path) in an XML format." << endl
    << "  (Fails, if directory path does not exist.)" << endl;
}

/**
 * Runs all unit tests registered in this executable.
 * See usage().
 */
int main(int argc, char** argv)
{
    try {
        // If not empty, write results as XML, to this file.
        string xmlFilename;

        // For each command-line option,
        int ch;
        while ((ch = getopt(argc, argv, "x:")) != -1)
        {
            switch (ch)
            {
            // XML results file
            case 'x': {
                xmlFilename = optarg;
                break;
            }
            // Unknown switch
            case '?': default: {
                cout << "Unknown switch." << endl;
                usage(argv[0]);
                return 1;
            }
            }
        }

        // Find all tests linked into this executable.
        CppUnit::TestFactoryRegistry &registry
            = CppUnit::TestFactoryRegistry::getRegistry();

        // Create a runner that will run all of these tests.
        CppUnit::TextUi::TestRunner runner;
        runner.addTest(registry.makeTest());

        // If writing results to XML file,
        ofstream xmlFile;
        if (!xmlFilename.empty())
        {
            // Tell runner it will write test output to an XML file,
            // as well as text output to stdout.
            runner.setOutputter(new ComboOutputter(
                    &(runner.result()), xmlFilename));

            // Report that output is headed to XML file.
            cout << "Writing unit test results to XML file ("
                 << xmlFilename << ")." << endl;
        } 

        // Run the tests.
        bool success = runner.run();

        // Return success/failure to shell.
        return ((success) ? 0 : 1);
    }
    catch (exception& e)
    {
        cout << "Failure due to: " << e.what() << endl;
        return 1;
    }
}

////
