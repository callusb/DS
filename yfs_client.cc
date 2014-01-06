// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lu = new lock_user(ec);
  lc = new lock_client_cache_rsm(lock_dst, lu);
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::create(inum parent, inum inum, std::string name) {
  int r = OK;

  std::string buf;
  if(ec->get(parent, buf) == extent_protocol::OK) {
    buf += name +  '=' + filename(inum) + ';';

    if(ec->put(parent, buf) == extent_protocol::OK && ec->put(inum, "") == extent_protocol::OK) 
      r = OK;
    else
      r = IOERR;
  } else
    r = IOERR;

  return r;
}

int
yfs_client::createroot() {
  int r = OK;

  RemoteLock(this, 0x00000001);
  if (ec->put(0x00000001, "") != extent_protocol::OK)
    r = IOERR;
  else
    r = OK;

  return r;
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

int
yfs_client::getfilecon(inum inum, std::string &buf) 
{
  int r = OK;
  if(ec->get(inum, buf) == extent_protocol::OK)
    r = OK;
  else
    r = IOERR;
  
  return r;
}

int
yfs_client::getdircon(inum inum, std::map<std::string, yfs_client::inum> & ents)
{
  int r = OK;

  std::string buf;
  if(ec->get(inum, buf) != extent_protocol::OK) {
    r = IOERR;
  } else {
    size_t start_pos = 0;
    size_t end_pos = 0;

    while((end_pos = buf.find(';', start_pos)) != std::string::npos) {
      if(start_pos != end_pos) {
        std::string token = buf.substr(start_pos, end_pos - start_pos);
        size_t mid_pos = token.find('=');
        ents[token.substr(0, mid_pos)] = yfs_client::n2i(token.substr(mid_pos + 1, token.size()));
      }

      start_pos = end_pos + 1;
    }
    r = OK;
  }

  return r;
}

int
yfs_client::put(inum inum, std::string buf) {
  int r = OK;

  if(ec->put(inum, buf) != extent_protocol::OK)
    r = IOERR;
  else
    r = OK;

  return r;
}

int
yfs_client::remove(inum parent, std::string name) {

  std::map<std::string, yfs_client::inum> ents;
  if(getdircon(parent, ents) != extent_protocol::OK) {
    return IOERR;
  } else {
    if(ents.count(name) != 1)
      return NOENT;
    else {
      inum inum = ents[name];
      RemoteLock(this, inum);
      if(ec->remove(inum) != extent_protocol::OK)
        return IOERR;

      ents.erase(name);
      std::string buf;
      std::map<std::string, yfs_client::inum>::iterator it;
      for(it = ents.begin(); it != ents.end(); it++) {
        buf += it->first +  '=' + filename(it->second) + ';';
      }

      if(ec->put(parent, buf) != extent_protocol::OK)
        return IOERR;
      else
        return OK;
    }
  }
}

void
yfs_client::acquire(inum inum)
{
  lc->acquire(inum);
}

void
yfs_client::release(inum inum)
{
  lc->release(inum);
}

void
lock_user::dorelease(lock_protocol::lockid_t lid)
{
  extent_protocol::extentid_t eid = lid;
  ec->flush(eid);
}

