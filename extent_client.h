// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"
#include "lock_client_cache.h"

class extent_client {
 private:
  typedef int status;
  enum xxstatus { EMPTY, OK, DIRTY, DELETE };
  struct cache_data {
    std::string data;
    status data_state;
    extent_protocol::attr attrs;
    status attr_state;
    cache_data():data_state(EMPTY), attr_state(EMPTY) {}
  };

  rpcc *cl;
  std::map<extent_protocol::extentid_t, cache_data> caches;
  pthread_mutex_t m_lock;
 public:
  extent_client(std::string dst);
  ~extent_client();
  extent_protocol::status get(extent_protocol::extentid_t eid, std::string &buf);
  extent_protocol::status getattr(extent_protocol::extentid_t eid, extent_protocol::attr &a);
  extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  extent_protocol::status remove(extent_protocol::extentid_t eid);
  void flush(extent_protocol::extentid_t eid);
};

#endif 

