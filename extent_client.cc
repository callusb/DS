// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
  VERIFY(pthread_mutex_init(&m_lock, NULL) == 0);
}

extent_client::~extent_client()
{
  VERIFY(pthread_mutex_destroy(&m_lock) == 0);
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf) 
{
  ScopedLock ml(&m_lock);
  extent_protocol::status ret = extent_protocol::OK;
  if(caches.count(eid) == 0 || caches[eid].data_state == EMPTY) {
    ret = cl->call(extent_protocol::get, eid, buf);
    if(ret == extent_protocol::OK) {
      caches[eid].data = buf;
      caches[eid].data_state = OK;
    }
  } else if(caches[eid].data_state == DELETE) {
    ret = extent_protocol::NOENT;
  } else {
    buf = caches[eid].data;
    if(caches[eid].attr_state == OK)
      caches[eid].attrs.atime = time(NULL);
  }
  return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, extent_protocol::attr &attr) 
{
  ScopedLock ml(&m_lock);
  extent_protocol::status ret = extent_protocol::OK;
  if(caches.count(eid) == 0 || caches[eid].attr_state == EMPTY) {
    ret = cl->call(extent_protocol::getattr, eid, attr);
    if(ret == extent_protocol::OK) {
      caches[eid].attrs = attr;
      caches[eid].attr_state = OK;
    }
  } else if(caches[eid].attr_state == DELETE) {
    ret = extent_protocol::NOENT;
  } else {
    attr = caches[eid].attrs;
  }
  return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf) 
{
  ScopedLock ml(&m_lock);

  cache_data tmp;
  tmp.data = buf;
  tmp.data_state = DIRTY;

  extent_protocol::attr attr_tmp;
  attr_tmp.size = buf.size();
  if(caches.count(eid) == 1 && caches[eid].attr_state == OK) {
    attr_tmp.atime = caches[eid].attrs.atime;
    attr_tmp.ctime = attr_tmp.mtime = time(NULL);
  } else {
    attr_tmp.atime = attr_tmp.ctime = attr_tmp.mtime = time(NULL);
  }

  tmp.attrs = attr_tmp;
  tmp.attr_state = OK;
  caches[eid] = tmp;

  return extent_protocol::OK;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid) 
{
  ScopedLock ml(&m_lock);
  caches[eid].data_state = DELETE;
  caches[eid].attr_state = DELETE;
  return extent_protocol::OK;
}

void
extent_client::flush(extent_protocol::extentid_t eid) 
{
  ScopedLock ml(&m_lock);
  if(caches.count(eid) == 0)
    return;

  int r;
  if(caches[eid].data_state == DIRTY)
    cl->call(extent_protocol::put, eid, caches[eid].data, r);
  else if(caches[eid].data_state == DELETE)
    cl->call(extent_protocol::remove, eid, r);

  caches.erase(eid);
}
