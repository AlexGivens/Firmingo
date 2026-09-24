#include "json.h"
#include <cstring>

namespace firmingo { namespace json {
static int hex(uint8_t b) {
  if (b >= '0' && b <= '9') return b - '0';
  if (b >= 'a' && b <= 'f') return b - 'a' + 10;
  if (b >= 'A' && b <= 'F') return b - 'A' + 10;
  return -1;
}
static bool hex4(const uint8_t* b, std::size_t size, std::size_t& p, uint32_t& cp) {
  if (size - p < 4) return false;
  cp = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const int digit = hex(b[p++]); if (digit < 0) return false;
    cp = (cp << 4) | unsigned(digit);
  }
  return true;
}
// Decode/validate one string. The same routine validates and compares keys,
// preventing alternate escaped spellings from bypassing duplicate detection.
static bool text(const uint8_t* b, std::size_t size, std::size_t& p,
                 char* out, std::size_t capacity) {
  if (p >= size || b[p++] != '"') return false;
  unsigned used = 0;
  while (p < size) {
    uint32_t cp = b[p++];
    if (cp == '"') {
      if (out) { if (used >= capacity) return false; out[used] = 0; }
      return true;
    }
    if (cp < 0x20) return false;
    if (cp == '\\') {
      if (p == size) return false;
      switch (b[p++]) {
        case '"': cp = '"'; break; case '\\': cp = '\\'; break; case '/': cp = '/'; break;
        case 'b': cp = 8; break; case 'f': cp = 12; break; case 'n': cp = 10; break;
        case 'r': cp = 13; break; case 't': cp = 9; break;
        case 'u': {
          if (!hex4(b,size,p,cp)) return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (size - p < 6 || b[p++] != '\\' || b[p++] != 'u') return false;
            uint32_t low;
            if (!hex4(b,size,p,low) || low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
          } else if (cp >= 0xdc00 && cp <= 0xdfff) return false;
          break;
        }
        default: return false;
      }
    } else if (cp >= 0x80) {
      unsigned remaining; uint32_t minimum;
      if (cp >= 0xc2 && cp <= 0xdf) { cp &= 0x1f; remaining = 1; minimum = 0x80; }
      else if (cp >= 0xe0 && cp <= 0xef) { cp &= 0x0f; remaining = 2; minimum = 0x800; }
      else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; remaining = 3; minimum = 0x10000; }
      else return false;
      if (size - p < remaining) return false;
      for (unsigned i = 0; i < remaining; ++i) {
        if ((b[p] & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (b[p++] & 0x3f);
      }
      if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    if (!cp) return false;
    uint8_t encoded[4]; unsigned n;
    if (cp < 0x80) { n = 1; encoded[0] = uint8_t(cp); }
    else if (cp < 0x800) { n = 2; encoded[0] = uint8_t(0xc0 | (cp >> 6)); }
    else if (cp < 0x10000) { n = 3; encoded[0] = uint8_t(0xe0 | (cp >> 12)); }
    else { n = 4; encoded[0] = uint8_t(0xf0 | (cp >> 18)); }
    for (unsigned i = 1; i < n; ++i) encoded[i] = uint8_t(0x80 | ((cp >> (6 * (n - 1 - i))) & 0x3f));
    if (used + n > Document::max_string || (out && used + n >= capacity)) return false;
    if (out) for (unsigned i = 0; i < n; ++i) out[used + i] = char(encoded[i]);
    used += n;
  }
  return false;
}
void Document::whitespace() {
  while (position_ < size_ && (bytes_[position_] == ' ' || bytes_[position_] == '\t' ||
         bytes_[position_] == '\r' || bytes_[position_] == '\n')) ++position_;
}
bool Document::quoted(unsigned parent, bool key) {
  if (count_ == max_tokens) return false;
  const unsigned index = count_++;
  tokens_[index] = {Kind::string,uint16_t(position_),0,uint16_t(parent),uint16_t(index+1),key};
  if (!text(bytes_,size_,position_,nullptr,0)) return false;
  tokens_[index].end = uint16_t(position_);
  if (key) {
    char name[65], previous[65];
    if (!string(index,name,sizeof(name))) return false;
    for (unsigned i = 0; i < index; ++i) {
      if (tokens_[i].key && tokens_[i].parent == parent) {
        if (!string(i,previous,sizeof(previous)) || std::strcmp(name,previous) == 0) return false;
      }
    }
  }
  return true;
}
static bool digit(uint8_t b) { return b >= '0' && b <= '9'; }
bool Document::value(unsigned parent, unsigned depth) {
  whitespace();
  if (depth > max_depth || position_ == size_ || count_ == max_tokens) return false;
  if (bytes_[position_] == '"') return quoted(parent,false);
  const unsigned index = count_++;
  const auto start = position_;
  const uint8_t c = bytes_[position_];
  if ((c == '{' || c == '[') && depth == max_depth) return false;
  tokens_[index] = {Kind::null_value,uint16_t(start),0,uint16_t(parent),0,false};
  if (c == '{' || c == '[') {
    const bool object = c == '{';
    tokens_[index].kind = object ? Kind::object : Kind::array;
    const uint8_t closing = object ? '}' : ']';
    ++position_; whitespace();
    if (position_ < size_ && bytes_[position_] == closing) ++position_;
    else {
      while (true) {
        if (object) {
          if (!quoted(index,true)) return false;
          whitespace(); if (position_ == size_ || bytes_[position_++] != ':') return false;
        }
        if (!value(index,depth+1)) return false;
        whitespace(); if (position_ == size_) return false;
        if (bytes_[position_] == closing) { ++position_; break; }
        if (bytes_[position_++] != ',') return false;
        whitespace();
      }
    }
  } else if (c == 't' || c == 'f' || c == 'n') {
    const char* literal = c == 't' ? "true" : (c == 'f' ? "false" : "null");
    const auto length = std::strlen(literal);
    if (size_ - position_ < length || std::memcmp(bytes_+position_,literal,length)) return false;
    position_ += length; tokens_[index].kind = c == 'n' ? Kind::null_value : Kind::boolean;
  } else {
    tokens_[index].kind = Kind::number;
    if (c == '-') ++position_;
    if (position_ == size_ || !digit(bytes_[position_])) return false;
    if (bytes_[position_] == '0') ++position_;
    else while (position_ < size_ && digit(bytes_[position_])) ++position_;
    if (position_ < size_ && bytes_[position_] == '.') {
      ++position_; const auto first = position_;
      while (position_ < size_ && digit(bytes_[position_])) ++position_;
      if (position_ == first) return false;
    }
    if (position_ < size_ && (bytes_[position_] == 'e' || bytes_[position_] == 'E')) {
      ++position_;
      if (position_ < size_ && (bytes_[position_] == '+' || bytes_[position_] == '-')) ++position_;
      const auto first = position_;
      while (position_ < size_ && digit(bytes_[position_])) ++position_;
      if (position_ == first) return false;
    }
  }
  tokens_[index].end = uint16_t(position_); tokens_[index].next = uint16_t(count_);
  return true;
}
bool Document::parse(const uint8_t* bytes, std::size_t size) {
  count_ = 0; position_ = 0; bytes_ = bytes; size_ = size;
  if (!bytes || !size || size > 4096 || !value(0xffff,0)) { count_ = 0; return false; }
  whitespace();
  if (position_ != size_) { count_ = 0; return false; }
  return true;
}
bool Document::string(unsigned index, char* out, std::size_t capacity) const {
  if (index >= count_ || tokens_[index].kind != Kind::string || !out) return false;
  std::size_t p = tokens_[index].start;
  return text(bytes_,tokens_[index].end,p,out,capacity);
}
int Document::find(const char* key) const {
  if (!count_ || tokens_[0].kind != Kind::object) return -1;
  char name[65];
  for (unsigned i = 1; i < count_; ++i) {
    if (tokens_[i].parent == 0 && tokens_[i].key && string(i,name,sizeof(name)) && !std::strcmp(name,key)) return int(i+1);
  }
  return -1;
}
bool Document::uint32(unsigned index, uint32_t& value_out) const {
  if (index >= count_ || tokens_[index].kind != Kind::number) return false;
  uint32_t value = 0;
  for (unsigned i = tokens_[index].start; i < tokens_[index].end; ++i) {
    if (!digit(bytes_[i])) return false;
    const unsigned n = bytes_[i] - '0';
    if (value > (UINT32_MAX - n) / 10) return false;
    value = value * 10 + n;
  }
  value_out = value; return true;
}
bool Document::boolean(unsigned index, bool& value_out) const {
  if (index >= count_ || tokens_[index].kind != Kind::boolean) return false;
  value_out = bytes_[tokens_[index].start] == 't'; return true;
}
} }
