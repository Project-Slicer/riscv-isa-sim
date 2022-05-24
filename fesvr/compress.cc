// See LICENSE for license details.

#include "compress.h"

#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

class queue_t {
 public:
  queue_t(size_t max_len)
      : buf_(std::make_unique<uint8_t[]>(max_len)),
        max_len_(max_len),
        head_(0),
        len_(0) {}

  void push(uint8_t elem) {
    buf_[(head_ + len_) % max_len_] = elem;
    if (len_ < max_len_) {
      ++len_;
    } else {
      head_ = (head_ + 1) % max_len_;
    }
  }

  uint8_t operator[](size_t i) const {
    assert(i < len_);
    return buf_[(head_ + i) % max_len_];
  }

  size_t len() const { return len_; }

 private:
  std::unique_ptr<uint8_t[]> buf_;
  size_t max_len_, head_, len_;
};

class write_buffer_t {
 public:
  write_buffer_t(int fd, size_t size)
      : fd_(fd),
        size_(size),
        pos_(0),
        buf_(std::make_unique<uint8_t[]>(size)) {}
  ~write_buffer_t() {
    bool ret = flush();
    assert(ret);
  }

  bool write_byte(uint8_t byte) {
    if (pos_ == size_) {
      if (!flush()) return false;
    }
    buf_[pos_++] = byte;
    return true;
  }

  bool flush() {
    if (pos_ > 0) {
      ssize_t ret = write(fd_, buf_.get(), pos_);
      if (ret < 0 || static_cast<size_t>(ret) != pos_) return false;
      pos_ = 0;
    }
    return true;
  }

 private:
  int fd_;
  size_t size_, pos_;
  std::unique_ptr<uint8_t[]> buf_;
};

compressor_t::state_t compressor_t::compress(int dirfd, const char *file_name) {
  // update state
  assert(state_ == state_t::Ready);
  state_ = state_t::Compressing;

  // open input file and temporary output file
  int in_fd = openat(dirfd, file_name, O_RDONLY);
  if (in_fd < 0) return state_ = state_t::Error;
  char temp_file_name[] = "/tmp/fesvr-compress-XXXXXX";
  int temp_fd = mkstemp(temp_file_name);
  if (temp_fd < 0) return state_ = state_t::Error;
  auto error = [this, in_fd, temp_fd, temp_file_name]() {
    close(in_fd);
    close(temp_fd);
    unlink(temp_file_name);
    return state_ = state_t::Error;
  };

  // check/skip the first byte
  uint8_t first_byte;
  if (read(in_fd, &first_byte, 1) != 1 || first_byte ||
      write(temp_fd, "\x01", 1) != 1) {
    return error();
  }

  // compress
  if (!compress_file(temp_fd, in_fd)) return error();

  // get the size of the compressed file and the original file
  struct stat st;
  if (fstat(temp_fd, &st) < 0) return error();
  size_t compressed_size = st.st_size;
  if (fstat(in_fd, &st) < 0) return error();
  size_t original_size = st.st_size;

  // close input file and temporary output file
  if (close(in_fd) < 0 || close(temp_fd) < 0) return error();

  // check the compressed size
  if (compressed_size >= original_size * compress_threshold_) {
    if (unlink(temp_file_name) < 0) return error();
  } else {
    if (!rename_file(AT_FDCWD, temp_file_name, dirfd, file_name)) {
      return error();
    }
  }
  return state_ = state_t::Done;
}

bool compressor_t::compress_file(int out_fd, int in_fd) {
  queue_t queue(kQueueSize);
  write_buffer_t write_buf(out_fd, write_buf_size_);
  auto file_buf = std::make_unique<uint8_t[]>(kFileBufSize);
  off_t i = lseek(in_fd, 0, SEEK_CUR);
  if (i < 0) return false;

  ssize_t ret;
  while ((ret = pread(in_fd, file_buf.get(), kFileBufSize, i)) > 0) {
    size_t offset = 0, length = 1, j = 1;
    while (j <= queue.len()) {
      size_t k = queue.len() - j;
      if (file_buf[0] == queue[k]) {
        size_t cur_len = 0;
        for (size_t l = 0; l < j; l++) {
          if (l >= static_cast<size_t>(ret) || file_buf[l] != queue[k + l]) {
            break;
          }
          cur_len++;
        }
        if (cur_len > length) {
          offset = j;
          length = cur_len;
        }
      }
      j++;
    }

    // write to output file
    if (!write_buf.write_byte(offset) || !write_buf.write_byte(length) ||
        !write_buf.write_byte(file_buf[0])) {
      return false;
    }

    // update queue
    for (size_t k = 0; k < length; k++) queue.push(file_buf[k]);
    i += length;
  }

  if (ret < 0 || !write_buf.flush()) return false;
  return true;
}

bool compressor_t::rename_file(int olddirfd, const char *oldpath, int newdirfd,
                               const char *newpath) {
  if (renameat(olddirfd, oldpath, newdirfd, newpath) < 0) {
    if (errno == EXDEV) {
      // open old file and new file
      int old_fd = openat(olddirfd, oldpath, O_RDONLY);
      int new_fd =
          openat(newdirfd, newpath, O_WRONLY | O_TRUNC | O_CREAT, 0644);
      if (old_fd < 0 || new_fd < 0) return false;
      // get the size of the old file
      struct stat st;
      if (fstat(old_fd, &st) < 0) return false;
      // copy the old file to the new file
      off_t offset = 0;
      while (offset < st.st_size) {
        ssize_t ret = sendfile(new_fd, old_fd, &offset, st.st_size - offset);
        if (ret < 0) return false;
      }
      // close old file and new file
      if (close(old_fd) < 0 || close(new_fd) < 0) return false;
      // remove old file
      if (unlinkat(olddirfd, oldpath, 0) < 0) return false;
    } else {
      return false;
    }
  }
  return true;
}

ssize_t compressors_t::compress(int dirfd, const char *file_name) {
  ssize_t i = select_compressor();
  if (i < 0) return i;
  compressors_[i].ready();
  std::string file_name_copy(file_name);
  std::thread([this, i, dirfd, file_name = std::move(file_name_copy)]() {
    compressors_[i].compress(dirfd, file_name.c_str());
  }).detach();
  return i;
}

compressor_t::state_t compressors_t::take_if_done(size_t i) {
  assert(i < compressors_.size());
  auto state = compressors_[i].state();
  if (state == compressor_t::state_t::Done ||
      state == compressor_t::state_t::Error) {
    compressors_[i].reset();
  }
  return state;
}

ssize_t compressors_t::select_compressor() {
  for (size_t i = 0; i < compressors_.size(); i++) {
    if (compressors_[i].state() == compressor_t::state_t::Idle) return i;
  }
  return -1;
}
