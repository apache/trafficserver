#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/PluginInit.h>

#include <iostream>

using namespace atscppapi;
using std::string;
using std::cout;
using std::endl;

class Intercept : public InterceptPlugin {
public:
  Intercept(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT) { }
  void consume(const string &data, InterceptPlugin::RequestDataType type);
  void handleInputComplete();
  ~Intercept() { cout << "Shutting down" << endl; }
};

class InterceptInstaller : public GlobalPlugin {
public:
  InterceptInstaller() : GlobalPlugin(true /* ignore internal transactions */) {
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
  }
  void handleReadRequestHeadersPreRemap(Transaction &transaction) {
    transaction.addPlugin(new Intercept(transaction));
    cout << "Added intercept" << endl;
    transaction.resume();
  }
};

void TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ []) {
  new InterceptInstaller();
}

void Intercept::consume(const string &data, InterceptPlugin::RequestDataType type) {
  if (type == InterceptPlugin::REQUEST_HEADER) {
    cout << "Read request header data" << endl << data;
  }
  else {
    cout << "Read request body data" << endl << data << endl;
  }
}

void Intercept::handleInputComplete() {
  cout << "Request data complete" << endl;
  string response("HTTP/1.1 200 OK\r\n"
                  "Content-Length: 7\r\n"
                  "\r\n");
  InterceptPlugin::produce(response);
//  sleep(5); TODO: this is a test for streaming; currently doesn't work
  response = "hello\r\n";
  InterceptPlugin::produce(response);
  InterceptPlugin::setOutputComplete();
}
