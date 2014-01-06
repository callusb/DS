// the caching lock server implementation

#include "lock_server_cache_rsm.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
revokethread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->retryer();
  return 0;
}

lock_server_cache_rsm::lock_server_cache_rsm(class rsm *_rsm) 
  : rsm (_rsm)
{
  pthread_mutex_init(&lm_lock, NULL);
  VERIFY(pthread_mutex_init(&retry_lock, NULL) == 0);
  VERIFY(pthread_cond_init(&retry_cond, NULL) == 0);
  VERIFY(pthread_mutex_init(&revoke_lock, NULL) == 0);
  VERIFY(pthread_cond_init(&revoke_cond, NULL) == 0);
  rsm->set_state_transfer(this);

  pthread_t revoke_th, retry_th;
  int r = pthread_create(&revoke_th, NULL, &revokethread, (void *) this);
  VERIFY (r == 0);
  r = pthread_create(&retry_th, NULL, &retrythread, (void *) this);
  VERIFY (r == 0);
}

lock_server_cache_rsm::~lock_server_cache_rsm()
{
  pthread_mutex_destroy(&lm_lock);
  pthread_mutex_destroy(&retry_lock);
  pthread_cond_destroy(&retry_cond);
  pthread_mutex_destroy(&revoke_lock);
  pthread_cond_destroy(&revoke_cond);
}

lock_server_cache_rsm::lock_state* lock_server_cache_rsm::get_lock
(lock_protocol::lockid_t lid) 
{
  if(lock_map.count(lid) == 0) { 
    lock_map[lid] = lock_state();
  }
  return &lock_map[lid];
}

void
lock_server_cache_rsm::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
  int r;
  lock_protocol::xid_t xid = 0;
  rlock_protocol::status ret;
  while(true) {
    pthread_mutex_lock(&revoke_lock);
    pthread_cond_wait(&revoke_cond, &revoke_lock);
    while(!revoke_list.empty()) {
      client_info info = revoke_list.front();
      revoke_list.pop_front();
      if(rsm->amiprimary()) {
        handle h(info.cid);
        if(h.safebind()) {
          ret = h.safebind()->call(rlock_protocol::revoke, info.lid, xid, r);
          tprintf("call the revoke one, id: %s is chosen for lid: %llu\n", info.cid.c_str(), info.lid);
        }
        if(!h.safebind() || ret != rlock_protocol::OK) 
          tprintf("revoke RPC failed\n");
      }
    }
    pthread_mutex_unlock(&revoke_lock);
  }
}


void
lock_server_cache_rsm::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
  int r;
  lock_protocol::xid_t xid = 0;
  rlock_protocol::status ret;
  while(true) {
    pthread_mutex_lock(&retry_lock);
    pthread_cond_wait(&retry_cond, &retry_lock);
    while(!retry_list.empty()) {
      client_info info = retry_list.front();
      retry_list.pop_front();
      if(rsm->amiprimary()) {
        handle h(info.cid);
        if(h.safebind()) {
          ret = h.safebind()->call(rlock_protocol::retry,  info.lid, xid, r);
          tprintf("call the retry one, id: %s is chosen for lid: %llu\n", info.cid.c_str(), info.lid);
        }
        if(!h.safebind() || ret != rlock_protocol::OK) 
          tprintf("retry RPC failed\n");
      }
    }
    pthread_mutex_unlock(&retry_lock);
  }

}


int lock_server_cache_rsm::acquire(lock_protocol::lockid_t lid, std::string id, 
             lock_protocol::xid_t xid, int &)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&lm_lock);
  lock_state *cur = get_lock(lid);
  if(cur->state == FREE) {
    tprintf("id %s is coming for lid:%llu, lock is now FREE\n", id.c_str(), lid);
    cur->state = LOCKED;
    cur->owner = id;
  } else if(cur->state == RETRYING && cur->retryid == id) {
    tprintf("id %s is coming for lid:%llu, it is the retrying\n", id.c_str(), lid);
    cur->waitids.pop_front();
    cur->state = LOCKED;
    cur->owner = id;
  } else {
    tprintf("id %s is coming for lid:%llu, it is locked\n", id.c_str(), lid);
    cur->waitids.push_back(id);
    ret = lock_protocol::RETRY;
  }

  if(cur->state == LOCKED && cur->waitids.size() > 0) {
    tprintf("id: %s is pushing into lid: %llu revoke list\n", cur->owner.c_str(), lid);
    cur->state = REVOKING;
    pthread_mutex_lock(&revoke_lock);
    client_info info;
    info.cid = cur->owner;
    info.lid = lid;
    revoke_list.push_back(info);
    pthread_cond_signal(&revoke_cond);
    pthread_mutex_unlock(&revoke_lock);
  }

  pthread_mutex_unlock(&lm_lock);
  return ret;
}

int 
lock_server_cache_rsm::release(lock_protocol::lockid_t lid, std::string id, 
         lock_protocol::xid_t xid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&lm_lock);

  tprintf("id: %s is now released for lid: %llu!\n", id.c_str(), lid);
  lock_state *cur = get_lock(lid);
  if(cur->owner != id) {
    pthread_mutex_unlock(&lm_lock);
    return ret;
  }
  cur->state = FREE;
  if(cur->waitids.size() > 0) {
    cur->state = RETRYING;
    client_info info;
    info.cid = cur->waitids.front();
    info.lid = lid;
    cur->retryid = info.cid;
    pthread_mutex_lock(&retry_lock);
    retry_list.push_back(info);
    pthread_cond_signal(&retry_cond);
    pthread_mutex_unlock(&retry_lock);
    tprintf("a new client id: %s is selected for the retry %llu!\n", info.cid.c_str(), lid);
  } else 
    tprintf("no one in the wait list\n");
  pthread_mutex_unlock(&lm_lock);

  return ret;
}

std::string
lock_server_cache_rsm::marshal_state()
{
  pthread_mutex_lock(&lm_lock);
  marshall rep;

  rep << lock_map.size();
  std::map<lock_protocol::lockid_t, lock_state>::iterator iter_lock;
  for(iter_lock = lock_map.begin(); iter_lock != lock_map.end(); iter_lock++) {
    lock_protocol::lockid_t lid = iter_lock->first;
    lock_state *cur = &lock_map[lid];
    rep << lid;
    rep << cur->state;
    rep << cur->owner;
    rep << cur->retryid;
    rep << cur->waitids.size();
    std::list<std::string>::iterator iter_wait;
    for(iter_wait = cur->waitids.begin(); iter_wait != cur->waitids.end(); iter_wait++)
      rep << *iter_wait;
  }
  pthread_mutex_unlock(&lm_lock);

  return rep.str();
}

void
lock_server_cache_rsm::unmarshal_state(std::string state)
{
  pthread_mutex_lock(&lm_lock);
  unmarshall rep(state);
  unsigned int locks_size;
  rep >> locks_size;
  for(unsigned int i = 0; i < locks_size; i++) {
    lock_protocol::lockid_t lid;
    rep >> lid;
    lock_map[lid] = lock_state();
    lock_state *cur = &lock_map[lid]; 
    rep >> cur->state;
    rep >> cur->owner;
    rep >> cur->retryid;
    unsigned int wait_size;
    rep >> wait_size;
    std::list<std::string> waitids;
    for(unsigned int j = 0; j < wait_size; j++) {
      std::string tmp;
      rep >> tmp;
      waitids.push_back(tmp);
    }
    cur->waitids = waitids;
  }
  pthread_mutex_unlock(&lm_lock);
}

lock_protocol::status
lock_server_cache_rsm::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

