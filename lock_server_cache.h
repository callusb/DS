#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
 public:
  enum xxstatus{ FREE, LOCKED };
  typedef int status;
  struct client_state {
    std::string id;
    bool revoked;
    bool retried;
    client_state(std::string id): id(id), revoked(false), retried(false) {} 
  };

/*  struct lock_state {
    status state;
    std::list<client_state> clients;
    lock_state(): state(FREE) {} 
  };*/

 private:
  int nacquire;
  std::map<lock_protocol::lockid_t, std::list<client_state> > lock_map;
  pthread_mutex_t m_lock;
  pthread_cond_t m_cond;
 public:
  lock_server_cache();
  ~lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
