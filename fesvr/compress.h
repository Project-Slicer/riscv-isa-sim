// See LICENSE for license details.

#ifndef __COMPRESS_H
#define __COMPRESS_H

#include <sys/types.h>

#include <cstddef>
#include <vector>

class compressor_t {
 public:
  enum class state_t {
    Idle,
    Ready,
    Compressing,
    Done,
    Error,
  };

  compressor_t(size_t write_buf_size, size_t compress_threshold_num,
               size_t compress_threshold_den)
      : state_(state_t::Idle),
        write_buf_size_(write_buf_size),
        compress_threshold_num_(compress_threshold_num),
        compress_threshold_den_(compress_threshold_den) {}

  state_t compress(const char *file_name);
  void reset() { state_ = state_t::Idle; }
  void ready() { state_ = state_t::Ready; }

  state_t state() const { return state_; }

 private:
  bool compress_file(int out_fd, int in_fd);

  static constexpr size_t kFileBufSize = 255, kQueueSize = 255;
  state_t state_;
  size_t write_buf_size_, compress_threshold_num_, compress_threshold_den_;
};

class compressors_t {
 public:
  compressors_t(size_t num_compressors, size_t write_buf_size,
                size_t compress_threshold_num, size_t compress_threshold_den)
      : compressors_(num_compressors,
                     compressor_t(write_buf_size, compress_threshold_num,
                                  compress_threshold_den)) {}

  ssize_t compress(const char *file_name);
  compressor_t::state_t take_if_done(size_t i);

 private:
  ssize_t select_compressor();

  std::vector<compressor_t> compressors_;
};

#endif
