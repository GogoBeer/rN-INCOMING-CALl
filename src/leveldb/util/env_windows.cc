// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Prevent Windows headers from defining min/max macros and instead
// use STL.
#ifndef NOMINMAX
#define NOMINMAX
#endif  // ifndef NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "leveldb/env.h"
#include "leveldb/slice.h"
#include "port/port.h"
#include "port/thread_annotations.h"
#include "util/env_windows_test_helper.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/windows_logger.h"

#if defined(DeleteFile)
#undef DeleteFile
#endif  // defined(DeleteFile)

namespace leveldb {

namespace {

constexpr const size_t kWritableFileBufferSize = 65536;

// Up to 1000 mmaps for 64-bit binaries; none for 32-bit.
constexpr int kDefaultMmapLimit = (sizeof(void*) >= 8) ? 1000 : 0;

// Can be set by by EnvWindowsTestHelper::SetReadOnlyMMapLimit().
int g_mmap_limit = kDefaultMmapLimit;

std::string GetWindowsErrorMessage(DWORD error_code) {
  std::string message;
  char* error_text = nullptr;
  // Use MBCS version of FormatMessage to match return value.
  size_t error_text_size = ::FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<char*>(&error_text), 0, nullptr);
  if (!error_text) {
    return message;
  }
  message.assign(error_text, error_text_size);
  ::LocalFree(error_text);
  return message;
}

Status WindowsError(const std::string& context, DWORD error_code) {
  if (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND)
    return Status::NotFound(context, GetWindowsErrorMessage(error_code));
  return Status::IOError(context, GetWindowsErrorMessage(error_code));
}

class ScopedHandle {
 public:
  ScopedHandle(HANDLE handle) : handle_(handle) {}
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.Release()) {}
  ~ScopedHandle() { Close(); }

  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle& operator=(ScopedHandle&& rhs) noexcept {
    if (this != &rhs) handle_ = rhs.Release();
    return *this;
  }

  bool Close() {
    if (!is_valid()) {
      return true;
    }
    HANDLE h = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return ::CloseHandle(h);
  }

  bool is_valid() const {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }

  HANDLE get() const { return handle_; }

  HANDLE Release() {
    HANDLE h = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return h;
  }

 private:
  HANDLE handle_;
};

// Helper class to limit resource usage to avoid exhaustion.
// Currently used to limit read-only file descriptors and mmap file usage
// so that we do not run out of file descriptors or virtual memory, or run into
// kernel performance problems for very large databases.
class Limiter {
 public:
  // Limit maximum number of resources to |max_acquires|.
  Limiter(int max_acquires) : acquires_allowed_(max_acquires) {}

  Limiter(const Limiter&) = delete;
  Limiter operator=(const Limiter&) = delete;

  // If another resource is available, acquire it and return true.
  // Else return false.
  bool Acquire() {
    int old_acquires_allowed =
        acquires_allowed_.fetch_sub(1, std::memory_order_relaxed);

    if (old_acquires_allowed > 0) return true;

    acquires_allowed_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // Release a resource acquired by a previous call to Acquire() that returned
  // true.
  void Release() { acquires_allowed_.fetch_add(1, std::memory_order_relaxed); }

 private:
  // The number of available resources.
  //
  // This is a counter and is not tied to the invariants of any other class, so
  // it can be operated on safely using std::memory_order_relaxed.
  std::atomic<int> acquires_allowed_;
};

class WindowsSequentialFile : public SequentialFile {
 public:
  WindowsSequentialFile(std::string filename, ScopedHandle handle)
      : handle_(std::move(handle)), filename_(std::move(filename)) {}
  ~WindowsSequentialFile() override {}

  Status Read(size_t n, Slice* result, char* scratch) override {
    DWORD bytes_read;
    // DWORD is 32-bit, but size_t could technically be larger. However leveldb
    // files are limited to leveldb::Options::max_file_size which is clamped to
    // 1<<30 or 1 GiB.
    assert(n <= std::numeric_limits<DWORD>::max());
    if (!::ReadFile(handle_.get(), scratch, static_cast<DWORD>(n), &bytes_read,
                    nullptr)) {
      return WindowsError(filename_, ::GetLastError());
    }

    *result = Slice(scratch, bytes_read);
    return Status::OK();
  }

  Status Skip(uint64_t n) override {
    LARGE_INTEGER distance;
    distance.QuadPart = n;
    if (!::SetFilePointerEx(handle_.get(), distance, nullptr, FILE_CURRENT)) {
      return WindowsError(filename_, ::GetLastError());
    }
    return Status::OK();
  }

  std::string GetName() const override { return filename_; }

 private:
  const ScopedHandle handle_;
  const std::string filename_;
};

class WindowsRandomAccessFile : public RandomAccessFile {
 public:
  WindowsRandomAccessFile(std::string filename, ScopedHandle handle)
      : handle_(std::move(handle)), filename_(std::move(filename)) {}

  ~WindowsRandomAccessFile() override = default;

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    DWORD bytes_read = 0;
    OVERLAPPED overlapped = {0};

    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.Offset = static_cast<DWORD>(offset);
    if (!::ReadFile(handle_.get(), scratch, static_cast<DWORD>(n), &bytes_read,
                    &overlapped)) {
      DWORD error_code = ::GetLastError();
      if (error_code != ERROR_HANDLE_EOF) {
        *result = Slice(scratch, 0);
        return Status::IOError(filename_, GetWindowsErrorMessage(error_code));
      }
    }

    *result = Slice(scratch, bytes_read);
    return Status::OK();
  }

  std::string GetName() const override { return filename_; }

 private:
  const ScopedHandle handle_;
  const std::string filename_;
};

class WindowsMmapReadableFile : public RandomAccessFile {
 public:
  // base[0,length-1] contains the mmapped contents of the file.
  WindowsMmapReadableFile(std::string filename, char* mmap_base, size_t length,
                          Limiter* mmap_limiter)
      : mmap_base_(mmap_base),
        length_(length),
        mmap_limiter_(mmap_limiter),
        filename_(std::move(filename)) {}

  ~WindowsMmapReadableFile() override {
    ::UnmapViewOfFile(mmap_base_);
    mmap_limiter_->Release();
  }

  Status Read(uint64_t offset, size_t n, Slice* result,
              char* scratch) const override {
    if (offset + n > length_) {
      *result = Slice();
      return WindowsError(filename_, ERROR_INVALID_PARAMETER);
    }

    *result = Slice(mmap_base_ + offset, n);
    return Status::OK();
  }

  std::string GetName() const override { return filename_; }

 private:
  char* const mmap_base_;
  const size_t length_;
  Limiter* const mmap_limiter_;
  const std::string filename_;
};

class WindowsWritableFile : public WritableFile {
 public:
  WindowsWritableFile(std::string filename, ScopedHandle handle)
      : pos_(0), handle_(std::move(handle)), filename_(std::move(filename)) {}

  ~WindowsWritableFile() override = default;

  Status Append(const Slice& data) override {
    size_t write_size = data.size();
    const char* write_data = data.data();

    // Fit as much as possible into buffer.
    size_t copy_size = std::min(write_size, kWritableFileBufferSize - pos_);
    std::memcpy(buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;
    if (write_size == 0) {
      return Status::OK();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status status = FlushBuffer();
    if (!status.ok()) {
      return status;
    }

    // Small writes go to buffer, large writes are written directly.
    if (write_size < kWritableFileBufferSize) {
      std::memcpy(buf_, write_data, write_size);
      pos_ = write_size;
      return Status::OK();
    }
    return WriteUnbuffered(write_data, write_size);
  }

  Status Close() override {
    Status status = FlushBuffer();
    if (!handle_.Close() && status.ok()