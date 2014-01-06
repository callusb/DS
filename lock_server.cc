// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "slock.h"

#define LOCK_FREE -1

lock_server::lock_server():
  nacquire (0)
{
  VERIFY(pthread_mutex_init(&m_lock, NULL) == 0);
  VERIFY(pthread_cond_init(&m_cond, NULL) == 0);
}

lock_server::~lock_server()
{
  VERIFY(pthread_mutex_destroy(&m_lock) == 0);
  VERIFY(pthread_cond_destroy(&m_cond) == 0);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  ScopedLock ml(&m_lock);
  if(lock_map.find(lid) != lock_map.end()) {
    while(lock_map[lid] != LOCK_FREE)
      pthread_cond_wait(&m_cond, &m_lock);

    lock_map[lid] = clt;
  } else
    lock_map.insert(std::make_pair(lid, clt));

  printf("acquire request from clt %d\n", clt);

  return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;

  ScopedLock ml(&m_lock);
  if(lock_map.find(lid) != lock_map.end()) {
    if(lock_map[lid] == clt) {
      lock_map[lid] = LOCK_FREE;
      pthread_cond_broadcast(&m_cond);
      printf("release request from clt %d\n", clt);
    } else
      ret = lock_protocol::NOENT;
  } else
    ret = lock_protocol::NOENT;

  return ret;
}

