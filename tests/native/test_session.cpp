#include "core/session.h"
#include "core/json.h"
#include "core/loopback.h"
#include "core/application.h"
#include "unity.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace firmingo;
using namespace firmingo::protocol;
void setUp() {}
void tearDown() {}
static const Identity identity{"a1b2c3d4e5f60718","0123456789abcdef","nano_rp2040_connect","session-dev-v1"};
struct IO : StreamBackend {
  std::vector<uint8_t> input, output;
  std::size_t offset = 0, read_limit = 65536, write_limit = 65536;
  bool online = true, bad_read = false, bad_write = false;
  unsigned discards = 0, reads = 0, writes = 0;
  bool connected() const override { return online; }
  std::size_t read(uint8_t* data, std::size_t size) override {
    ++reads; if (bad_read) return size+1;
    const auto n = std::min({size,read_limit,input.size()-offset});
    if (n) std::memcpy(data,input.data()+offset,n); offset += n; return n;
  }
  std::size_t write(const uint8_t* data, std::size_t size) override {
    ++writes; if (bad_write) return size+1;
    const auto n = std::min(size,write_limit); output.insert(output.end(),data,data+n); return n;
  }
  void discard() override { ++discards; input.clear(); output.clear(); offset = 0; }
  void frame(Type type, uint32_t id, const std::vector<uint8_t>& payload) {
    std::array<uint8_t,max_frame> bytes;
    const auto n = encode(bytes.data(),bytes.size(),type,id,payload.data(),payload.size());
    TEST_ASSERT_NOT_EQUAL(0,n); input.insert(input.end(),bytes.begin(),bytes.begin()+n);
  }
  void command(uint32_t id, const std::string& body) { frame(Type::request,id,{body.begin(),body.end()}); }
  void serial(const std::vector<uint8_t>& data, uint32_t channel = 1) {
    std::vector<uint8_t> payload(4); put32(payload.data(),channel); payload.insert(payload.end(),data.begin(),data.end());
    frame(Type::serial,0,payload);
  }
};
struct Frame { Type type; uint32_t id; std::vector<uint8_t> payload; };
static std::vector<Frame> frames(const std::vector<uint8_t>& bytes) {
  std::vector<Frame> result; Decoder d; std::size_t p = 0;
  while (p < bytes.size()) {
    const auto n = d.feed(bytes.data()+p,bytes.size()-p); TEST_ASSERT_NOT_EQUAL(0,n); p += n;
    if (d.state() == Decode::ready) { result.push_back({d.type(),d.request(),{d.payload(),d.payload()+d.size()}}); d.reset(); }
    else TEST_ASSERT_EQUAL_INT(int(Decode::incomplete),int(d.state()));
  }
  TEST_ASSERT_FALSE(d.started()); return result;
}
static std::string body(const Frame& f) { return {f.payload.begin(),f.payload.end()}; }
static void response(const Frame& f, uint32_t id, const char* code = nullptr) {
  TEST_ASSERT_EQUAL_INT(int(Type::response),int(f.type)); TEST_ASSERT_EQUAL_UINT32(id,f.id);
  json::Document d; TEST_ASSERT_TRUE(d.parse(f.payload.data(),f.payload.size()));
  const int ok = d.find("ok"); TEST_ASSERT_GREATER_OR_EQUAL_INT(0,ok); bool value;
  TEST_ASSERT_TRUE(d.boolean(unsigned(ok),value)); TEST_ASSERT_EQUAL_INT(code == nullptr,value);
  if (code) { const auto expected = std::string("\"code\":\"")+code+"\""; TEST_ASSERT_NOT_EQUAL(std::string::npos,body(f).find(expected)); }
}
static void poll(Session& s, IO& io, unsigned count = 100, uint32_t now = 0) {
  for (unsigned i = 0; i < count; ++i) TEST_ASSERT_EQUAL_INT(int(SessionResult::running),int(s.poll(io,now)));
}
static void hello(Session& s, IO& io, const char* request = "{\"op\":\"hello\"}") {
  io.command(1,request); poll(s,io,1000); TEST_ASSERT_TRUE(s.negotiated()); TEST_ASSERT_FALSE(s.owns_channel());
}
static void open(Session& s, IO& io) {
  hello(s,io); io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}"); poll(s,io,1000); TEST_ASSERT_TRUE(s.owns_channel());
}
static bool parse(const std::string& text, json::Document& d) { return d.parse(reinterpret_cast<const uint8_t*>(text.data()),text.size()); }
static bool parse(const char* text, json::Document& d) { return d.parse(reinterpret_cast<const uint8_t*>(text),std::strlen(text)); }
void strict_json_checks_syntax_unicode_numbers_and_duplicates() {
  json::Document d;
  const char* valid[]={"{}","[]","true","null","-0","1.25e-3","{\"a\":1,\"b\":[true,false,null,{},[]]}",
    "{\"x\":\"\\uD83D\\uDE00\"}","{\"x\":\"\xc3\xa9\"}","{\"a\":{},\"b\":{\"a\":1}}"};
  for (auto s:valid) TEST_ASSERT_TRUE_MESSAGE(parse(s,d),s);
  const char* invalid[]={"","{","[1,]","{\"a\":1,}","{\"a\" 1}","{a:1}","{\"a\":1 \"b\":2}",
    "{}{}","true false","01","-",".1","1.","1e+","+1","NaN","{\"op\":1,\"op\":2}",
    "{\"op\":1,\"\\u006fp\":2}","{\"nested\":{\"x\":1,\"x\":2}}","\"\\u0000\"",
    "\"\\uD800\"","\"\\uDC00\"","\"\\uD800\\u0041\"","\"\\x00\"","\"\xc0\x80\"",
    "\"\xed\xa0\x80\"","\"\xf4\x90\x80\x80\"","\"\x80\"","\"\xc2\"","\"\n\""};
  for (auto s:invalid) TEST_ASSERT_FALSE_MESSAGE(parse(s,d),s);
  TEST_ASSERT_TRUE(parse("{\"n\":4294967295,\"b\":false,\"s\":\"\\uD83D\\uDE00\"}",d));
  uint32_t n; bool b; char s[65];
  TEST_ASSERT_TRUE(d.uint32(unsigned(d.find("n")),n)); TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,n);
  TEST_ASSERT_TRUE(d.boolean(unsigned(d.find("b")),b)); TEST_ASSERT_FALSE(b);
  TEST_ASSERT_TRUE(d.string(unsigned(d.find("s")),s,sizeof(s))); TEST_ASSERT_EQUAL_STRING("\xf0\x9f\x98\x80",s);
  for (auto number:{"4294967296","-1","1.0","1e0"}) { TEST_ASSERT_TRUE(parse(number,d)); TEST_ASSERT_FALSE(d.uint32(0,n)); }
}
void json_resource_limits_are_explicit_and_reset_after_failure() {
  json::Document d;
  TEST_ASSERT_TRUE(parse("\""+std::string(64,'a')+"\"",d)); TEST_ASSERT_FALSE(parse("\""+std::string(65,'a')+"\"",d));
  TEST_ASSERT_TRUE(parse(std::string(8,'[')+"0"+std::string(8,']'),d));
  TEST_ASSERT_FALSE(parse(std::string(9,'[')+"0"+std::string(9,']'),d));
  TEST_ASSERT_TRUE(parse(std::string(8,'[')+std::string(8,']'),d));
  TEST_ASSERT_FALSE(parse(std::string(9,'[')+std::string(9,']'),d));
  std::string tokens="[0"; for (unsigned i=1;i<95;++i) tokens+=",0"; tokens+="]";
  TEST_ASSERT_TRUE(parse(tokens,d)); tokens.insert(tokens.size()-1,",0"); TEST_ASSERT_FALSE(parse(tokens,d));
  TEST_ASSERT_FALSE(parse(std::string(4097,' '),d)); TEST_ASSERT_TRUE(parse("{\"op\":\"hello\"}",d));
}
void hello_reports_stable_identity_capabilities_and_negotiated_limit() {
  IO io; Loopback backend; Channel channel(backend); Session s(channel,identity,1,0);
  hello(s,io,"{\"op\":\"hello\",\"max_payload\":512,\"sdk_version\":\"test-1\"}");
  auto out=frames(io.output); TEST_ASSERT_EQUAL_UINT(1,out.size()); response(out[0],1);
  TEST_ASSERT_EQUAL_UINT(512,s.limit()); TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  for (auto field:{"\"device_id\":\"a1b2c3d4e5f60718\"","\"boot_id\":\"0123456789abcdef\"",
      "\"auth_mode\":\"open-development\"","\"controls\":[]","\"targets\":[]","\"backend\":\"application\""})
    TEST_ASSERT_NOT_EQUAL(std::string::npos,body(out[0]).find(field));
  io.command(2,"{\"op\":\"hello\"}"); poll(s,io); response(frames(io.output)[1],2,"invalid_state");
}
void uart_capabilities_open_configuration_and_reconfiguration_are_explicit() {
  struct UART : IO {
    SerialConfiguration value{115200,115193,8,1,SerialParity::none};
    BackendKind kind() const override { return BackendKind::uart; }
    bool configuration(SerialConfiguration& output) const override { output=value; return true; }
    BackendResult configure(const SerialConfiguration& requested) override {
      if (requested.baud < 300 || requested.baud > 2000000 ||
          requested.data_bits < 5 || requested.data_bits > 8 ||
          (requested.stop_bits != 1 && requested.stop_bits != 2))
        return BackendResult::invalid_argument;
      value=requested; value.actual_baud=requested.baud-7; return BackendResult::ok;
    }
  } backend;
  IO io; Channel channel(backend); Session session(channel,identity,1,0); hello(session,io);
  auto output=frames(io.output); const auto hello_body=body(output.back());
  for (auto field:{"\"backend\":\"uart\"","\"controls\":[]","\"baud\":115200",
                   "\"actual_baud\":115193","\"data_bits\":8","\"parity\":\"none\"",
                   "\"stop_bits\":1","\"flow_control\":\"none\"",
                   "\"baud_min\":300","\"baud_max\":2000000"})
    TEST_ASSERT_NOT_EQUAL(std::string::npos,hello_body.find(field));
  TEST_ASSERT_LESS_OR_EQUAL_UINT(Session::response_capacity,output.back().payload.size());
  io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1,\"config\":{\"baud\":57600,\"data_bits\":7,\"parity\":\"even\",\"stop_bits\":2,\"flow_control\":\"none\"}}");
  poll(session,io); response(frames(io.output).back(),2); TEST_ASSERT_TRUE(session.owns_channel());
  TEST_ASSERT_EQUAL_UINT32(57600,backend.value.baud);
  TEST_ASSERT_EQUAL_UINT8(7,backend.value.data_bits);
  TEST_ASSERT_EQUAL_INT(int(SerialParity::even),int(backend.value.parity));
  io.command(3,"{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":38400,\"data_bits\":8,\"parity\":\"odd\",\"stop_bits\":1}}");
  poll(session,io); output=frames(io.output); response(output.back(),3);
  const auto configured=body(output.back());
  for (auto field:{"\"baud\":38400","\"actual_baud\":38393","\"data_bits\":8",
                   "\"parity\":\"odd\"","\"stop_bits\":1"})
    TEST_ASSERT_NOT_EQUAL(std::string::npos,configured.find(field));
  const char* invalid[]={
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":299,\"data_bits\":8,\"parity\":\"none\",\"stop_bits\":1}}",
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":9600,\"data_bits\":8,\"parity\":\"mark\",\"stop_bits\":1}}",
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":9600,\"data_bits\":8,\"parity\":\"none\",\"stop_bits\":1,\"flow_control\":\"rts_cts\"}}",
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":9600,\"data_bits\":8,\"stop_bits\":1}}",
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{}}",
    "{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":9600,\"extra\":1}}"};
  uint32_t id=4;
  for (auto command:invalid) {
    io.command(id,command); poll(session,io); response(frames(io.output).back(),id++,"invalid_argument");
  }
}
void uart_diagnostics_use_uart_names_report_loss_and_fit_minimum_payload() {
  static_assert(Session::response_capacity >= 776,
                "maximum UART and transport diagnostics must fit");
  struct UART : IO {
    BackendKind kind() const override { return BackendKind::uart; }
    bool configuration(SerialConfiguration& value) const override {
      value=SerialConfiguration(115200,115193,8,1,SerialParity::none); return true;
    }
    bool diagnostics(BackendDiagnostics& value) const override {
      value.rx_pending=value.tx_pending=value.rx_peak=value.tx_peak=256;
      value.rx_discarded=value.tx_discarded=UINT64_MAX;
      value.rx_overrun_events=UINT32_MAX;
      value.rx_lost_bytes_minimum=UINT64_MAX;
      value.tx_throttle_events=UINT32_MAX;
      return true;
    }
  } backend;
  TransportDiagnostics transport;
  transport.ncm_worker_runs=transport.ncm_rx_frames=transport.ncm_rx_deferred=UINT32_MAX;
  transport.ncm_rx_batch_peak=transport.ncm_mutex_contentions=UINT32_MAX;
  transport.ncm_budget_exhaustions=transport.ncm_wake_requests=UINT32_MAX;
  transport.tcp_accepts=transport.tcp_rx_callbacks=transport.tcp_sent_callbacks=UINT32_MAX;
  transport.tcp_errors=UINT32_MAX; transport.tcp_rx_bytes=transport.tcp_sent_bytes=UINT64_MAX;
  MemorySamples memory; memory.observe(UINT32_MAX,UINT32_MAX); memory.observe_transport(transport);
  IO io; Channel channel(backend); Session session(channel,identity,1,0,&memory); hello(session,io);
  io.command(2,"{\"op\":\"device.diagnostics\"}"); poll(session,io);
  const auto result=frames(io.output).back(); response(result,2);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(Session::response_capacity,result.payload.size());
  for (auto field:{"\"uart_rx_pending\":256","\"uart_rx_discarded\":18446744073709551615",
                   "\"uart_rx_overrun_events\":4294967295",
                   "\"uart_rx_lost_bytes_minimum\":18446744073709551615",
                   "\"uart_tx_throttles\":4294967295"})
    TEST_ASSERT_NOT_EQUAL(std::string::npos,body(result).find(field));
  TEST_ASSERT_EQUAL(std::string::npos,body(result).find("application_rx_pending"));
  IO limited_io; Session limited(channel,identity,2,0,&memory);
  hello(limited,limited_io,"{\"op\":\"hello\",\"max_payload\":512}");
  limited_io.command(2,"{\"op\":\"device.diagnostics\"}"); poll(limited,limited_io);
  response(frames(limited_io.output).back(),2,"response_too_large");
}
void all_control_frame_splits_keep_exactly_one_correlated_response() {
  const char* commands[]={"{\"op\":\"hello\"}","{\"op\":\"serial.open\",\"channel_id\":1}",
    "{\"op\":\"device.reset\",\"scope\":\"device\"}"};
  for (unsigned command=0;command<3;++command) {
    IO encoded; encoded.command(7,commands[command]);
    for (std::size_t split=0;split<=encoded.input.size();++split) {
      IO io; Loopback backend; Channel channel(backend); Session s(channel,identity,1,0);
      if (command) hello(s,io);
      io.input.clear(); io.offset=0; io.output.clear();
      io.input.insert(io.input.end(),encoded.input.begin(),encoded.input.begin()+split); poll(s,io,20,1);
      io.input.insert(io.input.end(),encoded.input.begin()+split,encoded.input.end()); poll(s,io,200,2);
      auto out=frames(io.output); TEST_ASSERT_EQUAL_UINT(1,out.size()); response(out[0],7,command==2?"unsupported":nullptr);
    }
  }
}
void coalesced_commands_and_all_partial_writes_preserve_binary_data() {
  for (std::size_t chunk=1;chunk<=256;++chunk) {
    IO io; Loopback backend; Channel channel(backend); Session s(channel,identity,1,0);
    io.read_limit=chunk; io.write_limit=257-chunk; open(s,io);
    std::vector<uint8_t> data(2048); for(unsigned i=0;i<data.size();++i)data[i]=uint8_t(i);
    io.serial(data); poll(s,io,6000);
    auto out=frames(io.output); response(out[0],1); response(out[1],2);
    std::vector<uint8_t> received;
    for (std::size_t i=2;i<out.size();++i) {
      TEST_ASSERT_EQUAL_INT(int(Type::serial),int(out[i].type)); TEST_ASSERT_EQUAL_UINT32(0,out[i].id);
      TEST_ASSERT_EQUAL_UINT32(1,get32(out[i].payload.data()));
      received.insert(received.end(),out[i].payload.begin()+4,out[i].payload.end());
    }
    TEST_ASSERT_EQUAL_UINT(data.size(),received.size()); TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(),received.data(),data.size());
    TEST_ASSERT_EQUAL_UINT64(data.size(),channel.rx_bytes()); TEST_ASSERT_EQUAL_UINT64(data.size(),channel.tx_bytes());
  }
}
void busy_connection_cannot_steal_or_discard_owner_queues() {
  IO backend,aio,bio; Channel channel(backend); Session a(channel,identity,1,0), b(channel,identity,2,0);
  open(a,aio); const auto discards=backend.discards;
  hello(b,bio); bio.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}"); poll(b,bio);
  response(frames(bio.output)[1],2,"busy"); TEST_ASSERT_EQUAL_UINT32(1,channel.owner());
  TEST_ASSERT_EQUAL_UINT(discards,backend.discards); b.close(); TEST_ASSERT_EQUAL_UINT32(1,channel.owner());
  aio.command(3,"{\"op\":\"serial.close\",\"channel_id\":1}"); poll(a,aio); TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  IO next; Session c(channel,identity,3,0); open(c,next); TEST_ASSERT_EQUAL_UINT32(3,channel.owner());
}
void request_schemas_reject_wrong_fields_types_and_parameters_without_actions() {
  IO io,backend; Channel channel(backend); Session s(channel,identity,1,0); open(s,io);
  const char* invalid[]={"{}","[]","{\"op\":true}","{\"op\":\"serial.open\",\"channel_id\":-1}",
    "{\"op\":\"serial.open\",\"channel_id\":1,\"config\":true}","{\"op\":\"serial.status\",\"channel_id\":1.0}",
    "{\"op\":\"serial.status\",\"channel_id\":4294967296}","{\"op\":\"serial.status\",\"channel_id\":1,\"extra\":1}",
    "{\"op\":\"serial.configure\",\"channel_id\":1}","{\"op\":\"serial.control\",\"channel_id\":1,\"control\":\"break\",\"value\":1}",
    "{\"op\":\"device.reset\"}","{\"op\":\"device.reset\",\"scope\":\"wrong\"}"};
  unsigned id=3;
  for(auto command:invalid) { io.command(id,command); poll(s,io); response(frames(io.output).back(),id++,"invalid_argument"); }
  const char* unsupported[]={"{\"op\":\"serial.configure\",\"channel_id\":1,\"config\":{\"baud\":115200}}",
    "{\"op\":\"serial.control\",\"channel_id\":1,\"control\":\"dtr\",\"value\":true}",
    "{\"op\":\"device.reset\",\"scope\":\"device\"}","{\"op\":\"flash.commit\",\"upload_id\":1}"};
  for(auto command:unsupported) { io.command(id,command); poll(s,io); response(frames(io.output).back(),id++,"unsupported"); }
  TEST_ASSERT_EQUAL_UINT(0,backend.output.size()); TEST_ASSERT_EQUAL_UINT(1,backend.discards); TEST_ASSERT_TRUE(s.owns_channel());
}
void handshake_schema_invalid_requests_can_be_corrected_without_negotiating() {
  IO io; Loopback backend; Channel channel(backend); Session s(channel,identity,1,0);
  const char* invalid[]={"{\"op\":\"hello\",\"max_payload\":511}","{\"op\":\"hello\",\"max_payload\":65537}",
    "{\"op\":\"hello\",\"max_payload\":512.0}","{\"op\":\"hello\",\"sdk_version\":1}"};
  unsigned id=1;
  for(auto c:invalid){io.command(id,c);poll(s,io);response(frames(io.output).back(),id++,"invalid_argument");TEST_ASSERT_FALSE(s.negotiated());}
  io.command(id,"{\"op\":\"serial.open\",\"channel_id\":1}");poll(s,io);response(frames(io.output).back(),id,"invalid_state");
  hello(s,io,"{\"op\":\"hello\",\"max_payload\":65536}"); TEST_ASSERT_EQUAL_UINT(4096,s.limit());
}
void malformed_json_releases_owner_and_flushes_one_error_without_retry() {
  IO io,backend; Channel channel(backend); Session s(channel,identity,1,0); open(s,io);
  io.command(3,"{\"op\":\"serial.close\",\"op\":\"device.reset\"}");
  SessionResult result=SessionResult::running;
  for(unsigned i=0;i<20 && result==SessionResult::running;++i)result=s.poll(io,1);
  TEST_ASSERT_EQUAL_INT(int(SessionResult::protocol_error),int(result)); TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  auto out=frames(io.output); TEST_ASSERT_EQUAL_UINT(3,out.size()); response(out[2],3,"invalid_argument");
  TEST_ASSERT_EQUAL_UINT(2,backend.discards);
  const auto written=io.output.size();TEST_ASSERT_EQUAL_INT(int(result),int(s.poll(io,2)));TEST_ASSERT_EQUAL_UINT(written,io.output.size());
}
void invalid_serial_and_client_response_frames_close_before_side_effects() {
  for(unsigned fault=0;fault<6;++fault) {
    IO io,backend; Channel channel(backend); Session s(channel,identity,1,0);
    if(fault) open(s,io);
    if(fault==0)io.serial({1});
    if(fault==1)io.serial({1},2);
    if(fault==2)io.serial({});
    if(fault==3)io.frame(Type::serial,0,{0,0,1});
    if(fault==4)io.frame(Type::response,9,{'{','}'});
    if(fault==5)io.frame(Type::event,0,{'{','}'});
    SessionResult result=SessionResult::running;
    for(unsigned i=0;i<20 && result==SessionResult::running;++i)result=s.poll(io,1);
    TEST_ASSERT_EQUAL_INT(int(SessionResult::protocol_error),int(result)); TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
    TEST_ASSERT_EQUAL_UINT(0,backend.output.size());
  }
}
void timeout_boundaries_cover_rollover_trickle_idle_and_error_flush() {
  IO io; Loopback backend; Channel channel(backend);
  {Session s(channel,identity,1,0xfffffff0);TEST_ASSERT_EQUAL_INT(int(SessionResult::running),int(s.poll(io,4983)));TEST_ASSERT_EQUAL_INT(int(SessionResult::timeout),int(s.poll(io,4984)));}
  {Session s(channel,identity,1,0);hello(s,io);io.input.push_back('F');poll(s,io,1,1);io.input.push_back('M');poll(s,io,1,4999);TEST_ASSERT_EQUAL_INT(int(SessionResult::timeout),int(s.poll(io,5001)));}
  io=IO{};
  {Session s(channel,identity,1,0);open(s,io);TEST_ASSERT_EQUAL_INT(int(SessionResult::running),int(s.poll(io,29999)));TEST_ASSERT_EQUAL_INT(int(SessionResult::timeout),int(s.poll(io,30000)));}
  io=IO{};
  {Session s(channel,identity,1,0);io.command(1,"{");io.write_limit=0;poll(s,io,10);TEST_ASSERT_EQUAL_INT(int(SessionResult::protocol_error),int(s.poll(io,1000)));}
  TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
}
void stalled_output_is_bounded_while_input_progresses_and_then_drains() {
  IO io,backend; Channel channel(backend); Session s(channel,identity,1,0);open(s,io);
  backend.input.assign(4096,0x55);io.write_limit=0;
  for(unsigned i=0;i<10;++i)io.serial(std::vector<uint8_t>(256,0x77));poll(s,io,200);
  TEST_ASSERT_EQUAL_UINT(2560,backend.output.size());TEST_ASSERT_EQUAL_UINT(512,backend.offset);
  io.write_limit=17;poll(s,io,2000);
  auto out=frames(io.output);std::vector<uint8_t> data;
  for(std::size_t i=2;i<out.size();++i)data.insert(data.end(),out[i].payload.begin()+4,out[i].payload.end());
  TEST_ASSERT_EQUAL_UINT(4096,data.size());TEST_ASSERT_EACH_EQUAL_UINT8(0x55,data.data(),data.size());
}
void stalled_backend_bounds_input_and_allows_opposite_direction() {
  IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);open(s,io);
  const auto initial=io.offset;backend.write_limit=0;backend.input.assign(1024,0x55);
  for(unsigned i=0;i<10;++i)io.serial(std::vector<uint8_t>(256,0x77));poll(s,io,100);
  TEST_ASSERT_EQUAL_UINT(initial+2*(header_size+260),io.offset);TEST_ASSERT_EQUAL_UINT(0,backend.output.size());TEST_ASSERT_EQUAL_UINT(1024,backend.offset);
  backend.write_limit=3;poll(s,io,2000);TEST_ASSERT_EQUAL_UINT(2560,backend.output.size());TEST_ASSERT_EACH_EQUAL_UINT8(0x77,backend.output.data(),2560);
}
void disconnect_close_and_destruction_discard_queues_before_new_owner() {
  IO backend,first,second;Channel channel(backend);
  {Session a(channel,identity,1,0);open(a,first);backend.write_limit=0;first.serial(std::vector<uint8_t>(1024,0x77));poll(a,first);
   first.online=false;TEST_ASSERT_EQUAL_INT(int(SessionResult::disconnected),int(a.poll(first,1)));}
  {Session b(channel,identity,2,0);open(b,second);backend.write_limit=256;second.serial({0,13,10,255});poll(b,second);
   const uint8_t expected[]={0,13,10,255};TEST_ASSERT_EQUAL_UINT(4,backend.output.size());TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,backend.output.data(),4);}
  TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
}
void commands_and_backend_data_both_progress_under_continuous_traffic() {
  IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);open(s,io);
  backend.input.assign(4096,0x55);
  for(unsigned i=0;i<30;++i)io.command(i+3,"{\"op\":\"serial.status\",\"channel_id\":1}");poll(s,io,200);
  auto out=frames(io.output);unsigned responses=0;std::size_t data=0;
  for(auto& f:out)if(f.type==Type::response)++responses;else data+=f.payload.size()-4;
  TEST_ASSERT_EQUAL_UINT(32,responses);TEST_ASSERT_EQUAL_UINT(4096,data);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,body(out.back()).find("backend_tx_bytes"));
}
void unsupported_upload_open_config_and_wrong_target_have_clear_errors() {
  IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);hello(s,io);
  io.frame(Type::upload,2,{0});io.command(3,"{\"op\":\"serial.open\",\"channel_id\":2}");
  io.command(4,"{\"op\":\"serial.open\",\"channel_id\":1,\"config\":{\"baud\":115200}}");
  io.command(5,"{\"op\":\"serial.status\",\"channel_id\":1}");poll(s,io);
  auto out=frames(io.output);response(out[1],2,"unsupported");response(out[2],3,"wrong_target");response(out[3],4,"unsupported");response(out[4],5,"invalid_state");
  TEST_ASSERT_EQUAL_UINT(0,backend.discards);TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
}
void adapter_failures_invalid_identity_and_offline_backend_fail_safely() {
  for(unsigned fault=0;fault<4;++fault) {
    IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);
    if(fault==0)io.bad_read=true;
    else {open(s,io);io.serial({1});if(fault==1)backend.bad_read=true;if(fault==2)backend.bad_write=true;if(fault==3){io.command(3,"{\"op\":\"serial.status\",\"channel_id\":1}");io.bad_write=true;}}
    SessionResult result=SessionResult::running;for(unsigned i=0;i<20 && result==SessionResult::running;++i)result=s.poll(io,1);
    TEST_ASSERT_EQUAL_INT(int(SessionResult::io_error),int(result));TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  }
  IO io,backend;Channel channel(backend);Identity bad=identity;bad.device_id[0]='"';Session invalid(channel,bad,1,0);
  TEST_ASSERT_EQUAL_INT(int(SessionResult::io_error),int(invalid.poll(io,0)));
  Session zero(channel,identity,0,0);TEST_ASSERT_EQUAL_INT(int(SessionResult::io_error),int(zero.poll(io,0)));
  Session offline(channel,identity,1,0);hello(offline,io);backend.online=false;io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}");poll(offline,io);
  response(frames(io.output).back(),2,"io_error");TEST_ASSERT_EQUAL_UINT(0,backend.discards);
}
static std::vector<uint8_t> fixture(const char* name) {
  const auto path=std::string(FIXTURE_DIR)+"/"+name;FILE* f=std::fopen(path.c_str(),"r");TEST_ASSERT_NOT_NULL(f);
  std::vector<uint8_t> bytes;unsigned value;
  while(std::fscanf(f,"%2x",&value)==1)bytes.push_back(uint8_t(value));
  const bool ended=std::feof(f);std::fclose(f);TEST_ASSERT_TRUE(ended);return bytes;
}
void exact_hello_and_unsupported_reset_fixtures_match_production_output() {
  IO io;Loopback backend;Channel channel(backend);Session s(channel,identity,1,0);
  io.input=fixture("hello-request.hex");poll(s,io);auto expected=fixture("hello-response.hex");
  TEST_ASSERT_EQUAL_UINT(expected.size(),io.output.size());TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(),io.output.data(),expected.size());
  io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}");poll(s,io);io.output.clear();
  auto reset=fixture("reset-request.hex");io.input.insert(io.input.end(),reset.begin(),reset.end());poll(s,io);
  expected=fixture("reset-unsupported.hex");TEST_ASSERT_EQUAL_UINT(expected.size(),io.output.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(),io.output.data(),expected.size());TEST_ASSERT_TRUE(s.owns_channel());
}
void maximum_binary_frame_and_negotiated_overflow_have_correct_boundaries() {
  {IO io;Loopback backend;Channel channel(backend);Session s(channel,identity,1,0);open(s,io);
   std::vector<uint8_t> data(max_payload-4);for(unsigned i=0;i<data.size();++i)data[i]=uint8_t(i);
   io.serial(data);poll(s,io,1000);std::vector<uint8_t> received;
   auto out=frames(io.output);for(std::size_t i=2;i<out.size();++i)received.insert(received.end(),out[i].payload.begin()+4,out[i].payload.end());
   TEST_ASSERT_EQUAL_UINT(data.size(),received.size());TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(),received.data(),data.size());}
  {IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);hello(s,io,"{\"op\":\"hello\",\"max_payload\":512}");
   io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}");poll(s,io);const auto initial=io.offset;
   io.serial(std::vector<uint8_t>(509,0x55));TEST_ASSERT_EQUAL_INT(int(SessionResult::protocol_error),int(s.poll(io,1)));
   TEST_ASSERT_EQUAL_UINT(initial+header_size,io.offset);TEST_ASSERT_EQUAL_UINT32(0,channel.owner());TEST_ASSERT_EQUAL_UINT(0,backend.output.size());}
}
void close_disposes_pending_data_and_status_is_not_a_flush_acknowledgement() {
  IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);open(s,io);backend.write_limit=0;
  io.serial(std::vector<uint8_t>(256,0x77));io.command(3,"{\"op\":\"serial.status\",\"channel_id\":1}");poll(s,io);
  auto out=frames(io.output);response(out.back(),3);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,body(out.back()).find("\"pending_to_backend\":256"));TEST_ASSERT_EQUAL_UINT(0,backend.output.size());
  io.command(4,"{\"op\":\"serial.close\",\"channel_id\":1}");poll(s,io);response(frames(io.output).back(),4);TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
  io.command(5,"{\"op\":\"serial.open\",\"channel_id\":1,\"config\":{}}");poll(s,io);backend.write_limit=256;io.serial({1,2});poll(s,io);
  TEST_ASSERT_EQUAL_UINT(2,backend.output.size());TEST_ASSERT_EQUAL_UINT64(2,channel.rx_bytes());
}
void poll_has_bounded_calls_and_bad_header_does_not_splice_partial_output() {
  IO io,backend;Channel channel(backend);Session s(channel,identity,1,0);open(s,io);
  backend.input.assign(4096,0x55);io.serial(std::vector<uint8_t>(4092,0x77));
  for(unsigned i=0;i<100;++i){const auto r=io.reads,w=io.writes,br=backend.reads,bw=backend.writes;
    poll(s,io,1);TEST_ASSERT_LESS_OR_EQUAL_UINT(1,io.reads-r);TEST_ASSERT_LESS_OR_EQUAL_UINT(1,io.writes-w);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(1,backend.reads-br);TEST_ASSERT_LESS_OR_EQUAL_UINT(1,backend.writes-bw);}
  io.write_limit=1;backend.input.insert(backend.input.end(),256,0x55);poll(s,io,1);
  IO malformed;malformed.command(9,"{}");malformed.input[0]=0;
  io.input.insert(io.input.end(),malformed.input.begin(),malformed.input.end());const auto before=io.output.size();
  TEST_ASSERT_EQUAL_INT(int(SessionResult::protocol_error),int(s.poll(io,1)));TEST_ASSERT_LESS_OR_EQUAL_UINT(before+1,io.output.size());
  TEST_ASSERT_EQUAL_UINT32(0,channel.owner());
}

void diagnostics_are_bounded_correlated_and_keep_sampled_minima() {
  IO io; Loopback backend; Channel channel(backend); MemorySamples memory;
  memory.observe(UINT32_MAX,UINT32_MAX); memory.observe(1234,567); memory.observe(2345,678);
  TEST_ASSERT_EQUAL_UINT32(3,memory.count()); TEST_ASSERT_EQUAL_UINT32(1234,memory.heap_min());
  Session session(channel,identity,1,0,&memory);
  hello(session,io,"{\"op\":\"hello\",\"max_payload\":512}");
  io.command(9,"{\"op\":\"device.diagnostics\"}"); poll(session,io);
  const auto result=frames(io.output).back(); response(result,9);
  const auto expected=fixture("diagnostics-response.hex");
  IO request; request.command(9,"{\"op\":\"device.diagnostics\"}");
  const auto request_fixture=fixture("diagnostics-request.hex");
  TEST_ASSERT_EQUAL_UINT(request_fixture.size(),request.input.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(request_fixture.data(),request.input.data(),request_fixture.size());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(expected.size(),io.output.size());
  const auto encoded=io.output.end()-static_cast<std::ptrdiff_t>(expected.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(),&*encoded,expected.size());
  TEST_ASSERT_FALSE(session.owns_channel());
  MemorySamples worst; worst.observe(UINT32_MAX,UINT32_MAX);
  IO other; Session maximum(channel,identity,2,0,&worst); hello(maximum,other);
  other.command(10,"{\"op\":\"device.diagnostics\"}"); poll(maximum,other);
  const auto out=frames(other.output).back(); response(out,10);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(512,out.payload.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,body(out).find("4294967295"));
  struct MaximumBackend : IO {
    bool diagnostics(BackendDiagnostics& d) const override {
      d.rx_pending=d.tx_pending=d.rx_peak=d.tx_peak=256;
      d.rx_discarded=d.tx_discarded=UINT64_MAX;
      return true;
    }
  } maximum_backend;
  Channel maximum_channel(maximum_backend); IO maximum_io;
  Session maximum_session(maximum_channel,identity,3,0,&worst); hello(maximum_session,maximum_io);
  maximum_io.command(11,"{\"op\":\"device.diagnostics\"}"); poll(maximum_session,maximum_io);
  const auto maximum_response=frames(maximum_io.output).back(); response(maximum_response,11);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(512,maximum_response.payload.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos,body(maximum_response).find("18446744073709551615"));
}
void diagnostics_require_negotiation_samples_and_exact_schema() {
  IO io; Loopback backend; Channel channel(backend); MemorySamples memory;
  Session session(channel,identity,1,0,&memory);
  io.command(5,"{\"op\":\"device.diagnostics\"}"); poll(session,io);
  response(frames(io.output).back(),5,"invalid_state"); hello(session,io);
  io.command(6,"{\"op\":\"device.diagnostics\"}"); poll(session,io);
  response(frames(io.output).back(),6,"unsupported");
  memory.observe(10,20);
  io.command(7,"{\"op\":\"device.diagnostics\",\"channel_id\":1}"); poll(session,io);
  response(frames(io.output).back(),7,"invalid_argument");
  IO other; Session missing(channel,identity,2,0); hello(missing,other);
  other.command(8,"{\"op\":\"device.diagnostics\"}"); poll(missing,other);
  response(frames(other.output).back(),8,"unsupported");
}
void application_queue_diagnostics_are_bounded_and_additive() {
  IO io; ApplicationEndpoint backend; Channel channel(backend); MemorySamples memory;
  memory.observe(1000,2000);
  memory.observe_transport({1,2,3,4,5,6,7,8,9,10,11,12,13});
  Session session(channel,identity,1,0,&memory);
  hello(session,io); io.command(2,"{\"op\":\"serial.open\",\"channel_id\":1}"); poll(session,io);
  const uint8_t outbound[] = {1,2,3};
  TEST_ASSERT_EQUAL_UINT(3,backend.write_to_host(outbound,sizeof(outbound)));
  io.command(3,"{\"op\":\"device.diagnostics\"}"); poll(session,io);
  const auto output=frames(io.output); response(output.back(),3);
  for (auto field:{"\"application_rx_pending\":0","\"application_tx_pending\":0",
                   "\"application_rx_peak\":0","\"application_tx_peak\":3",
                   "\"application_rx_discarded\":0","\"application_tx_discarded\":0",
                   "\"ncm_worker_runs\":1","\"ncm_rx_frames\":2",
                   "\"ncm_rx_deferred\":3","\"ncm_rx_batch_peak\":4",
                   "\"ncm_mutex_contentions\":5","\"ncm_budget_exhaustions\":6",
                   "\"ncm_wake_requests\":7","\"tcp_accepts\":8",
                   "\"tcp_rx_callbacks\":9","\"tcp_sent_callbacks\":10",
                   "\"tcp_errors\":11","\"tcp_rx_bytes\":12",
                   "\"tcp_sent_bytes\":13"})
    TEST_ASSERT_NOT_EQUAL(std::string::npos,body(output.back()).find(field));
  TEST_ASSERT_LESS_OR_EQUAL_UINT(768,output.back().payload.size());
}
void stream_queue_peaks_include_drained_reads_and_survive_new_owner() {
  IO io,backend; Channel channel(backend); Session session(channel,identity,1,0); open(session,io);
  backend.write_limit=0; io.serial(std::vector<uint8_t>(256,0x77)); poll(session,io);
  TEST_ASSERT_EQUAL_UINT(256,channel.pending_rx()); TEST_ASSERT_EQUAL_UINT(256,channel.peak_rx());
  backend.input.assign(123,0x55); poll(session,io);
  TEST_ASSERT_EQUAL_UINT(0,channel.pending_tx()); TEST_ASSERT_EQUAL_UINT(123,channel.peak_tx());
  session.close(); TEST_ASSERT_EQUAL_UINT(0,channel.pending_rx());
  IO next; Session replacement(channel,identity,2,0); open(replacement,next);
  next.serial({1,2}); backend.write_limit=256; poll(replacement,next);
  TEST_ASSERT_EQUAL_UINT(256,channel.peak_rx()); TEST_ASSERT_EQUAL_UINT(123,channel.peak_tx());
  TEST_ASSERT_EQUAL_UINT64(2,channel.rx_bytes());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(diagnostics_are_bounded_correlated_and_keep_sampled_minima);
  RUN_TEST(diagnostics_require_negotiation_samples_and_exact_schema);
  RUN_TEST(application_queue_diagnostics_are_bounded_and_additive);
  RUN_TEST(stream_queue_peaks_include_drained_reads_and_survive_new_owner);
  RUN_TEST(strict_json_checks_syntax_unicode_numbers_and_duplicates);
  RUN_TEST(json_resource_limits_are_explicit_and_reset_after_failure);
  RUN_TEST(hello_reports_stable_identity_capabilities_and_negotiated_limit);
  RUN_TEST(uart_capabilities_open_configuration_and_reconfiguration_are_explicit);
  RUN_TEST(uart_diagnostics_use_uart_names_report_loss_and_fit_minimum_payload);
  RUN_TEST(all_control_frame_splits_keep_exactly_one_correlated_response);
  RUN_TEST(coalesced_commands_and_all_partial_writes_preserve_binary_data);
  RUN_TEST(busy_connection_cannot_steal_or_discard_owner_queues);
  RUN_TEST(request_schemas_reject_wrong_fields_types_and_parameters_without_actions);
  RUN_TEST(handshake_schema_invalid_requests_can_be_corrected_without_negotiating);
  RUN_TEST(malformed_json_releases_owner_and_flushes_one_error_without_retry);
  RUN_TEST(invalid_serial_and_client_response_frames_close_before_side_effects);
  RUN_TEST(timeout_boundaries_cover_rollover_trickle_idle_and_error_flush);
  RUN_TEST(stalled_output_is_bounded_while_input_progresses_and_then_drains);
  RUN_TEST(stalled_backend_bounds_input_and_allows_opposite_direction);
  RUN_TEST(disconnect_close_and_destruction_discard_queues_before_new_owner);
  RUN_TEST(commands_and_backend_data_both_progress_under_continuous_traffic);
  RUN_TEST(unsupported_upload_open_config_and_wrong_target_have_clear_errors);
  RUN_TEST(adapter_failures_invalid_identity_and_offline_backend_fail_safely);
  RUN_TEST(exact_hello_and_unsupported_reset_fixtures_match_production_output);
  RUN_TEST(maximum_binary_frame_and_negotiated_overflow_have_correct_boundaries);
  RUN_TEST(close_disposes_pending_data_and_status_is_not_a_flush_acknowledgement);
  RUN_TEST(poll_has_bounded_calls_and_bad_header_does_not_splice_partial_output);
  return UNITY_END();
}
