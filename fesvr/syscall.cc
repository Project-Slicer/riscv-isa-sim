// See LICENSE for license details.

#include "syscall.h"
#include "byteorder.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <termios.h>
#include <sys/sendfile.h>
#include <sstream>
#include <iostream>
using namespace std::placeholders;

#define RISCV_AT_FDCWD -100

struct riscv_stat
{
  target_endian<uint64_t> dev;
  target_endian<uint64_t> ino;
  target_endian<uint32_t> mode;
  target_endian<uint32_t> nlink;
  target_endian<uint32_t> uid;
  target_endian<uint32_t> gid;
  target_endian<uint64_t> rdev;
  target_endian<uint64_t> __pad1;
  target_endian<uint64_t> size;
  target_endian<uint32_t> blksize;
  target_endian<uint32_t> __pad2;
  target_endian<uint64_t> blocks;
  target_endian<uint64_t> atime;
  target_endian<uint64_t> __pad3;
  target_endian<uint64_t> mtime;
  target_endian<uint64_t> __pad4;
  target_endian<uint64_t> ctime;
  target_endian<uint64_t> __pad5;
  target_endian<uint32_t> __unused4;
  target_endian<uint32_t> __unused5;

  riscv_stat(const struct stat& s, syscall_host_t* host)
    : dev(host->to_target<uint64_t>(s.st_dev)),
      ino(host->to_target<uint64_t>(s.st_ino)),
      mode(host->to_target<uint32_t>(s.st_mode)),
      nlink(host->to_target<uint32_t>(s.st_nlink)),
      uid(host->to_target<uint32_t>(s.st_uid)),
      gid(host->to_target<uint32_t>(s.st_gid)),
      rdev(host->to_target<uint64_t>(s.st_rdev)), __pad1(),
      size(host->to_target<uint64_t>(s.st_size)),
      blksize(host->to_target<uint32_t>(s.st_blksize)), __pad2(),
      blocks(host->to_target<uint64_t>(s.st_blocks)),
      atime(host->to_target<uint64_t>(s.st_atime)), __pad3(),
      mtime(host->to_target<uint64_t>(s.st_mtime)), __pad4(),
      ctime(host->to_target<uint64_t>(s.st_ctime)), __pad5(),
      __unused4(), __unused5() {}
};


struct riscv_statx_timestamp {
    target_endian<int64_t>  tv_sec;
    target_endian<uint32_t> tv_nsec;
    target_endian<int32_t>  __reserved;
};

#ifdef HAVE_STATX
struct riscv_statx
{
    target_endian<uint32_t> mask;
    target_endian<uint32_t> blksize;
    target_endian<uint64_t> attributes;
    target_endian<uint32_t> nlink;
    target_endian<uint32_t> uid;
    target_endian<uint32_t> gid;
    target_endian<uint16_t> mode;
    target_endian<uint16_t> __spare0[1];
    target_endian<uint64_t> ino;
    target_endian<uint64_t> size;
    target_endian<uint64_t> blocks;
    target_endian<uint64_t> attributes_mask;
    struct riscv_statx_timestamp atime;
    struct riscv_statx_timestamp btime;
    struct riscv_statx_timestamp ctime;
    struct riscv_statx_timestamp mtime;
    target_endian<uint32_t> rdev_major;
    target_endian<uint32_t> rdev_minor;
    target_endian<uint32_t> dev_major;
    target_endian<uint32_t> dev_minor;
#ifdef HAVE_STATX_MNT_ID
    target_endian<uint64_t> mnt_id;
    target_endian<uint64_t> __spare2;
    target_endian<uint64_t> __spare3[12];
#else
    target_endian<uint64_t> __spare2[14];
#endif

  riscv_statx(const struct statx& s, syscall_host_t* host)
    : mask(host->to_target<uint32_t>(s.stx_mask)),
      blksize(host->to_target<uint32_t>(s.stx_blksize)),
      attributes(host->to_target<uint64_t>(s.stx_attributes)),
      nlink(host->to_target<uint32_t>(s.stx_nlink)),
      uid(host->to_target<uint32_t>(s.stx_uid)),
      gid(host->to_target<uint32_t>(s.stx_gid)),
      mode(host->to_target<uint16_t>(s.stx_mode)), __spare0(),
      ino(host->to_target<uint64_t>(s.stx_ino)),
      size(host->to_target<uint64_t>(s.stx_size)),
      blocks(host->to_target<uint64_t>(s.stx_blocks)),
      attributes_mask(host->to_target<uint64_t>(s.stx_attributes_mask)),
      atime {
        host->to_target<int64_t>(s.stx_atime.tv_sec),
        host->to_target<uint32_t>(s.stx_atime.tv_nsec)
      },
      btime {
        host->to_target<int64_t>(s.stx_btime.tv_sec),
        host->to_target<uint32_t>(s.stx_btime.tv_nsec)
      },
      ctime {
        host->to_target<int64_t>(s.stx_ctime.tv_sec),
        host->to_target<uint32_t>(s.stx_ctime.tv_nsec)
      },
      mtime {
        host->to_target<int64_t>(s.stx_mtime.tv_sec),
        host->to_target<uint32_t>(s.stx_mtime.tv_nsec)
      },
      rdev_major(host->to_target<uint32_t>(s.stx_rdev_major)),
      rdev_minor(host->to_target<uint32_t>(s.stx_rdev_minor)),
      dev_major(host->to_target<uint32_t>(s.stx_dev_major)),
      dev_minor(host->to_target<uint32_t>(s.stx_dev_minor)),
#ifdef HAVE_STATX_MNT_ID
      mnt_id(host->to_target<uint64_t>(s.stx_mnt_id)),
      __spare2(), __spare3()
#else
      __spare2()
#endif      
      {}
};
#endif

syscall_t::syscall_t(syscall_host_t* host)
  : host(host), memif(&host->memif()), table(2048), compressors(64, 1024, 0.9)
{
  table[17] = &syscall_t::sys_getcwd;
  table[25] = &syscall_t::sys_fcntl;
  table[34] = &syscall_t::sys_mkdirat;
  table[35] = &syscall_t::sys_unlinkat;
  table[37] = &syscall_t::sys_linkat;
  table[38] = &syscall_t::sys_renameat;
  table[46] = &syscall_t::sys_ftruncate;
  table[48] = &syscall_t::sys_faccessat;
  table[49] = &syscall_t::sys_chdir;
  table[56] = &syscall_t::sys_openat;
  table[57] = &syscall_t::sys_close;
  table[62] = &syscall_t::sys_lseek;
  table[63] = &syscall_t::sys_read;
  table[64] = &syscall_t::sys_write;
  table[67] = &syscall_t::sys_pread;
  table[68] = &syscall_t::sys_pwrite;
  table[71] = &syscall_t::sys_sendfile;
  table[79] = &syscall_t::sys_fstatat;
  table[80] = &syscall_t::sys_fstat;
  table[93] = &syscall_t::sys_exit;
  table[291] = &syscall_t::sys_statx;
  table[1039] = &syscall_t::sys_lstat;
  table[2011] = &syscall_t::sys_getmainvars;
  table[2012] = &syscall_t::sys_getfdpath;
  table[2013] = &syscall_t::sys_compressfile;
  table[2014] = &syscall_t::sys_compressquery;

  register_command(0, std::bind(&syscall_t::handle_syscall, this, _1), "syscall");

  int stdin_fd = dup(0), stdout_fd0 = dup(1), stdout_fd1 = dup(1);
  if (stdin_fd < 0 || stdout_fd0 < 0 || stdout_fd1 < 0)
    throw std::runtime_error("could not dup stdin/stdout");

  fds.alloc(stdin_fd); // stdin -> stdin
  fds.alloc(stdout_fd0); // stdout -> stdout
  fds.alloc(stdout_fd1); // stderr -> stdout
}

std::string syscall_t::do_chroot(const char* fn)
{
  if (!chroot.empty() && *fn == '/')
    return chroot + fn;
  return fn;
}

std::string syscall_t::undo_chroot(const char* fn)
{
  if (chroot.empty())
    return fn;
  if (strncmp(fn, chroot.c_str(), chroot.size()) == 0
      && (chroot.back() == '/' || fn[chroot.size()] == '/'))
    return fn + chroot.size() - (chroot.back() == '/');
  return "/";
}

void syscall_t::handle_syscall(command_t cmd)
{
  if (cmd.payload() & 1) // test pass/fail
  {
    host->set_exit_code(cmd.payload());
    if (host->exit_code())
      std::cerr << "*** FAILED *** (tohost = " << host->exit_code() << ")" << std::endl;
    return;
  }
  else // proxied system call
    dispatch(cmd.payload());

  cmd.respond(1);
}

reg_t syscall_t::sys_exit(reg_t code, reg_t a1, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  host->set_exit_code(code << 1 | 1);
  return 0;
}

static reg_t sysret_errno(sreg_t ret)
{
  return ret == -1 ? -errno : ret;
}

reg_t syscall_t::sys_read(reg_t fd, reg_t pbuf, reg_t len, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> buf(len);
  ssize_t ret = read(fds.lookup(fd), buf.data(), len);
  reg_t ret_errno = sysret_errno(ret);
  if (ret > 0)
    memif->write(pbuf, ret, buf.data());
  return ret_errno;
}

reg_t syscall_t::sys_pread(reg_t fd, reg_t pbuf, reg_t len, reg_t off, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> buf(len);
  ssize_t ret = pread(fds.lookup(fd), buf.data(), len, off);
  reg_t ret_errno = sysret_errno(ret);
  if (ret > 0)
    memif->write(pbuf, ret, buf.data());
  return ret_errno;
}

reg_t syscall_t::sys_write(reg_t fd, reg_t pbuf, reg_t len, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> buf(len);
  memif->read(pbuf, len, buf.data());
  reg_t ret = sysret_errno(write(fds.lookup(fd), buf.data(), len));
  return ret;
}

reg_t syscall_t::sys_pwrite(reg_t fd, reg_t pbuf, reg_t len, reg_t off, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> buf(len);
  memif->read(pbuf, len, buf.data());
  reg_t ret = sysret_errno(pwrite(fds.lookup(fd), buf.data(), len, off));
  return ret;
}

reg_t syscall_t::sys_close(reg_t fd, reg_t a1, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  if (close(fds.lookup(fd)) < 0)
    return sysret_errno(-1);
  fds.dealloc(fd);
  return 0;
}

reg_t syscall_t::sys_lseek(reg_t fd, reg_t ptr, reg_t dir, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  return sysret_errno(lseek(fds.lookup(fd), ptr, dir));
}

reg_t syscall_t::sys_fstat(reg_t fd, reg_t pbuf, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  struct stat buf;
  reg_t ret = sysret_errno(fstat(fds.lookup(fd), &buf));
  if (ret != (reg_t)-1)
  {
    riscv_stat rbuf(buf, host);
    memif->write(pbuf, sizeof(rbuf), &rbuf);
  }
  return ret;
}

reg_t syscall_t::sys_fcntl(reg_t fd, reg_t cmd, reg_t arg, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  return sysret_errno(fcntl(fds.lookup(fd), cmd, arg));
}

reg_t syscall_t::sys_ftruncate(reg_t fd, reg_t len, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  return sysret_errno(ftruncate(fds.lookup(fd), len));
}

reg_t syscall_t::sys_lstat(reg_t pname, reg_t len, reg_t pbuf, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());

  struct stat buf;
  reg_t ret = sysret_errno(lstat(do_chroot(name.data()).c_str(), &buf));
  if (ret != (reg_t)-1)
  {
    riscv_stat rbuf(buf, host);
    memif->write(pbuf, sizeof(rbuf), &rbuf);
  }
  return ret;
}

reg_t syscall_t::sys_statx(reg_t fd, reg_t pname, reg_t len, reg_t flags, reg_t mask, reg_t pbuf, reg_t a6)
{
#ifndef HAVE_STATX
  return -ENOSYS;
#else
  std::vector<char> name(len);
  memif->read(pname, len, name.data());

  struct statx buf;
  reg_t ret = sysret_errno(statx(fds.lookup(fd), do_chroot(name.data()).c_str(), flags, mask, &buf));
  if (ret != (reg_t)-1)
  {
    riscv_statx rbuf(buf, host);
    memif->write(pbuf, sizeof(rbuf), &rbuf);
  }
  return ret;
#endif
}

#define AT_SYSCALL(syscall, fd, name, ...) \
  (syscall(fds.lookup(fd), int(fd) == RISCV_AT_FDCWD ? do_chroot(name).c_str() : (name), __VA_ARGS__))

reg_t syscall_t::sys_openat(reg_t dirfd, reg_t pname, reg_t len, reg_t flags, reg_t mode, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());
  int fd = sysret_errno(AT_SYSCALL(openat, dirfd, name.data(), flags, mode));
  if (fd < 0)
    return sysret_errno(-1);
  return fds.alloc(fd);
}

reg_t syscall_t::sys_fstatat(reg_t dirfd, reg_t pname, reg_t len, reg_t pbuf, reg_t flags, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());

  struct stat buf;
  reg_t ret = sysret_errno(AT_SYSCALL(fstatat, dirfd, name.data(), &buf, flags));
  if (ret != (reg_t)-1)
  {
    riscv_stat rbuf(buf, host);
    memif->write(pbuf, sizeof(rbuf), &rbuf);
  }
  return ret;
}

reg_t syscall_t::sys_faccessat(reg_t dirfd, reg_t pname, reg_t len, reg_t mode, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());
  return sysret_errno(AT_SYSCALL(faccessat, dirfd, name.data(), mode, 0));
}

reg_t syscall_t::sys_renameat(reg_t odirfd, reg_t popath, reg_t olen, reg_t ndirfd, reg_t pnpath, reg_t nlen, reg_t a6)
{
  std::vector<char> opath(olen), npath(nlen);
  memif->read(popath, olen, opath.data());
  memif->read(pnpath, nlen, npath.data());
  return sysret_errno(renameat(fds.lookup(odirfd), int(odirfd) == RISCV_AT_FDCWD ? do_chroot(opath.data()).c_str() : opath.data(),
                             fds.lookup(ndirfd), int(ndirfd) == RISCV_AT_FDCWD ? do_chroot(npath.data()).c_str() : npath.data()));
}

reg_t syscall_t::sys_linkat(reg_t odirfd, reg_t poname, reg_t olen, reg_t ndirfd, reg_t pnname, reg_t nlen, reg_t flags)
{
  std::vector<char> oname(olen), nname(nlen);
  memif->read(poname, olen, oname.data());
  memif->read(pnname, nlen, nname.data());
  return sysret_errno(linkat(fds.lookup(odirfd), int(odirfd) == RISCV_AT_FDCWD ? do_chroot(oname.data()).c_str() : oname.data(),
                             fds.lookup(ndirfd), int(ndirfd) == RISCV_AT_FDCWD ? do_chroot(nname.data()).c_str() : nname.data(),
                             flags));
}

reg_t syscall_t::sys_unlinkat(reg_t dirfd, reg_t pname, reg_t len, reg_t flags, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());
  return sysret_errno(AT_SYSCALL(unlinkat, dirfd, name.data(), flags));
}

reg_t syscall_t::sys_mkdirat(reg_t dirfd, reg_t pname, reg_t len, reg_t mode, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());
  return sysret_errno(AT_SYSCALL(mkdirat, dirfd, name.data(), mode));
}

reg_t syscall_t::sys_getcwd(reg_t pbuf, reg_t size, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> buf(size);
  char* ret = getcwd(buf.data(), size);
  if (ret == NULL)
    return sysret_errno(-1);
  std::string tmp = undo_chroot(buf.data());
  if (size <= tmp.size())
    return -ENOMEM;
  memif->write(pbuf, tmp.size() + 1, tmp.data());
  return tmp.size() + 1;
}

reg_t syscall_t::sys_getmainvars(reg_t pbuf, reg_t limit, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<std::string> args = host->target_args();
  std::vector<target_endian<uint64_t>> words(args.size() + 3);
  words[0] = host->to_target<uint64_t>(args.size());
  words[args.size()+1] = target_endian<uint64_t>::zero; // argv[argc] = NULL
  words[args.size()+2] = target_endian<uint64_t>::zero; // envp[0] = NULL

  size_t sz = (args.size() + 3) * sizeof(words[0]);
  for (size_t i = 0; i < args.size(); i++)
  {
    words[i+1] = host->to_target<uint64_t>(sz + pbuf);
    sz += args[i].length() + 1;
  }

  std::vector<char> bytes(sz);
  memcpy(bytes.data(), words.data(), sizeof(words[0]) * words.size());
  for (size_t i = 0; i < args.size(); i++)
    strcpy(&bytes[host->from_target(words[i+1]) - pbuf], args[i].c_str());

  if (bytes.size() > limit)
    return -ENOMEM;

  memif->write(pbuf, bytes.size(), bytes.data());
  return 0;
}

reg_t syscall_t::sys_chdir(reg_t path, reg_t a1, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  size_t size = 0;
  while (memif->read_uint8(path + size++))
    ;
  std::vector<char> buf(size);
  for (size_t offset = 0;; offset++)
  {
    buf[offset] = memif->read_uint8(path + offset);
    if (!buf[offset])
      break;
  }
  return sysret_errno(chdir(buf.data()));
}

reg_t syscall_t::sys_getfdpath(reg_t fd, reg_t pbuf, reg_t size, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  auto fd_file = "/proc/self/fd/" + std::to_string(fds.lookup(fd));
  std::vector<char> buf(size);
  ssize_t ret = readlink(fd_file.c_str(), buf.data(), size);
  reg_t ret_errno = sysret_errno(ret);
  if (ret > 0)
    memif->write(pbuf, ret, buf.data());
  return ret_errno;
}

reg_t syscall_t::sys_sendfile(reg_t out_fd, reg_t in_fd, reg_t poffset, reg_t count, reg_t a4, reg_t a5, reg_t a6)
{
  off_t offset, *offsetp = NULL;
  if (poffset)
  {
    memif->read(poffset, sizeof(offset), &offset);
    offsetp = &offset;
  }
  ssize_t ret = sendfile(fds.lookup(out_fd), fds.lookup(in_fd), offsetp, count);
  reg_t ret_errno = sysret_errno(ret);
  if (ret >= 0 && poffset)
    memif->write(poffset, sizeof(offset), &offset);
  return ret_errno;
}

reg_t syscall_t::sys_compressfile(reg_t dirfd, reg_t pname, reg_t len, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  std::vector<char> name(len);
  memif->read(pname, len, name.data());
  return compressors.compress(fds.lookup(dirfd),
                              int(dirfd) == RISCV_AT_FDCWD ? do_chroot(name.data()).c_str() : name.data());
}

reg_t syscall_t::sys_compressquery(reg_t id, reg_t a1, reg_t a2, reg_t a3, reg_t a4, reg_t a5, reg_t a6)
{
  if (id >= compressors.num_compressors())
    return -1;
  auto state = compressors.take_if_done(id);
  if (state == compressor_t::state_t::Done)
    return 0;
  if (state == compressor_t::state_t::Error)
    return 1;
  return 2;
}

void syscall_t::dispatch(reg_t mm)
{
  target_endian<reg_t> magicmem[8];
  memif->read(mm, sizeof(magicmem), magicmem);

  reg_t n = host->from_target(magicmem[0]);
  if (n >= table.size() || !table[n])
    throw std::runtime_error("bad syscall #" + std::to_string(n));

  magicmem[0] = host->to_target((this->*table[n])(host->from_target(magicmem[1]), host->from_target(magicmem[2]), host->from_target(magicmem[3]), host->from_target(magicmem[4]), host->from_target(magicmem[5]), host->from_target(magicmem[6]), host->from_target(magicmem[7])));

  memif->write(mm, sizeof(magicmem), magicmem);
}

reg_t fds_t::alloc(int fd)
{
  reg_t i;
  for (i = 0; i < fds.size(); i++)
    if (fds[i] == -1)
      break;

  if (i == fds.size())
    fds.resize(i+1);

  fds[i] = fd;
  return i;
}

void fds_t::dealloc(reg_t fd)
{
  fds[fd] = -1;
}

int fds_t::lookup(reg_t fd)
{
  if (int(fd) == RISCV_AT_FDCWD)
    return AT_FDCWD;
  return fd >= fds.size() ? -1 : fds[fd];
}

void syscall_t::set_chroot(const char* where)
{
  char buf1[PATH_MAX], buf2[PATH_MAX];

  if (getcwd(buf1, sizeof(buf1)) == NULL
      || chdir(where) != 0
      || getcwd(buf2, sizeof(buf2)) == NULL
      || chdir(buf1) != 0)
  {
    fprintf(stderr, "could not chroot to %s\n", where);
    exit(-1);
  }

  chroot = buf2;
}
