#include "session.h"
#include "json.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace firmingo {
using namespace protocol;
static void add(uint64_t& counter, std::size_t n) {
  counter = n > UINT64_MAX - counter ? UINT64_MAX : counter + n;
}
std::size_t Channel::read(uint8_t* data, std::size_t capacity) {
  const auto n = backend_.read(data,capacity); if (n <= capacity) add(tx_bytes_,n); return n;
}
std::size_t Channel::write(const uint8_t* data, std::size_t size) {
  const auto n = backend_.write(data,size); if (n <= size) add(rx_bytes_,n); return n;
}
static StreamResult stream_result(BackendResult result) {
  switch (result) {
    case BackendResult::ok: return StreamResult::ok;
    case BackendResult::unsupported: return StreamResult::unsupported;
    case BackendResult::invalid_argument: return StreamResult::invalid_argument;
    case BackendResult::busy: return StreamResult::busy;
    case BackendResult::io_error: return StreamResult::io_error;
  }
  return StreamResult::io_error;
}
StreamResult Channel::acquire(uint32_t owner, uint32_t now,
                              const SerialConfiguration* configuration) {
  if (stream_.owner()) return StreamResult::busy;
  if (configuration) {
    const auto configured = backend_.configure(*configuration);
    if (configured != BackendResult::ok) return stream_result(configured);
  }
  const auto prepared = backend_.prepare_open();
  if (prepared != BackendResult::ok) return stream_result(prepared);
  const auto result = stream_.open(owner,now);
  if (result == StreamResult::ok) {
    backend_.discard(); backend_.opened(owner); rx_bytes_ = tx_bytes_ = 0;
  }
  return result;
}
BackendResult Channel::configure(uint32_t owner,
                                 const SerialConfiguration& configuration) {
  if (!owner || owner != stream_.owner()) return BackendResult::io_error;
  if (stream_.pending_to_backend() || stream_.pending_to_peer())
    return BackendResult::busy;
  return backend_.configure(configuration);
}
void Channel::release(uint32_t owner) {
  if (stream_.close(owner) == StreamResult::ok) {
    backend_.closed(); backend_.discard();
  }
}
StreamResult Channel::poll(uint32_t owner, ByteIO& peer, uint32_t now) {
  const auto result = stream_.poll(owner,peer,*this,now);
  if (result != StreamResult::ok && result != StreamResult::invalid_owner) {
    backend_.closed(); backend_.discard();
  }
  return result;
}
static bool hex_id(const char* s) {
  for (unsigned i = 0; i < 16; ++i) if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
  return s[16] == 0;
}
static bool name(const char* s) {
  for (unsigned i = 0; i <= 32; ++i) {
    const char c = s[i]; if (!c) return i != 0;
    if (i == 32 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  }
  return false;
}
Session::Session(Channel& channel, const Identity& identity, uint32_t owner, uint32_t now, const MemorySamples* memory)
  : channel_(channel), memory_(memory), identity_(identity), owner_(owner), started_at_(now), progress_at_(now) {
  if (!owner || !hex_id(identity_.device_id) || !hex_id(identity_.boot_id) ||
      !name(identity_.board_id) || !name(identity_.firmware_version)) result_ = SessionResult::io_error;
}
Session::~Session() { close(); }
void Session::close() {
  if (owned_) channel_.release(owner_);
  owned_ = false; decoder_.reset(); output_size_ = output_offset_ = data_offset_ = 0;
  if (result_ == SessionResult::running) result_ = SessionResult::closed;
}
SessionResult Session::finish(SessionResult result) { close(); result_ = result; return result_; }
void Session::queue(Type type, uint32_t request, const uint8_t* payload, std::size_t size) {
  if (output_size_ || size > decoder_.limit()) { finish(SessionResult::io_error); return; }
  output_size_ = encode(output_,sizeof(output_),type,request,payload,size); output_offset_ = 0;
  if (!output_size_) finish(SessionResult::io_error);
}
void Session::error(uint32_t request, const char* code) {
  char body[128];
  const int n = std::snprintf(body,sizeof(body),"{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",code,code);
  if (n <= 0 || std::size_t(n) >= sizeof(body)) { finish(SessionResult::io_error); return; }
  queue(Type::response,request,reinterpret_cast<const uint8_t*>(body),std::size_t(n));
}
void Session::success(uint32_t request) {
  const char body[] = "{\"ok\":true,\"result\":{}}";
  queue(Type::response,request,reinterpret_cast<const uint8_t*>(body),sizeof(body)-1);
}
enum class Command { hello, open, close, status, configure, control, reset, diagnostics, unknown };
struct Request {
  Command command = Command::unknown;
  uint32_t offer = max_payload, channel = 0;
  bool config_present = false, config_nonempty = false, config_complete = false;
  SerialConfiguration configuration{};
};
static int child(const json::Document& d, unsigned parent, const char* key) {
  char name[65];
  for (unsigned i = parent + 1; i < d.count(); ++i) {
    if (d.token(i).parent == parent && d.token(i).key &&
        d.string(i,name,sizeof(name)) && !std::strcmp(name,key)) return int(i+1);
  }
  return -1;
}
static bool serial_configuration(const json::Document& d, unsigned object, Request& r) {
  if (d.token(object).kind != json::Kind::object) return false;
  r.config_present = true;
  const char* fields[] = {"baud","data_bits","parity","stop_bits","flow_control"};
  for (unsigned i = object + 1; i < d.count(); ++i) {
    if (d.token(i).parent != object || !d.token(i).key) continue;
    char key[65]; if (!d.string(i,key,sizeof(key))) return false;
    unsigned field = 0;
    while (field < 5 && std::strcmp(key,fields[field])) ++field;
    if (field == 5) return false;
    r.config_nonempty = true;
  }
  uint32_t value = 0;
  const int baud = child(d,object,"baud");
  if (baud >= 0 && !d.uint32(unsigned(baud),r.configuration.baud)) return false;
  const int data = child(d,object,"data_bits");
  if (data >= 0) {
    if (!d.uint32(unsigned(data),value) || value > UINT8_MAX) return false;
    r.configuration.data_bits = uint8_t(value);
  }
  const int stop = child(d,object,"stop_bits");
  if (stop >= 0) {
    if (!d.uint32(unsigned(stop),value) || value > UINT8_MAX) return false;
    r.configuration.stop_bits = uint8_t(value);
  }
  const int parity = child(d,object,"parity");
  if (parity >= 0) {
    char text[16]; if (!d.string(unsigned(parity),text,sizeof(text))) return false;
    if (!std::strcmp(text,"none")) r.configuration.parity = SerialParity::none;
    else if (!std::strcmp(text,"even")) r.configuration.parity = SerialParity::even;
    else if (!std::strcmp(text,"odd")) r.configuration.parity = SerialParity::odd;
    else return false;
  }
  const int flow = child(d,object,"flow_control");
  if (flow >= 0) {
    char text[16];
    if (!d.string(unsigned(flow),text,sizeof(text)) || std::strcmp(text,"none")) return false;
  }
  r.config_complete = baud >= 0 && data >= 0 && parity >= 0 && stop >= 0;
  return true;
}
static bool request(const json::Document& d, Request& r) {
  if (d.token(0).kind != json::Kind::object) return false;
  const int op = d.find("op"); char operation[33];
  if (op < 0 || !d.string(unsigned(op),operation,sizeof(operation)) || !operation[0]) return false;
  const char* operations[] = {"hello","serial.open","serial.close","serial.status","serial.configure","serial.control","device.reset","device.diagnostics"};
  for (unsigned i = 0; i < 8; ++i) if (!std::strcmp(operation,operations[i])) r.command = static_cast<Command>(i);
  if (r.command == Command::unknown) return true;
  const char* fields[] = {"op","max_payload","sdk_version","channel_id","config","control","value","scope"};
  unsigned present = 0;
  for (unsigned i = 1; i < d.count(); ++i) {
    if (!d.token(i).key || d.token(i).parent != 0) continue;
    char key[65]; if (!d.string(i,key,sizeof(key))) return false;
    unsigned field; for (field = 0; field < 8; ++field) if (!std::strcmp(key,fields[field])) break;
    if (field == 8) return false;
    present |= 1u << field;
  }
  unsigned allowed = 1;
  switch (r.command) {
    case Command::hello: allowed |= 2 | 4; break;
    case Command::open: case Command::configure: allowed |= 8 | 16; break;
    case Command::close: case Command::status: allowed |= 8; break;
    case Command::control: allowed |= 8 | 32 | 64; break;
    case Command::reset: allowed |= 128; break;
    default: break;
  }
  if (present & ~allowed) return false;
  if (r.command == Command::hello) {
    const int offer = d.find("max_payload"), sdk = d.find("sdk_version"); char version[65];
    if (offer >= 0 && (!d.uint32(unsigned(offer),r.offer) || r.offer < 512 || r.offer > 65536)) return false;
    return sdk < 0 || d.string(unsigned(sdk),version,sizeof(version));
  }
  if (r.command == Command::diagnostics) return true;
  if (r.command == Command::reset) {
    const int scope = d.find("scope"); char value[33];
    return scope >= 0 && d.string(unsigned(scope),value,sizeof(value)) && !std::strcmp(value,"device");
  }
  const int channel = d.find("channel_id");
  if (channel < 0 || !d.uint32(unsigned(channel),r.channel) || !r.channel) return false;
  const int config = d.find("config");
  if (config >= 0) {
    if (!serial_configuration(d,unsigned(config),r)) return false;
  }
  if (r.command == Command::configure && config < 0) return false;
  if (r.command == Command::control) {
    const int control = d.find("control"), value = d.find("value"); char selector[33];
    if (control < 0 || value < 0 || !d.string(unsigned(control),selector,sizeof(selector)) || !selector[0]) return false;
    if (!std::strcmp(selector,"break") || !std::strcmp(selector,"dtr") || !std::strcmp(selector,"rts")) {
      bool state; return d.boolean(unsigned(value),state);
    }
    const auto kind = d.token(unsigned(value)).kind;
    if (kind == json::Kind::number) { uint32_t number; return d.uint32(unsigned(value),number); }
    return kind == json::Kind::string || kind == json::Kind::boolean;
  }
  return true;
}
static const char* parity_name(SerialParity parity) {
  switch (parity) {
    case SerialParity::none: return "none";
    case SerialParity::even: return "even";
    case SerialParity::odd: return "odd";
  }
  return "none";
}
static const char* backend_error(BackendResult result) {
  switch (result) {
    case BackendResult::unsupported: return "unsupported";
    case BackendResult::invalid_argument: return "invalid_argument";
    case BackendResult::busy: return "busy";
    case BackendResult::io_error: return "io_error";
    case BackendResult::ok: break;
  }
  return "io_error";
}
void Session::dispatch(uint32_t now) {
  const auto type = decoder_.type(); const auto id = decoder_.request();
  if (type == Type::serial) {
    if (!negotiated_ || !owned_ || decoder_.size() <= 4 || get32(decoder_.payload()) != 1) finish(SessionResult::protocol_error);
    return; // Stream consumes this frame through read().
  }
  if (type == Type::response || type == Type::event) { finish(SessionResult::protocol_error); return; }
  if (type == Type::upload) { error(id,negotiated_ ? "unsupported" : "invalid_state"); decoder_.reset(); return; }
  json::Document document;
  if (!document.parse(decoder_.payload(),decoder_.size())) {
    error(id,"invalid_argument"); decoder_.reset();
    if (owned_) channel_.release(owner_);
    owned_ = false; closing_ = true; closing_at_ = now; return;
  }
  Request r;
  if (!request(document,r)) { error(id,"invalid_argument"); decoder_.reset(); return; }
  if (!negotiated_ && r.command != Command::hello) { error(id,"invalid_state"); decoder_.reset(); return; }
  char body[response_capacity+1]; int n = 0;
  if (r.command == Command::hello) {
    if (negotiated_) error(id,"invalid_state");
    else {
      decoder_.reset();
      if (!decoder_.negotiate(r.offer)) { finish(SessionResult::io_error); return; }
      negotiated_ = true;
      if (channel_.backend_kind() == BackendKind::uart) {
        SerialConfiguration configuration;
        if (!channel_.configuration(configuration)) { finish(SessionResult::io_error); return; }
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"protocol_major\":1,\"firmware_version\":\"%s\",\"device_id\":\"%s\","
          "\"boot_id\":\"%s\",\"board_id\":\"%s\",\"max_payload\":%u,\"auth_mode\":\"open-development\","
          "\"channels\":[{\"id\":1,\"backend\":\"uart\",\"controls\":[],\"rx_queue\":256,\"tx_queue\":256,"
          "\"owned\":%s,\"config\":{\"baud\":%lu,\"actual_baud\":%lu,\"data_bits\":%u,"
          "\"parity\":\"%s\",\"stop_bits\":%u,\"flow_control\":\"none\"},"
          "\"configurable\":[\"baud\",\"data_bits\",\"parity\",\"stop_bits\"],"
          "\"baud_min\":300,\"baud_max\":2000000}],\"targets\":[]}}",
          identity_.firmware_version,identity_.device_id,identity_.boot_id,
          identity_.board_id,unsigned(decoder_.limit()),channel_.owner() ? "true" : "false",
          static_cast<unsigned long>(configuration.baud),
          static_cast<unsigned long>(configuration.actual_baud),unsigned(configuration.data_bits),
          parity_name(configuration.parity),unsigned(configuration.stop_bits));
      } else {
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"protocol_major\":1,\"firmware_version\":\"%s\",\"device_id\":\"%s\","
          "\"boot_id\":\"%s\",\"board_id\":\"%s\",\"max_payload\":%u,\"auth_mode\":\"open-development\","
          "\"channels\":[{\"id\":1,\"backend\":\"application\",\"controls\":[],\"rx_queue\":256,\"tx_queue\":256,"
          "\"owned\":%s}],\"targets\":[]}}",identity_.firmware_version,identity_.device_id,identity_.boot_id,
          identity_.board_id,unsigned(decoder_.limit()),channel_.owner() ? "true" : "false");
      }
    }
  } else if (r.command == Command::diagnostics) {
    if (!memory_ || !memory_->count()) error(id,"unsupported");
    else {
      BackendDiagnostics backend;
      TransportDiagnostics transport;
      const bool has_backend = channel_.backend_diagnostics(backend);
      const bool has_transport = memory_->transport(transport);
      if (has_backend && has_transport && channel_.backend_kind() == BackendKind::uart)
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u,\"uart_rx_pending\":%u,"
          "\"uart_rx_discarded\":%llu,"
          "\"uart_rx_overrun_events\":%lu,\"uart_rx_lost_bytes_minimum\":%llu,"
          "\"uart_tx_throttles\":%lu,"
          "\"ncm_worker_runs\":%lu,\"ncm_rx_frames\":%lu,\"ncm_rx_deferred\":%lu,"
          "\"ncm_rx_batch_peak\":%lu,\"ncm_mutex_contentions\":%lu,"
          "\"ncm_budget_exhaustions\":%lu,\"ncm_wake_requests\":%lu,\"tcp_accepts\":%lu,"
          "\"tcp_rx_callbacks\":%lu,\"tcp_rx_bytes\":%llu,\"tcp_sent_callbacks\":%lu,"
          "\"tcp_sent_bytes\":%llu,\"tcp_errors\":%lu}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()),
          unsigned(backend.rx_pending),static_cast<unsigned long long>(backend.rx_discarded),
          static_cast<unsigned long>(backend.rx_overrun_events),
          static_cast<unsigned long long>(backend.rx_lost_bytes_minimum),
          static_cast<unsigned long>(backend.tx_throttle_events),
          static_cast<unsigned long>(transport.ncm_worker_runs),static_cast<unsigned long>(transport.ncm_rx_frames),
          static_cast<unsigned long>(transport.ncm_rx_deferred),
          static_cast<unsigned long>(transport.ncm_rx_batch_peak),
          static_cast<unsigned long>(transport.ncm_mutex_contentions),
          static_cast<unsigned long>(transport.ncm_budget_exhaustions),
          static_cast<unsigned long>(transport.ncm_wake_requests),static_cast<unsigned long>(transport.tcp_accepts),
          static_cast<unsigned long>(transport.tcp_rx_callbacks),
          static_cast<unsigned long long>(transport.tcp_rx_bytes),
          static_cast<unsigned long>(transport.tcp_sent_callbacks),
          static_cast<unsigned long long>(transport.tcp_sent_bytes),
          static_cast<unsigned long>(transport.tcp_errors));
      else if (has_backend && has_transport)
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u,\"application_rx_pending\":%u,"
          "\"application_tx_pending\":%u,\"application_rx_peak\":%u,\"application_tx_peak\":%u,"
          "\"application_rx_discarded\":%llu,\"application_tx_discarded\":%llu,"
          "\"ncm_worker_runs\":%lu,\"ncm_rx_frames\":%lu,\"ncm_rx_deferred\":%lu,"
          "\"ncm_rx_batch_peak\":%lu,\"ncm_mutex_contentions\":%lu,"
          "\"ncm_budget_exhaustions\":%lu,\"ncm_wake_requests\":%lu,"
          "\"tcp_accepts\":%lu,"
          "\"tcp_rx_callbacks\":%lu,\"tcp_rx_bytes\":%llu,\"tcp_sent_callbacks\":%lu,"
          "\"tcp_sent_bytes\":%llu,\"tcp_errors\":%lu}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()),
          unsigned(backend.rx_pending),unsigned(backend.tx_pending),unsigned(backend.rx_peak),unsigned(backend.tx_peak),
          static_cast<unsigned long long>(backend.rx_discarded),static_cast<unsigned long long>(backend.tx_discarded),
          static_cast<unsigned long>(transport.ncm_worker_runs),static_cast<unsigned long>(transport.ncm_rx_frames),
          static_cast<unsigned long>(transport.ncm_rx_deferred),
          static_cast<unsigned long>(transport.ncm_rx_batch_peak),
          static_cast<unsigned long>(transport.ncm_mutex_contentions),
          static_cast<unsigned long>(transport.ncm_budget_exhaustions),
          static_cast<unsigned long>(transport.ncm_wake_requests),static_cast<unsigned long>(transport.tcp_accepts),
          static_cast<unsigned long>(transport.tcp_rx_callbacks),
          static_cast<unsigned long long>(transport.tcp_rx_bytes),
          static_cast<unsigned long>(transport.tcp_sent_callbacks),
          static_cast<unsigned long long>(transport.tcp_sent_bytes),
          static_cast<unsigned long>(transport.tcp_errors));
      else if (has_backend && channel_.backend_kind() == BackendKind::uart)
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u,\"uart_rx_pending\":%u,"
          "\"uart_rx_discarded\":%llu,"
          "\"uart_rx_overrun_events\":%lu,\"uart_rx_lost_bytes_minimum\":%llu,"
          "\"uart_tx_throttles\":%lu}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()),
          unsigned(backend.rx_pending),static_cast<unsigned long long>(backend.rx_discarded),
          static_cast<unsigned long>(backend.rx_overrun_events),
          static_cast<unsigned long long>(backend.rx_lost_bytes_minimum),
          static_cast<unsigned long>(backend.tx_throttle_events));
      else if (has_backend)
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u,\"application_rx_pending\":%u,"
          "\"application_tx_pending\":%u,\"application_rx_peak\":%u,\"application_tx_peak\":%u,"
          "\"application_rx_discarded\":%llu,\"application_tx_discarded\":%llu}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()),
          unsigned(backend.rx_pending),unsigned(backend.tx_pending),unsigned(backend.rx_peak),unsigned(backend.tx_peak),
          static_cast<unsigned long long>(backend.rx_discarded),static_cast<unsigned long long>(backend.tx_discarded));
      else if (has_transport)
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u,\"ncm_worker_runs\":%lu,"
          "\"ncm_rx_frames\":%lu,\"ncm_rx_deferred\":%lu,\"ncm_rx_batch_peak\":%lu,"
          "\"ncm_mutex_contentions\":%lu,"
          "\"ncm_budget_exhaustions\":%lu,\"ncm_wake_requests\":%lu,\"tcp_accepts\":%lu,"
          "\"tcp_rx_callbacks\":%lu,"
          "\"tcp_rx_bytes\":%llu,\"tcp_sent_callbacks\":%lu,\"tcp_sent_bytes\":%llu,"
          "\"tcp_errors\":%lu}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()),
          static_cast<unsigned long>(transport.ncm_worker_runs),static_cast<unsigned long>(transport.ncm_rx_frames),
          static_cast<unsigned long>(transport.ncm_rx_deferred),
          static_cast<unsigned long>(transport.ncm_rx_batch_peak),
          static_cast<unsigned long>(transport.ncm_mutex_contentions),
          static_cast<unsigned long>(transport.ncm_budget_exhaustions),
          static_cast<unsigned long>(transport.ncm_wake_requests),static_cast<unsigned long>(transport.tcp_accepts),
          static_cast<unsigned long>(transport.tcp_rx_callbacks),
          static_cast<unsigned long long>(transport.tcp_rx_bytes),
          static_cast<unsigned long>(transport.tcp_sent_callbacks),
          static_cast<unsigned long long>(transport.tcp_sent_bytes),
          static_cast<unsigned long>(transport.tcp_errors));
      else
        n = std::snprintf(body,sizeof(body),
          "{\"ok\":true,\"result\":{\"samples\":%lu,\"heap_free\":%lu,\"heap_min\":%lu,"
          "\"stack_free\":%lu,\"stack_min\":%lu,\"lwip_free\":null,"
          "\"peak_to_backend\":%u,\"peak_to_peer\":%u}}",
          static_cast<unsigned long>(memory_->count()),static_cast<unsigned long>(memory_->heap_free()),
          static_cast<unsigned long>(memory_->heap_min()),static_cast<unsigned long>(memory_->stack_free()),
          static_cast<unsigned long>(memory_->stack_min()),unsigned(channel_.peak_rx()),unsigned(channel_.peak_tx()));
    }
  } else if (r.command == Command::unknown || r.command == Command::reset) error(id,"unsupported");
  else if (r.channel != 1) error(id,"wrong_target");
  else if (r.command == Command::open) {
    if (owned_) error(id,"invalid_state");
    else if (channel_.backend_kind() == BackendKind::uart &&
             r.config_nonempty && !r.config_complete) error(id,"invalid_argument");
    else {
      const auto acquired = channel_.acquire(
          owner_,now,r.config_nonempty ? &r.configuration : nullptr);
      if (acquired == StreamResult::busy) error(id,"busy");
      else if (acquired == StreamResult::unsupported) error(id,"unsupported");
      else if (acquired == StreamResult::invalid_argument) error(id,"invalid_argument");
      else if (acquired != StreamResult::ok) error(id,"io_error");
      else { owned_ = true; success(id); }
    }
  } else if (!owned_) error(id,"invalid_state");
  else if (r.command == Command::close) { channel_.release(owner_); owned_ = false; success(id); }
  else if (r.command == Command::configure) {
    const auto configured = channel_.backend_kind() == BackendKind::uart && !r.config_complete
        ? BackendResult::invalid_argument : channel_.configure(owner_,r.configuration);
    if (configured != BackendResult::ok) error(id,backend_error(configured));
    else {
      SerialConfiguration configuration;
      if (!channel_.configuration(configuration)) { finish(SessionResult::io_error); return; }
      n = std::snprintf(body,sizeof(body),
        "{\"ok\":true,\"result\":{\"baud\":%lu,\"actual_baud\":%lu,\"data_bits\":%u,"
        "\"parity\":\"%s\",\"stop_bits\":%u,\"flow_control\":\"none\"}}",
        static_cast<unsigned long>(configuration.baud),
        static_cast<unsigned long>(configuration.actual_baud),unsigned(configuration.data_bits),
        parity_name(configuration.parity),unsigned(configuration.stop_bits));
    }
  }
  else if (r.command == Command::control) error(id,"unsupported");
  else if (r.command == Command::status) {
    n = std::snprintf(body,sizeof(body),"{\"ok\":true,\"result\":{\"channel_id\":1,\"backend_rx_bytes\":%llu,"
      "\"backend_tx_bytes\":%llu,\"pending_to_backend\":%u,\"pending_to_peer\":%u}}",
      static_cast<unsigned long long>(channel_.rx_bytes()),static_cast<unsigned long long>(channel_.tx_bytes()),
      unsigned(channel_.pending_rx()),unsigned(channel_.pending_tx()));
  }
  if (n) {
    if (n < 0 || std::size_t(n) >= sizeof(body)) finish(SessionResult::io_error);
    else if (std::size_t(n) > decoder_.limit()) error(id,"response_too_large");
    else queue(Type::response,id,reinterpret_cast<const uint8_t*>(body),std::size_t(n));
  }
  decoder_.reset(); data_turn_ = true;
}
std::size_t Session::read(uint8_t* data, std::size_t capacity) {
  if (decoder_.state() != Decode::ready || decoder_.type() != Type::serial) return 0;
  const auto count = std::min<std::size_t>(capacity,decoder_.size()-4-data_offset_);
  std::memcpy(data,decoder_.payload()+4+data_offset_,count); data_offset_ += count;
  if (data_offset_ == decoder_.size()-4) { decoder_.reset(); data_offset_ = 0; }
  return count;
}
std::size_t Session::write(const uint8_t* data, std::size_t size) {
  if (output_size_) return 0;
  const auto count = std::min<std::size_t>(size,Stream::capacity);
  uint8_t payload[4+Stream::capacity]; put32(payload,1);
  if (count) { std::memcpy(payload+4,data,count); queue(Type::serial,0,payload,4+count); }
  return result_ == SessionResult::running ? count : 0;
}
SessionResult Session::pump(uint32_t now) {
  const auto rx = channel_.rx_bytes(), tx = channel_.tx_bytes();
  const auto pending_rx = channel_.pending_rx(), pending_tx = channel_.pending_tx(), output = output_size_;
  if (channel_.poll(owner_,*this,now) != StreamResult::ok) return finish(SessionResult::io_error);
  if (rx != channel_.rx_bytes() || tx != channel_.tx_bytes() || pending_rx != channel_.pending_rx() ||
      pending_tx != channel_.pending_tx() || output != output_size_) progress_at_ = now;
  return result_;
}
SessionResult Session::poll(ByteIO& transport, uint32_t now) {
  if (result_ != SessionResult::running) return result_;
  if (!transport.connected()) return finish(SessionResult::disconnected);
  if (closing_) {
    if (!output_size_ || now-closing_at_ >= error_flush_ms) return finish(SessionResult::protocol_error);
  } else if ((!negotiated_ && now-started_at_ >= handshake_ms) || (negotiated_ && now-progress_at_ >= idle_ms) ||
             (decoder_.started() && decoder_.state() == Decode::incomplete && now-frame_at_ >= frame_ms)) return finish(SessionResult::timeout);
  if (output_size_) {
    const auto count = transport.write(output_+output_offset_,output_size_);
    if (count > output_size_) return finish(SessionResult::io_error);
    output_offset_ += count; output_size_ -= count;
    if (count) progress_at_ = now;
  }
  if (closing_) return output_size_ ? SessionResult::running : finish(SessionResult::protocol_error);
  if (decoder_.state() == Decode::incomplete) {
    uint8_t bytes[256]; const auto capacity = std::min(decoder_.needed(),sizeof(bytes));
    const auto count = transport.read(bytes,capacity);
    if (count > capacity) return finish(SessionResult::io_error);
    if (count) {
      if (!decoder_.started()) frame_at_ = now;
      decoder_.feed(bytes,count); progress_at_ = now;
    }
  }
  const auto state = decoder_.state();
  if (state != Decode::incomplete && state != Decode::ready) return finish(SessionResult::protocol_error);
  bool pumped = false;
  if (state == Decode::ready) {
    if (decoder_.type() == Type::serial) dispatch(now);
    else {
      // Alternate command replies and backend output under continuous traffic.
      if (!output_size_ && owned_ && data_turn_) {
        if (pump(now) != SessionResult::running) return result_;
        pumped = true; data_turn_ = false;
      }
      if (!output_size_) dispatch(now);
    }
  }
  if (result_ == SessionResult::running && owned_ && !pumped) pump(now);
  return result_;
}
}  // namespace firmingo
