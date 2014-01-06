// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
  VERIFY(pthread_mutex_init(&m_lock, NULL) == 0);
}

lock_server_cache::~lock_server_cache()
{
  VERIFY(pthread_mutex_destroy(&m_lock) == 0);
}

int
lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  pthread_mutex_lock(&m_lock);
  if(lock_map.count(lid) == 0)
    lock_map[lid] = std::list<client_state>();
  
  if(lock_map[lid].empty()) {
    client_state cur = client_state(id);
    lock_map[lid].push_back(cur);
    pthread_mutex_unlock(&m_lock);
  } else {
    client_state *cur = &(lock_map[lid].back());
    lock_map[lid].push_back(client_state(id));
    if(cur->revoked == false) {
      handle h(cur->id);
      cur->revoked = true;
      pthread_mutex_unlock(&m_lock);
      rpcc* cl = h.safebind();
      if(cl) {
        int r;
        cl->call(rlock_protocol::revoke, lid, r);
        ret = lock_protocol::RETRY;
      } else
        ret = lock_protocol::RPCERR;
    } else {
      ret = lock_protocol::RETRY;
      pthread_mutex_unlock(&m_lock);
    }
  }
 
  return ret;
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&m_lock);
  lock_map[lid].pop_front();

  if(!lock_map[lid].empty()) {
    client_state *cur = &(lock_map[lid].front());
    if(cur->retried == false) {
      cur->retried = true;
      handle h(cur->id);
      pthread_mutex_unlock(&m_lock);
      rpcc* cl = h.safebind();
      if(cl) {
        int r;
        if(cl->call(rlock_protocol::retry, lid, r) == rlock_protocol::OK)
          ret = lock_protocol::OK;
        else
          ret = lock_protocol::RPCERR;
      } else
        ret = lock_protocol::RPCERR;
    } else
      pthread_mutex_unlock(&m_lock); 
    
  } else
    pthread_mutex_unlock(&m_lock);
 
  return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

