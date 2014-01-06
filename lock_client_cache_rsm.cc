// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache_rsm.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

#include "rsm_client.h"

static void *
releasethread(void *x)
{
  lock_client_cache_rsm *cc = (lock_client_cache_rsm *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache_rsm::last_port = 0;

lock_client_cache_rsm::lock_client_cache_rsm(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache_rsm::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache_rsm::retry_handler);
  xid = 0;
  // You fill this in Step Two, Lab 7
  // - Create rsmc, and use the object to do RPC 
  //   calls instead of the rpcc object of lock_client
  rsmc = new rsm_client(xdst); 
  VERIFY(pthread_mutex_init(&cm_lock, NULL) == 0);
  VERIFY(pthread_mutex_init(&revoke_lock, NULL) == 0);
  VERIFY(pthread_cond_init(&revoke_cond, NULL) == 0);
  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  VERIFY (r == 0);
}

lock_client_cache_rsm::~lock_client_cache_rsm()
{
  pthread_mutex_destroy(&cm_lock);
  pthread_mutex_destroy(&revoke_lock);
  pthread_cond_destroy(&revoke_cond);
}

void
lock_client_cache_rsm::releaser()
{
  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.
  while(true) {
    pthread_mutex_lock(&revoke_lock);
    pthread_cond_wait(&revoke_cond, &revoke_lock);
    while(!revoke_list.empty()) {
      lock_protocol::lockid_t lid = revoke_list.front();
      lock_state *cur = get_lock(lid);
      pthread_mutex_lock(&cur->ls_lock);
      if(cur->state == LOCKED) {
        cur->state = RELEASING;
        tprintf("releaser: waiting in releasing state,lid:%llu, id: %s\n", lid, id.c_str());
        pthread_cond_wait(&cur->revoke_cond, &cur->ls_lock);
      } else if (cur->state == ACQUIRING) {
        pthread_mutex_unlock(&cur->ls_lock);
        tprintf("releaser: has not yet got the lock, lid:%llu, id: %s\n", lid, id.c_str());
        continue;
      } else {
        tprintf("now the lock is in %d state, lid:%llu, id: %s\n", cur->state, lid, id.c_str());
      }

      revoke_list.pop_front();
      tprintf("releaser: calling server release, lid:%llu, id: %s\n", lid, id.c_str());
      if(lu != NULL)
        lu->dorelease(lid);
      int r;
      int ret;
      do {
        ret = rsmc->call(lock_protocol::release, lid, id, xid, r);
        tprintf("releaser is now in the releasing loop");
      } while(ret != lock_protocol::OK);
      tprintf("releaser: the lock is now released, lid:%llu, id: %s\n", lid, id.c_str());
      cur->state = NONE;
      pthread_cond_signal(&cur->ls_cond);
      pthread_mutex_unlock(&cur->ls_lock);
    }
    pthread_mutex_unlock(&revoke_lock);
  }
}

lock_protocol::status
lock_client_cache_rsm::acquire(lock_protocol::lockid_t lid)
{
  int ret = lock_protocol::OK;
  lock_state *cur = get_lock(lid);
  pthread_mutex_lock(&cur->ls_lock);
 connect:
  if(cur->state == NONE) {
    tprintf("in None case,lid: %llu, id: %s\n", lid, id.c_str());
    cur->state = ACQUIRING;
    int r;
    ret = rsmc->call(lock_protocol::acquire, lid, id, xid, r);
    tprintf("Responded back, lid: %llu, id: %s\n", lid, id.c_str());
    if (ret == lock_protocol::OK) { 
      tprintf("Client %llu get the lock, id: %s\n", lid, id.c_str());
      cur->state = LOCKED;
      xid++;
    } else if (ret == lock_protocol::RETRY) {
      while(ret != lock_protocol::OK) {
        tprintf("In retrying state: lid:%llu, id: %s\n", lid, id.c_str());
        struct timespec timeToWait;
        struct timeval now;
        gettimeofday(&now, NULL);
        timeToWait.tv_sec = now.tv_sec;
        timeToWait.tv_nsec = now.tv_usec*1000;
        timeToWait.tv_sec += 1;
        pthread_cond_wait(&cur->retry_cond, &cur->ls_lock);
        ret = rsmc->call(lock_protocol::acquire, lid, id, xid, r);
      }
      cur->state = LOCKED;
      tprintf("get lock in retrying, lid: %llu, id: %s\n", lid, id.c_str());
      xid++;
    } else {
      cur->state = NONE;
      goto connect;
    }
  } else if (cur->state == FREE) {
    tprintf("get lock in FREE case, lid: %llu, id: %s\n", lid, id.c_str());
    cur->state = LOCKED;
  } else {
    tprintf("get lock in Loc, acqur, rele lid: %llu, id: %s\n", lid, id.c_str());
    pthread_cond_wait(&cur->ls_cond, &cur->ls_lock);
    if(FREE == cur->state) {
      cur->state = LOCKED;
      tprintf("get lock in lock, acqu, rele lid: %llu, id: %s\n", lid, id.c_str());

    } else 
      goto connect;
  }
  pthread_mutex_unlock(&cur->ls_lock);

  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache_rsm::release(lock_protocol::lockid_t lid)
{
  lock_state *cur = get_lock(lid);
  pthread_mutex_lock(&cur->ls_lock);
  if (cur->state == RELEASING) {
    pthread_cond_signal(&cur->revoke_cond);
    tprintf("release in a revoke condition, lid: %llu, id: %s\n", lid, id.c_str());
  } else if (cur->state == LOCKED) {
    cur->state = FREE;
    pthread_cond_signal(&cur->ls_cond);
    tprintf("release in locked condition, lid: %llu, id: %s\n", lid, id.c_str());
  }
  else 
   tprintf("unknown state in release, lid: %llu, id: %s\n", lid, id.c_str());
  tprintf("thread release lock, lid: %llu, id: %s\n", lid, id.c_str());
  pthread_mutex_unlock(&cur->ls_lock);
  
  return lock_protocol::OK;
}

lock_client_cache_rsm::lock_state* 
lock_client_cache_rsm::get_lock(lock_protocol::lockid_t lid)
{
  lock_state *cur;
  pthread_mutex_lock(&cm_lock);
  if(cache_map.count(lid) > 0) {
    cur = &cache_map[lid];
  } else {
    cache_map[lid] = lock_state();
    cur = &cache_map[lid];
    VERIFY(pthread_mutex_init(&cur->ls_lock, NULL) == 0);
    VERIFY(pthread_cond_init(&cur->ls_cond, NULL) == 0);
    VERIFY(pthread_cond_init(&cur->revoke_cond, NULL) == 0);
    VERIFY(pthread_cond_init(&cur->retry_cond, NULL) == 0);
  }
  pthread_mutex_unlock(&cm_lock);
  return cur;
}

rlock_protocol::status
lock_client_cache_rsm::revoke_handler(lock_protocol::lockid_t lid, 
			          lock_protocol::xid_t xid, int &)
{
  int ret = rlock_protocol::OK;
  tprintf("get revoke, client is now releasing!, lid: %llu, id: %s\n", lid, id.c_str());
  pthread_mutex_lock(&revoke_lock);
  revoke_list.push_back(lid);
  pthread_cond_signal(&revoke_cond);
  pthread_mutex_unlock(&revoke_lock);
  return ret;
}

rlock_protocol::status
lock_client_cache_rsm::retry_handler(lock_protocol::lockid_t lid, 
			         lock_protocol::xid_t xid, int &)
{
  int ret = rlock_protocol::OK;
  tprintf("got retry from server for lid:%llu, id: %s\n", lid, id.c_str());
  lock_state *cur = get_lock(lid);
  pthread_mutex_lock(&cur->ls_lock);
  pthread_cond_signal(&cur->retry_cond);
  pthread_mutex_unlock(&cur->ls_lock);
  return ret;
}
