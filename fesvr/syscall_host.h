// See LICENSE for license details.

#ifndef __SYSCALL_HOST_H
#define __SYSCALL_HOST_H

#include "memif.h"
#include "byteorder.h"
#include <vector>
#include <string>

// Host interface for class `syscall_t`.
class syscall_host_t
{
 public:
  virtual ~syscall_host_t() = default;

  virtual void set_exit_code(int exit_code) = 0;

  virtual int exit_code() = 0;
  virtual memif_t& memif() = 0;
  virtual const std::vector<std::string>& target_args() = 0;

  template<typename T> inline T from_target(target_endian<T> n) const
  {
#ifdef RISCV_ENABLE_DUAL_ENDIAN
    memif_endianness_t endianness = memif().get_target_endianness();
    assert(endianness == memif_endianness_little || endianness == memif_endianness_big);

    return endianness == memif_endianness_big? n.from_be() : n.from_le();
#else
    return n.from_le();
#endif
  }

  template<typename T> inline target_endian<T> to_target(T n) const
  {
#ifdef RISCV_ENABLE_DUAL_ENDIAN
    memif_endianness_t endianness = memif().get_target_endianness();
    assert(endianness == memif_endianness_little || endianness == memif_endianness_big);

    return endianness == memif_endianness_big? target_endian<T>::to_be(n) : target_endian<T>::to_le(n);
#else
    return target_endian<T>::to_le(n);
#endif
  }
};

#endif
