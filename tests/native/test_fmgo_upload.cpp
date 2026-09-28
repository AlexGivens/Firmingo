#include "core/session.h"
#include "core/application.h"
#include "core/json.h"
#include "fmgo_upload.h"
#include "unity.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
using namespace firmingo;
using namespace firmingo::protocol;
using namespace firmingo_managed;
static bool armed = true, returned = false;
static bool arm() { return armed; }
static bool ran() { return returned; }
void setUp() { armed = true; returned = false; }
void tearDown() {}
static const Identity identity{"a1b2c3d4e5f60718","0123456789abcdef","nano_rp2040_connect","0.1.0-m9exp1"};
constexpr uint32_t api = 0x1002154c;
struct Flash : ProofFlashPort {
  uint8_t bytes[kProofSectorBytes]{};
  unsigned erases = 0, programs = 0;
  bool fail = false;
  bool begin() override { return true; }
  void end() override {}
  bool erase(uint32_t offset, std::size_t size) override {
    TEST_ASSERT_EQUAL_HEX32(kSlotAddress-kFlashXipAddress,offset);
    TEST_ASSERT_EQUAL_UINT(kProofSectorBytes,size);
    ++erases; std::memset(bytes,0xff,sizeof(bytes)); return true;
  }
  bool program(uint32_t offset, const uint8_t* data, std::size_t size) override {
    ++programs;
    const auto at = offset-(kSlotAddress-kFlashXipAddress);
    TEST_ASSERT_TRUE(at+size<=sizeof(bytes));
    if (fail) return false;
    std::memcpy(bytes+at,data,size); return true;
  }
  const uint8_t* slot_data() const override { return bytes; }
};
struct Runtime : SketchRuntime {
  unsigned starts=0,stops=0;
  void stop() override { ++stops; returned=false; }
  void start(const ImageInfo&) override { ++starts; }
};
struct Fixture {
  CapsuleStage stage;
  Core1Pause pause;
  Flash flash;
  Runtime runtime;
  SketchSwitch switching{stage,pause,flash,runtime};
  FmgoUpload upload{stage,switching,api,arm,ran};
  ApplicationEndpoint backend;
  Channel channel{backend};
};
struct Peer : ByteIO {
  std::vector<uint8_t> input,output;
  std::size_t offset=0,read_limit=65536,write_limit=65536;
  bool online=true;
  bool connected() const override { return online; }
  std::size_t read(uint8_t* bytes,std::size_t capacity) override {
    auto n=std::min({capacity,read_limit,input.size()-offset});
    if(n)std::memcpy(bytes,input.data()+offset,n);
    offset+=n; return n;
  }
  std::size_t write(const uint8_t* bytes,std::size_t size) override {
    auto n=std::min(size,write_limit); output.insert(output.end(),bytes,bytes+n);return n;
  }
};
static std::vector<uint8_t> wire(Type type,uint32_t id,const std::vector<uint8_t>& body) {
  std::vector<uint8_t> bytes(max_frame);
  auto n=encode(bytes.data(),bytes.size(),type,id,body.data(),body.size());
  TEST_ASSERT_NOT_EQUAL(0,n);bytes.resize(n);return bytes;
}
static void pump(Session& s,Peer& p,unsigned count=500,uint32_t now=0) {
  for(unsigned i=0;i<count;++i) TEST_ASSERT_EQUAL_INT(int(SessionResult::running),int(s.poll(p,now)));
}
static std::string response(Peer& p,uint32_t id=9) {
  Decoder d;std::size_t at=0;
  while(at<p.output.size()) { auto n=d.feed(p.output.data()+at,p.output.size()-at);TEST_ASSERT_NOT_EQUAL(0,n);at+=n; }
  TEST_ASSERT_EQUAL_INT(int(Decode::ready),int(d.state()));
  TEST_ASSERT_EQUAL_INT(int(Type::response),int(d.type()));TEST_ASSERT_EQUAL_UINT32(id,d.request());
  json::Document doc;TEST_ASSERT_TRUE(doc.parse(d.payload(),d.size()));
  std::string text(reinterpret_cast<const char*>(d.payload()),d.size());p.output.clear();return text;
}
static std::string exchange(Session& s,Peer& p,const std::vector<uint8_t>& bytes) {
  p.input.insert(p.input.end(),bytes.begin(),bytes.end());pump(s,p);return response(p);
}
static std::string cmd(Session& s,Peer& p,const std::string& text) {
  return exchange(s,p,wire(Type::request,9,{text.begin(),text.end()}));
}
static void ok(const std::string& text) { TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("\"ok\":true")); }
static void error(const std::string& text,const char* code) {
  TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("\"ok\":false"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find(std::string("\"code\":\"")+code+"\""));
}
static std::vector<uint8_t> capsule() {
  std::vector<uint8_t> bytes(kCodeOffset+8);
  std::memcpy(bytes.data(),"FMS1",4);bytes[4]=kAbiVersion;bytes[6]=kHeaderBytes;
  const auto le=[&](unsigned at,uint32_t n){for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(n>>(8*i));};
  le(8,kNanoBoardTag);le(12,api);le(16,kSlotAddress+kCodeOffset);le(20,8);
  le(24,kSlotAddress+kCodeOffset+1);le(28,kSlotAddress+kCodeOffset+5);
  for(unsigned i=0;i<8;++i)bytes[kCodeOffset+i]=uint8_t(i*13);
  sha256(bytes.data()+kCodeOffset,8,bytes.data()+32);return bytes;
}
static std::string hash(const std::vector<uint8_t>& bytes) {
  uint8_t digest[32];sha256(bytes.data(),bytes.size(),digest);char text[65];
  for (unsigned i = 0; i < 32; ++i) {
    std::snprintf(text + 2 * i, 3, "%02x", digest[i]);
  }
  return text;
}
static std::string begin_text(const std::vector<uint8_t>& bytes) {
  return "{\"op\":\"flash.begin\",\"target_id\":1,\"board_id\":\"nano_rp2040_connect\",\"format\":\"nano-managed-v1\",\"size\":"+
      std::to_string(bytes.size())+",\"sha256\":\""+hash(bytes)+"\"}";
}
static std::string op(const char* method,uint32_t id=1) {
  return std::string("{\"op\":\"flash.")+method+"\",\"target_id\":1,\"upload_id\":"+std::to_string(id)+"}";
}
static void hello(Session& s,Peer& p,const char* body="{\"op\":\"hello\"}") { ok(cmd(s,p,body)); }
static std::vector<uint8_t> chunk(const std::vector<uint8_t>& data,uint32_t offset=0,uint32_t id=1) {
  std::vector<uint8_t> bytes(8);put32(bytes.data(),id);put32(bytes.data()+4,offset);
  bytes.insert(bytes.end(),data.begin(),data.end());return wire(Type::upload,9,bytes);
}
static void verified(Session& s,Peer& p,const std::vector<uint8_t>& bytes) {
  ok(cmd(s,p,begin_text(bytes)));ok(exchange(s,p,chunk(bytes)));ok(cmd(s,p,op("finish")));
}
static void install(Fixture& f,uint32_t now=1) {
  f.upload.poll(now);
  std::thread core([&]{f.pause.park_if_requested();});
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!f.pause.parked()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  bool parked=f.pause.parked();
  if(parked)f.upload.poll(now+1);else f.pause.release();
  core.join();TEST_ASSERT_TRUE_MESSAGE(parked,"core did not park within two seconds");
}
void target_is_optional_and_minimum_hello_offer_is_supported() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);
  auto text=cmd(s,p,"{\"op\":\"hello\",\"max_payload\":512}");ok(text);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("nano-managed-v1"));TEST_ASSERT_LESS_OR_EQUAL_UINT(512,text.size());
  Peer q;Session legacy(f.channel,identity,8,0);auto old=cmd(legacy,q,"{\"op\":\"hello\"}");
  TEST_ASSERT_NOT_EQUAL(std::string::npos,old.find("\"targets\":[]"));
  error(cmd(legacy,q,begin_text(capsule())),"unsupported");error(exchange(legacy,q,chunk({1})),"unsupported");
}
void accepted_verified_installed_and_boot_confirmed_are_distinct() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
  auto bytes=capsule();auto begin=cmd(s,p,begin_text(bytes));ok(begin);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,begin.find("\"state\":\"accepted\""));
  error(cmd(s,p,op("commit")),"invalid_state");
  ok(exchange(s,p,chunk(bytes)));auto finish=cmd(s,p,op("finish"));ok(finish);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,finish.find("\"state\":\"verified\""));
  auto commit=cmd(s,p,op("commit"));ok(commit);TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,commit.find("\"state\":\"committing\""));
  error(cmd(s,p,op("commit")),"invalid_state");error(cmd(s,p,op("abort")),"invalid_state");
  install(f);auto installed=cmd(s,p,op("status"));ok(installed);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,installed.find("\"state\":\"installed\""));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(),f.flash.bytes,bytes.size());TEST_ASSERT_EQUAL_UINT(1,f.runtime.starts);
  returned=true;f.upload.poll(3);auto boot=cmd(s,p,op("status"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,boot.find("\"state\":\"boot_confirmed\""));
}
void malformed_schema_board_format_and_unarmed_requests_have_no_side_effects() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);
  error(cmd(s,p,begin_text(capsule())),"invalid_state");hello(s,p);
  for(auto text:{"{\"op\":\"flash.begin\"}","{\"op\":\"flash.status\",\"target_id\":1,\"upload_id\":0}",
      "{\"op\":\"flash.status\",\"target_id\":1,\"upload_id\":1,\"extra\":true}",
      "{\"op\":\"flash.status\",\"target_id\":1,\"upload_id\":1e0}"})error(cmd(s,p,text),"invalid_argument");
  auto text=begin_text(capsule());auto wrong=text;wrong.replace(wrong.find("nano_rp2040_connect"),19,"raspberry_pi_pico");
  error(cmd(s,p,wrong),"wrong_target");wrong=text;wrong.replace(wrong.find("nano-managed-v1"),15,"bad-format");
  error(cmd(s,p,wrong),"wrong_target");
  wrong=text;wrong.replace(wrong.find(hash(capsule())),64,std::string(64,'G'));
  error(cmd(s,p,wrong),"invalid_argument");
  wrong=text;wrong.replace(wrong.find(hash(capsule())),64,std::string(63,'0'));
  error(cmd(s,p,wrong),"invalid_argument");
  armed=false;error(cmd(s,p,text),"unauthorized");
  TEST_ASSERT_FALSE(f.stage.active());TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);
  armed=true;auto big=capsule();big.resize(4097);error(cmd(s,p,begin_text(big)),"insufficient_storage");
}
void duplicate_out_of_order_overflow_and_oversize_chunks_preserve_stage() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
  auto bytes=capsule();ok(cmd(s,p,begin_text(bytes)));
  error(exchange(s,p,chunk({1},UINT32_MAX)),"wrong_offset");error(exchange(s,p,chunk({1},1)),"wrong_offset");
  error(exchange(s,p,chunk({1},0,2)),"wrong_target");error(exchange(s,p,chunk({})),"invalid_argument");
  error(exchange(s,p,chunk(std::vector<uint8_t>(1025))),"invalid_argument");
  ok(exchange(s,p,chunk({bytes.begin(),bytes.begin()+64})));
  error(exchange(s,p,chunk({bytes.begin(),bytes.begin()+64})),"wrong_offset");
  error(cmd(s,p,op("finish")),"incomplete");
  ok(exchange(s,p,chunk({bytes.begin()+64,bytes.end()},64)));ok(cmd(s,p,op("finish")));
}
void digest_and_image_failures_never_commit() {
  for(unsigned fault=0;fault<7;++fault) {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);auto bytes=capsule();
    if (fault == 1) { bytes[8] ^= 1; }
    if (fault == 2) { bytes[12] ^= 1; }
    if (fault == 3) { bytes[16] ^= 1; }
    if (fault == 4) { bytes[70] = 1; }
    if (fault == 5) { bytes[24] = 0; }
    if (fault == 6) { bytes.pop_back(); }
    ok(cmd(s,p,begin_text(bytes)));if(fault==0)bytes.back()^=1;
    ok(exchange(s,p,chunk(bytes)));error(cmd(s,p,op("finish")),fault==0?"digest_mismatch":"invalid_image");
    TEST_ASSERT_FALSE(f.stage.active());error(cmd(s,p,op("commit")),"invalid_state");
    auto status=cmd(s,p,op("status"));TEST_ASSERT_NOT_EQUAL(std::string::npos,status.find("\"state\":\"failed\""));
    TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);
  }
}
void console_and_upload_ownership_are_mutually_exclusive() {
  Fixture f;Peer p,q;Session s(f.channel,identity,7,0,nullptr,&f.upload),other(f.channel,identity,8,0,nullptr,&f.upload);
  hello(s,p);hello(other,q);ok(cmd(other,q,"{\"op\":\"serial.open\",\"channel_id\":1}"));
  error(cmd(s,p,begin_text(capsule())),"busy");ok(cmd(other,q,"{\"op\":\"serial.close\",\"channel_id\":1}"));
  ok(cmd(s,p,begin_text(capsule())));error(cmd(other,q,begin_text(capsule())),"busy");
  error(cmd(other,q,"{\"op\":\"serial.open\",\"channel_id\":1}"),"busy");
  error(exchange(other,q,chunk({1})),"wrong_owner");error(cmd(other,q,op("abort")),"wrong_owner");
  ok(cmd(other,q,op("status")));ok(cmd(s,p,op("abort")));
  ok(cmd(other,q,"{\"op\":\"serial.open\",\"channel_id\":1}"));
}
void disconnect_before_commit_aborts_but_lost_commit_reply_is_queryable() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);verified(s,p,capsule());
  s.close();UploadStatus status;TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.status(1,status)));
  TEST_ASSERT_EQUAL_INT(int(UploadState::aborted),int(status.state));TEST_ASSERT_FALSE(f.stage.active());
  Peer q;Session next(f.channel,identity,7,0,nullptr,&f.upload);hello(next,q);
  auto bytes=capsule();ok(cmd(next,q,begin_text(bytes)));ok(exchange(next,q,chunk(bytes,0,2)));ok(cmd(next,q,op("finish",2)));
  s.close(); // Idempotent old close cannot dispose a new stage on the reused socket token.
  TEST_ASSERT_TRUE(f.stage.active());
  auto commit=op("commit",2);auto request=wire(Type::request,9,{commit.begin(),commit.end()});
  q.write_limit=0;q.input.insert(q.input.end(),request.begin(),request.end());pump(next,q);next.close();
  TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);install(f);
  Peer r;Session reconnect(f.channel,identity,7,0,nullptr,&f.upload);hello(reconnect,r);
  auto installed=cmd(reconnect,r,op("status",2));TEST_ASSERT_NOT_EQUAL(std::string::npos,installed.find("\"state\":\"installed\""));
  error(cmd(reconnect,r,op("commit",2)),"invalid_state");TEST_ASSERT_EQUAL_UINT(1,f.flash.erases);
}
void arm_removal_and_stalled_core_fail_without_erasing() {
  for(unsigned fault=0;fault<3;++fault) {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);verified(s,p,capsule());
    if(fault==0){armed=false;error(cmd(s,p,op("commit")),"unauthorized");armed=true;}
    ok(cmd(s,p,op("commit")));
    if(fault==0)armed=false;
    f.upload.poll(UINT32_MAX-100);
    if(fault==1)armed=false; // Arm removed after the park request is already active.
    f.upload.poll(1000);
    auto text=cmd(s,p,op("status"));TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("\"state\":\"failed\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find(fault<2?"unauthorized":"park_timeout"));
    TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_FALSE(f.stage.active());armed=true;
  }
}
void partial_install_failure_reports_touched_slot_without_boot_confirmation() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);verified(s,p,capsule());
  f.flash.fail=true;ok(cmd(s,p,op("commit")));install(f);
  auto text=cmd(s,p,op("status"));TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("install_failed"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos,text.find("\"slot_touched\":true"));TEST_ASSERT_EQUAL_UINT(0,f.runtime.starts);
  returned=true;f.upload.poll(3);UploadStatus status;f.upload.status(1,status);
  TEST_ASSERT_EQUAL_INT(int(UploadState::failed),int(status.state));
}
void every_chunk_split_and_partial_response_write_preserve_exact_bytes() {
  const auto bytes=capsule();const auto framed=chunk(bytes);
  for(std::size_t split=0;split<=framed.size();++split) {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);ok(cmd(s,p,begin_text(bytes)));
    p.write_limit=3;p.read_limit=7;p.input.insert(p.input.end(),framed.begin(),framed.begin()+split);pump(s,p);
    p.input.insert(p.input.end(),framed.begin()+split,framed.end());pump(s,p);ok(response(p));
    ok(cmd(s,p,op("finish")));std::size_t size=0;auto staged=f.stage.verified(7,&size);
    TEST_ASSERT_EQUAL_UINT(bytes.size(),size);TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(),staged,size);
  }
}
void session_timeout_invalid_json_and_reboot_dispose_uncommitted_state() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);ok(cmd(s,p,begin_text(capsule())));
  TEST_ASSERT_EQUAL_INT(int(SessionResult::timeout),int(s.poll(p,Session::idle_ms)));
  UploadStatus status;f.upload.status(1,status);TEST_ASSERT_EQUAL_INT(int(UploadState::aborted),int(status.state));
  Peer q;Session next(f.channel,identity,8,0,nullptr,&f.upload);hello(next,q);ok(cmd(next,q,begin_text(capsule())));
  auto request=wire(Type::request,9,{'{'});q.input.insert(q.input.end(),request.begin(),request.end());
  for(unsigned i=0;i<500&&next.poll(q,0)==SessionResult::running;++i){}
  TEST_ASSERT_FALSE(f.stage.active());Fixture reboot;
  TEST_ASSERT_EQUAL_INT(int(UploadError::wrong_target),int(reboot.upload.status(2,status)));
}
static std::vector<uint8_t> fixture(const char* name) {
  const auto path=std::string(UPLOAD_FIXTURE_DIR)+"/"+name+".hex";
  FILE* file=std::fopen(path.c_str(),"r");TEST_ASSERT_NOT_NULL(file);
  std::vector<uint8_t> bytes;unsigned byte;
  while(std::fscanf(file,"%2x",&byte)==1)bytes.push_back(uint8_t(byte));
  bool ended=std::feof(file);std::fclose(file);TEST_ASSERT_TRUE(ended);return bytes;
}
static void vector_exchange(Session& session,Peer& peer,const char* request,const char* expected) {
  const auto input=fixture(request),output=fixture(expected);
  peer.input.insert(peer.input.end(),input.begin(),input.end());pump(session,peer);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(output.size(),peer.output.size(),expected);
  TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(output.data(),peer.output.data(),output.size(),expected);
  peer.output.clear();
}
void sdk_vectors_match_production_identity_install_failure_and_reconnect_states() {
  {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);
    vector_exchange(s,p,"hello-request","hello-response");
    vector_exchange(s,p,"begin-request","accepted-response");
    vector_exchange(s,p,"chunk","chunk-response");
    vector_exchange(s,p,"finish-request","verified-response");
    vector_exchange(s,p,"commit-request","committing-response");
    install(f);
    vector_exchange(s,p,"status-request","installed-response");
    returned=true;f.upload.poll(3);
    vector_exchange(s,p,"status-request","boot-confirmed-response");
  }
  {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
    verified(s,p,capsule());
    vector_exchange(s,p,"abort-request","aborted-response");
  }
  {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
    verified(s,p,capsule());s.close();
    Peer q;Session next(f.channel,identity,8,0,nullptr,&f.upload);hello(next,q);
    vector_exchange(next,q,"status-request","disconnected-response");
  }
  {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
    verified(s,p,capsule());f.flash.fail=true;
    vector_exchange(s,p,"commit-request","committing-response");install(f);
    vector_exchange(s,p,"status-request","install-failed-response");
  }
  {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
    armed=false;vector_exchange(s,p,"begin-request","unauthorized-response");
    TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);
  }
}
void shared_upload_vectors_are_consumed_by_the_production_session() {
  Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
  auto bytes=capsule();auto expected_chunk=fixture("chunk");auto actual_chunk=chunk(bytes);
  TEST_ASSERT_EQUAL_UINT(expected_chunk.size(),actual_chunk.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_chunk.data(),actual_chunk.data(),actual_chunk.size());
  ok(exchange(s,p,fixture("begin-request")));ok(exchange(s,p,fixture("chunk")));
  auto finish=fixture("finish-request");p.input.insert(p.input.end(),finish.begin(),finish.end());pump(s,p);
  auto expected=fixture("verified-response");TEST_ASSERT_EQUAL_UINT(expected.size(),p.output.size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(),p.output.data(),expected.size());p.output.clear();
  ok(exchange(s,p,fixture("status-request")));ok(exchange(s,p,fixture("abort-request")));
}
#ifdef FIRMINGO_UPLOAD_FUZZ
void bounded_upload_mutations_never_write_flash_from_parser_calls() {
  uint32_t random=0x4d394655;
  std::printf("managed upload mutation seed: 0x%08x; 1000 cases\n",random);
  for(unsigned trial=0;trial<1000;++trial) {
    Fixture f;Peer p;Session s(f.channel,identity,7,0,nullptr,&f.upload);hello(s,p);
    ok(cmd(s,p,begin_text(capsule())));
    auto bytes=fixture(trial%2?"chunk":"commit-request");
    for(unsigned change=0;change<1+trial%8;++change) {
      random=random*1664525u+1013904223u;std::size_t at=random%bytes.size();
      random=random*1664525u+1013904223u;bytes[at]^=uint8_t(random>>24);
    }
    p.input.insert(p.input.end(),bytes.begin(),bytes.end());
    for(unsigned turn=0;turn<100;++turn) if(s.poll(p,turn*100)!=SessionResult::running)break;
    TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_LESS_OR_EQUAL_UINT(kProofStageBytes,f.stage.received());
  }
}
#endif
int main() {
  UNITY_BEGIN();
  RUN_TEST(target_is_optional_and_minimum_hello_offer_is_supported);
  RUN_TEST(accepted_verified_installed_and_boot_confirmed_are_distinct);
  RUN_TEST(malformed_schema_board_format_and_unarmed_requests_have_no_side_effects);
  RUN_TEST(duplicate_out_of_order_overflow_and_oversize_chunks_preserve_stage);
  RUN_TEST(digest_and_image_failures_never_commit);
  RUN_TEST(console_and_upload_ownership_are_mutually_exclusive);
  RUN_TEST(disconnect_before_commit_aborts_but_lost_commit_reply_is_queryable);
  RUN_TEST(arm_removal_and_stalled_core_fail_without_erasing);
  RUN_TEST(partial_install_failure_reports_touched_slot_without_boot_confirmation);
  RUN_TEST(every_chunk_split_and_partial_response_write_preserve_exact_bytes);
  RUN_TEST(session_timeout_invalid_json_and_reboot_dispose_uncommitted_state);
  RUN_TEST(shared_upload_vectors_are_consumed_by_the_production_session);
  RUN_TEST(sdk_vectors_match_production_identity_install_failure_and_reconnect_states);
#ifdef FIRMINGO_UPLOAD_FUZZ
  RUN_TEST(bounded_upload_mutations_never_write_flash_from_parser_calls);
#endif
  return UNITY_END();
}
