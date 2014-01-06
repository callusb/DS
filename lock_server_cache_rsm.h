#ifndef lock_server_cache_rsm_h
#define lock_server_cache_rsm_h

#include <string>

#include "lock_protocol.h"
#include "rpc.h"
#include "rsm_state_transfer.h"
#include "rsm.h"


class lock_server_cache_rsm : public rsm_state_transfer {
 protected:
  enum xxstatus { FREE, LOCKED, REVOKING, RETRYING };
  typedef int status;
  struct lock_state {
    status state;
    std::string owner;
    std::string retryid;
    std::list<std::string> waitids;
  };
  struct client_info {
    std::string cid;
    lock_protocol::lockid_t lid;
  };
 private:
  int nacquire;
  class rsm *rsm;
  std::map<lock_protocol::lockid_t, lock_state> lock_map;
  std::list<client_info> retry_list;
  std::list<client_info> revoke_list;
  lock_state* get_lock(lock_protocol::lockid_t lid);
  pthread_mutex_t lm_lock;
  pthread_mutex_t retry_lock;
  pthread_cond_t retry_cond;
  pthread_mutex_t revoke_lock;
  pthread_cond_t revoke_cond;
 public:
  lock_server_cache_rsm(class rsm *rsm = 0);
  ~lock_server_cache_rsm();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  std::string marshal_state();
  void unmarshal_state(std::string state);
  int acquire(lock_protocol::lockid_t, std::string id, 
	      lock_protocol::xid_t, int &);
  int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
	      int &);
};

#endif
