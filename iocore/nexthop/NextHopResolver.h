#include "NextHopHost.h"

namespace NextHop
{
struct Resolver {
  using Request = const Request &;

  /// Define the output lambda of the resolver
  /// returns true if output was successful (aka stop processing).
  using ResolverOutput = std::function<bool(Request, HostNamePtr &, HostAddr *)>;

  /// Define the request query
  using ResolverQuery = std::function<bool(Request, const Resolver &, ResolverOutput)>;

  ResolverQuery m_query;
  std::vector<Resolver *> m_children;

  bool
  query(Request req, ResolverOutput out)
  {
    return m_query(req, *this, out);
  }
  bool
  queryChildren(Request req, ResolverOutput out)
  {
    for (auto child : resolver.children) {
      if (child->m_query(req, *this, out)) {
        return; // stop once we have output
      }
    }
  }
};

bool
ResolveByCachedHostName(Request req, const Resolver &resolver, ResolverOutput out)
{
  HostNamePtr host = HostLookupByName(req->fqdn);
  if (host == nullptr) {
    return false;
  }
  host->getMutex().lock();
  auto addr_list = host->getAddrList();
  for (HostAddr *addr : addr_list) {
    if (out(req, host, addr)) {
      host->getMutex().unlock();
      return true;
    }
  }
  host->getMutex().unlock();
  return false;
}

bool
ResolveFilterAvailableIP(Request req, const Resolver &resolver, ResolverOutput out)
{
  auto out_filter = [&out](Request req, HostNamePtr &host, HostAddr *addr) {
    TSAssert(host->getMutex().has_lock());
    if (addr->m_available && addr->m_eol > time()) {
      if (out(req, host, addr)) {
        return true;
      }
    }
    return false;
  };

  return queryChildren(req, out_filter);
}

bool
ResolveSelectRandom(Request _req, const Resolver &resolver, ResolverOutput out)
{
  HostNamePtr rand_host = nullptr;
  HostAddr rand_addr    = nullptr;
  int count             = 0;

  auto select = [&](Request req, HostNamePtr &host, HostAddr *addr) {
    count++;
    bool keep = rand() % count == 0;
    if (keep) {
      rand_host = host;
      rand_addr = addr;
    }
    return false; // return false so we get all possible results.
  };

  queryChildren(req, select);
  if (count == 0) {
    return false;
  }
  if (out(req, rand_host, rand_addr)) {
    return true;
  }
  return false;
}

Resolver testResolver{ResolveSelectRandom, {{ResolveFilterAvailableIP, {{ResolveByCachedHostName, {}}}}}};

}; // end NextHop namespace