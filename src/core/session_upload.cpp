#include "session.h"
#include "json.h"
#include <cstdio>
#include <cstring>

namespace firmingo {
void Session::upload_response(uint32_t request, UploadError result, const UploadStatus& value) {
  if (result != UploadError::none) { error(request,upload_error_name(result)); return; }
  char digest[65];
  const char hex[] = "0123456789abcdef";
  for (unsigned i = 0; i < 32; ++i) {
    digest[2*i] = hex[value.digest[i] >> 4]; digest[2*i+1] = hex[value.digest[i] & 15];
  }
  digest[64] = 0;
  const int n = std::snprintf(response_body_,sizeof(response_body_),
      "{\"ok\":true,\"result\":{\"upload_id\":%lu,\"state\":\"%s\",\"size\":%lu,"
      "\"received\":%lu,\"sha256\":\"%s\",\"slot_touched\":%s,\"error\":\"%s\"}}",
      static_cast<unsigned long>(value.id),upload_state_name(value.state),
      static_cast<unsigned long>(value.size),static_cast<unsigned long>(value.received),
      digest,value.slot_touched ? "true" : "false",upload_error_name(value.error));
  if (n < 0 || std::size_t(n) >= sizeof(response_body_)) finish(SessionResult::io_error);
  else if (std::size_t(n) > decoder_.limit()) error(request,"response_too_large");
  else queue(protocol::Type::response,request,reinterpret_cast<const uint8_t*>(response_body_),std::size_t(n));
}

bool Session::dispatch_upload(const json::Document& document) {
  if (!upload_) return false; // Preserve unsupported-method behavior in release profiles.
  const int op = document.find("op"); char operation[33];
  if (op < 0 || !document.string(unsigned(op),operation,sizeof(operation))) return false;
  const char* methods[] = {"flash.begin","flash.status","flash.finish","flash.commit","flash.abort"};
  unsigned method = 0;
  while (method < 5 && std::strcmp(operation,methods[method])) ++method;
  if (method == 5) return false;
  const auto request = decoder_.request();
  const char* fields[] = {"op","target_id","board_id","format","size","sha256","upload_id"};
  const unsigned required = method == 0 ? 63u : 67u;
  unsigned present = 0;
  for (unsigned i = 1; i < document.count(); ++i) {
    if (!document.token(i).key || document.token(i).parent != 0) continue;
    char key[65];
    if (!document.string(i,key,sizeof(key))) { error(request,"invalid_argument"); return true; }
    unsigned field = 0;
    while (field < 7 && std::strcmp(key,fields[field])) ++field;
    if (field == 7) { error(request,"invalid_argument"); return true; }
    present |= 1u << field;
  }
  uint32_t target = 0, id = 0, size = 0;
  uint8_t digest[32]{};
  char board[33]{}, format[33]{};
  const auto number = [&](const char* key, uint32_t& value) {
    const int index = document.find(key);
    return index >= 0 && document.uint32(unsigned(index),value) && value != 0;
  };
  bool valid = document.token(0).kind == json::Kind::object && present == required &&
               number("target_id",target);
  if (method == 0) {
    char hash[65];
    const int board_index = document.find("board_id"), format_index = document.find("format"),
              hash_index = document.find("sha256");
    valid = valid && number("size",size) && board_index >= 0 && format_index >= 0 && hash_index >= 0 &&
        document.string(unsigned(board_index),board,sizeof(board)) && board[0] &&
        document.string(unsigned(format_index),format,sizeof(format)) && format[0] &&
        document.string(unsigned(hash_index),hash,sizeof(hash)) && std::strlen(hash) == 64;
    if (valid) for (unsigned i = 0; i < 64; ++i) {
      unsigned nibble = hash[i] >= '0' && hash[i] <= '9' ? unsigned(hash[i]-'0') :
                        hash[i] >= 'a' && hash[i] <= 'f' ? unsigned(hash[i]-'a'+10) : 16u;
      if (nibble == 16) { valid = false; break; }
      digest[i/2] = uint8_t((digest[i/2] << 4) | nibble);
    }
  } else valid = valid && number("upload_id",id);
  if (!valid) error(request,"invalid_argument");
  else if (!negotiated_) error(request,"invalid_state");
  else if (!upload_) error(request,"unsupported");
  else if (target != upload_->target().id || (method == 0 &&
           (std::strcmp(board,identity_.board_id) || std::strcmp(format,upload_->target().format))))
    error(request,"wrong_target");
  else if ((method == 0 || method == 3) && channel_.owner()) error(request,"busy");
  else {
    UploadStatus status;
    UploadError result = UploadError::io_error;
    switch (method) {
      case 0: result = upload_->begin(owner_,size,digest,status); break;
      case 1: result = upload_->status(id,status); break;
      case 2: result = upload_->finish(owner_,id,status); break;
      case 3: result = upload_->commit(owner_,id,status); break;
      case 4: result = upload_->abort(owner_,id,status); break;
    }
    upload_response(request,result,status);
  }
  return true;
}
}  // namespace firmingo
