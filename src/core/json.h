#pragma once
#include <cstddef>
#include <cstdint>

namespace firmingo { namespace json {
enum class Kind { object, array, string, number, boolean, null_value };
struct Token {
  Kind kind;
  uint16_t start, end, parent, next;
  bool key;
};
// Strict bounded JSON validation. Strings decode to at most 64 UTF-8 bytes;
// embedded NUL is rejected. Duplicate keys compare after escape decoding.
// Document borrows input; keep it unchanged/alive while reading tokens/values.
class Document {
 public:
  static constexpr unsigned max_tokens = 96, max_depth = 8, max_string = 64;
  bool parse(const uint8_t* bytes, std::size_t size);
  const Token& token(unsigned index) const { return tokens_[index]; }
  unsigned count() const { return count_; }
  int find(const char* key) const; // root object only, -1 if absent
  bool string(unsigned index, char* out, std::size_t capacity) const;
  bool uint32(unsigned index, uint32_t& value) const;
  bool boolean(unsigned index, bool& value) const;
 private:
  bool value(unsigned parent, unsigned depth);
  bool quoted(unsigned parent, bool key);
  void whitespace();
  const uint8_t* bytes_ = nullptr;
  std::size_t size_ = 0, position_ = 0;
  unsigned count_ = 0;
  Token tokens_[max_tokens]{};
};
} }
