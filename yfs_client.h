#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>
#include "lock_protocol.h"
#include "lock_client_cache.h"
#include <map>

class lock_user : public lock_release_user {
 private:
  class extent_client *ec;
 public:
  lock_user(extent_client *_ec) : ec(_ec) {}
  virtual ~lock_user() {}
  void dorelease(lock_protocol::lockid_t lid);
};

class yfs_client {
  extent_client *ec;
  lock_user *lu;
  lock_client_cache *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };
 private:

  static std::string filename(inum);
  static inum n2i(std::string);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int create(inum, inum, std::string);
  int createroot(); 
  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  int getfilecon(inum, std::string &);
  int getdircon(inum, std::map<std::string, yfs_client::inum> &);
  int put(inum, std::string);
  int remove(inum, std::string);

  void acquire(inum);
  void release(inum);
};

struct RemoteLock {
 private:

  yfs_client *yfs;
  yfs_client::inum lid;
 public:

  RemoteLock(yfs_client *yfs, yfs_client::inum inum): yfs(yfs), lid(inum)
  {
    yfs->acquire(lid);
  }
  ~RemoteLock()
  {
    yfs->release(lid);
  }
};

#endif 
