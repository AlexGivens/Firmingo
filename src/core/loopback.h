#pragma once
#include "stream.h"
#include <algorithm>
#include <cstring>

namespace firmingo {
// Qualification backend: one bounded batch, with no transformations.
class Loopback : public StreamBackend {
 public:
  bool connected() const override { return true; }
  std::size_t read(std::uint8_t* data, std::size_t capacity) override {
    const auto count = std::min(capacity, size_);
    std::memcpy(data, bytes_ + offset_, count);
    offset_ += count;
    size_ -= count;
    if (!size_) offset_ = 0;
    return count;
  }
  std::size_t write(const std::uint8_t* data, std::size_t size) override {
    if (size_) return 0;
    size_ = std::min(size, sizeof(bytes_));
    std::memcpy(bytes_, data, size_);
    return size_;
  }
  void clear() { offset_ = size_ = 0; }
  void discard() override { clear(); }
 private:
  std::uint8_t bytes_[Stream::capacity]{};
  std::size_t offset_ = 0, size_ = 0;
};
}
