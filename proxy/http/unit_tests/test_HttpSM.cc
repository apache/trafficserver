#include <catch.hpp>

#include "apidefs.h"
#include "CacheControl.h"
#include "Http1ClientSession.h"
#include "Http1ClientTransaction.h"
#include "HttpConfig.h"
#include "HttpSessionAccept.h"
#include "HttpSM.h"
#include "I_IOBuffer.h"
#include "I_VConnection.h"
#include "InkAPIInternal.h"
#include "P_SSLNetVConnection.h"
#include "ParentSelection.h"
#include "ReverseProxy.h"

#include <cstring>

class Http1ClientTestSession final : public Http1ClientSession
{
public:
  int get_transact_count() const override;
  void set_transact_count(int count);
  void set_vc(NetVConnection *new_vc);

private:
  int m_transact_count{0};
};

int
Http1ClientTestSession::get_transact_count() const
{
  return m_transact_count;
}

void
Http1ClientTestSession::set_transact_count(int count)
{
  m_transact_count = count;
}

void
Http1ClientTestSession::set_vc(NetVConnection *new_vc)
{
  _vc = new_vc;
};

TEST_CASE("tcp_reused should be set correctly when a session is attached.")
{
  init_reverse_proxy();
  initCacheControl();
  HttpConfig::startup();
  ParentConfig::startup();

  HttpSM sm;
  sm.init();

  Http1ClientTestSession ssn;
  SSLNetVConnection netvc;
  ssn.set_vc(&netvc);
  HttpSessionAccept::Options options;
  ssn.accept_options = &options;

  Http1ClientTransaction txn{&ssn};
  IOBufferReader reader;
  txn.set_reader(&reader);

  http_global_hooks = new HttpAPIHooks;

  SECTION("When a transaction is the first one, "
          "then tcp_reused should be false.")
  {
    ssn.set_transact_count(1);
    sm.attach_client_session(&txn);
    CHECK(sm.get_client_tcp_reused() == false);
  }

  SECTION("When a transaction is the second one, "
          "then tcp_reused should be true.")
  {
    ssn.set_transact_count(2);
    sm.attach_client_session(&txn);
    CHECK(sm.get_client_tcp_reused() == true);
  }
}
