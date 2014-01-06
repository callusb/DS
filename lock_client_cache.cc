// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


lock_client_cache::lock_client_cache(std::string xdst, class lock_release_user *_lu) : lock_client(xdst), lu(_lu)
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();

  VERIFY(pthread_mutex_init(&c_lock, NULL) == 0);
  VERIFY(pthread_cond_init(&c_cond, NULL) == 0);
}

lock_client_cache::~lock_client_cache() {
  VERIFY(pthread_mutex_destroy(&c_lock) == 0);
  VERIFY(pthread_cond_destroy(&c_cond) == 0);
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;

  pthread_mutex_lock(&c_lock);
  if(cache_map.count(lid) == 0)
    cache_map[lid] = lock_state();

  if(cache_map[lid].state == NONCE || cache_map[lid].state == RELEASING) {
    cache_map[lid].state = ACQUIRING; 
    cache_map[lid].refcount++;
    pthread_mutex_unlock(&c_lock);
   connect:
    int r;
    ret = cl->call(lock_protocol::acquire, lid, id, r);
    
    if(ret == lock_protocol::OK) {
      pthread_mutex_lock(&c_lock);
      cache_map[lid].state = LOCKED;
      pthread_mutex_unlock(&c_lock);
    } else if (ret == lock_protocol::RETRY) {
      pthread_mutex_lock(&c_lock); 
      while(cache_map[lid].state != FREE)
        pthread_cond_wait(&c_cond,&c_lock);
      cache_map[lid].state = LOCKED;
      pthread_mutex_unlock(&c_lock);
    } else {
      //pthread_mutex_lock(&c_lock);
      //cache_map[lid].refcount--;
      //if(cache_map[lid].refcount == 0)
      //  cache_map[lid].state = NONCE;
      //pthread_mutex_unlock(&c_lock);
      goto connect;
    }
  } else {
    cache_map[lid].refcount++;
    while(cache_map[lid].state != FREE)
      pthread_cond_wait(&c_cond, &c_lock);

    cache_map[lid].state = LOCKED;
    pthread_mutex_unlock(&c_lock);
  }
  return ret;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
  pthread_mutex_lock(&c_lock);
  cache_map[lid].state = FREE;
  cache_map[lid].refcount--;
  pthread_mutex_unlock(&c_lock);
  pthread_cond_broadcast(&c_cond);

  return lock_protocol::OK;
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, int &)
{
  int ret = rlock_protocol::OK;
  pthread_mutex_lock(&c_lock);
  /*
  if(cache_map.count(lid) == 0 || cache_map[lid].state == NONCE || cache_map[lid].state == RELEASING) {
    pthread_mutex_unlock(&c_lock);
    return ret;
  }*/
  while(cache_map[lid].state != FREE || cache_map[lid].refcount != 0)
    pthread_cond_wait(&c_cond, &c_lock);    

  cache_map[lid].state = RELEASING;
  pthread_mutex_unlock(&c_lock);
  int r;
  lu->dorelease(lid);
  ret = cl->call(lock_protocol::release, lid, id, r);
  pthread_mutex_lock(&c_lock);
  if(cache_map[lid].state == RELEASING)
    cache_map[lid].state = NONCE;
  pthread_mutex_unlock(&c_lock);

  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, int &)
{
  int ret = rlock_protocol::OK;
  pthread_mutex_lock(&c_lock);
  if(cache_map[lid].state != ACQUIRING) {
    pthread_mutex_unlock(&c_lock);
  } else {
    cache_map[lid].state = FREE;
    pthread_mutex_unlock(&c_lock);
    pthread_cond_broadcast(&c_cond);
  }

  return ret;
}
