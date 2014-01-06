// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

extent_server::extent_server() 
{
  VERIFY(pthread_mutex_init(&m_lock, NULL) == 0);
}

extent_server::~extent_server() 
{
  VERIFY(pthread_mutex_destroy(&m_lock) == 0);
}

int 
extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  ScopedLock ml(&m_lock);
  if(blocks.count(id) == 0) {
    Block tmp_block;
    tmp_block.data = buf;
    tmp_block.attrs.atime = tmp_block.attrs.ctime = tmp_block.attrs.mtime = time(NULL);
    tmp_block.attrs.size = buf.size();
    blocks[id] = tmp_block;
  } else {
    blocks[id].data = buf;
    blocks[id].attrs.size = buf.size();
    blocks[id].attrs.ctime = blocks[id].attrs.mtime = time(NULL);
  }

  return extent_protocol::OK;
}

int 
extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  ScopedLock ml(&m_lock);
  if(blocks.count(id) == 0)
    return extent_protocol::NOENT;
  else {
    buf = blocks[id].data;
    blocks[id].attrs.atime = time(NULL);
    return extent_protocol::OK;
  }
}

int 
extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  ScopedLock ml(&m_lock);
  if(blocks.count(id) == 0) { 
    a.size = 0;
    a.atime = 0;
    a.mtime = 0;
    a.ctime = 0;
  } else
    a = blocks[id].attrs;

  return extent_protocol::OK;
}

int 
extent_server::remove(extent_protocol::extentid_t id, int &)
{
  ScopedLock ml(&m_lock);
  if(blocks.count(id) == 0)
    return extent_protocol::NOENT;
  else {
    blocks.erase(id);
    return extent_protocol::OK;
  }
}

