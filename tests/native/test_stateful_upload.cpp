#include "stateful_upload.h"
#include "core/session.h"
#include "core/application.h"
#include "unity.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace sf = firmingo_managed::stateful;
namespace fm = firmingo_managed;
using firmingo::UploadError;
using firmingo::UploadState;
using firmingo::UploadStatus;
static bool armed = true;
static bool arm() { return armed; }
void setUp() { armed = true; }
void tearDown() {}
namespace {
constexpr sf::Contract contract{0x10000100, 0x20030000, 32, 4096};
const firmingo::Identity identity{"a1b2c3d4e5f60718", "0123456789abcdef", "nano_rp2040_connect", "0.1.0-statefulupload-exp1"};
std::vector<std::uint8_t> read_hex(const char* path) {
  std::ifstream in(path); TEST_ASSERT_TRUE(in.good());
  std::vector<std::uint8_t> bytes; unsigned n;
  while (in >> std::hex >> n) { TEST_ASSERT_TRUE(n <= 255); bytes.push_back(n); }
  return bytes;
}
std::vector<std::uint8_t> image() { return read_hex(STATEFUL_FIXTURE_DIR "/initialized.hex"); }
std::vector<std::uint8_t> vector(const char* name) {
  return read_hex((std::string(STATEFUL_UPLOAD_FIXTURE_DIR) + "/" + name + ".hex").c_str());
}
struct Entries : sf::ExecutionPort {
  unsigned setups = 0, loops = 0, stops = 0;
  void stopped() override { ++stops; }
  void invoke(std::uint32_t entry) override { if (entry == 0x10200101) ++setups; else ++loops; }
};
struct Flash : fm::ProofFlashPort {
  std::array<std::uint8_t, 4096> bytes{};
  unsigned erases = 0, pages = 0;
  bool fail = false;
  bool begin() override { return true; }
  void end() override {}
  bool erase(std::uint32_t offset, std::size_t size) override {
    if (offset != fm::kSlotAddress-fm::kFlashXipAddress || size != 4096) return false;
    ++erases; bytes.fill(0xff); return true;
  }
  bool program(std::uint32_t offset, const std::uint8_t* page, std::size_t size) override {
    ++pages; const auto at = offset-(fm::kSlotAddress-fm::kFlashXipAddress);
    if (size != 256 || at > 3840 || (fail && pages == 2)) return false;
    std::memcpy(bytes.data()+at,page,size); return true;
  }
  const std::uint8_t* slot_data() const override { return bytes.data(); }
};
struct Fixture {
  std::array<std::uint8_t, 48> ram;
  Entries entries;
  sf::Runtime runtime{{ram.data()+8,32,contract.state_address},contract,entries};
  fm::Core1Pause pause;
  Flash flash;
  sf::Replacement replacement{runtime,pause,flash};
  sf::Upload upload{contract,runtime,replacement,arm};
  firmingo::ApplicationEndpoint backend;
  firmingo::Channel channel{backend};
  Fixture() {
    ram.fill(0xa5); auto bytes=image(); runtime.boot_from_slot(bytes.data(),bytes.size()); runtime.tick();
  }
  void guards() const { for(unsigned i=0;i<8;++i){TEST_ASSERT_EQUAL_HEX8(0xa5,ram[i]);TEST_ASSERT_EQUAL_HEX8(0xa5,ram[40+i]);} }
};
struct Peer : firmingo::ByteIO {
  std::vector<std::uint8_t> input, output;
  std::size_t at = 0, read_limit = 65536;
  bool online = true;
  bool connected() const override { return online; }
  std::size_t read(std::uint8_t* bytes,std::size_t size) override {
    const auto n=std::min({size,read_limit,input.size()-at}); if(n)std::memcpy(bytes,input.data()+at,n); at+=n;return n;
  }
  std::size_t write(const std::uint8_t* bytes,std::size_t size) override { output.insert(output.end(),bytes,bytes+size);return size; }
};
void exchange(firmingo::Session& session, Peer& peer, const char* request, const char* expected) {
  const auto bytes=vector(request);peer.input.insert(peer.input.end(),bytes.begin(),bytes.end());
  for(unsigned i=0;i<500;++i) TEST_ASSERT_EQUAL_INT(int(firmingo::SessionResult::running),int(session.poll(peer,0)));
  const auto want=vector(expected);
  if (peer.output != want && peer.output.size() >= 16)
    std::fprintf(stderr, "%s: actual body %.*s\n", request, int(peer.output.size()-16), peer.output.data()+16);
  TEST_ASSERT_EQUAL_UINT(want.size(),peer.output.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(),peer.output.data(),want.size());peer.output.clear();
}
UploadStatus stage(Fixture& f) {
  auto bytes=image();std::uint8_t digest[32];fm::sha256(bytes.data(),bytes.size(),digest);UploadStatus out;
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.begin(7,bytes.size(),digest,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.chunk(7,out.id,0,bytes.data(),bytes.size(),out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.finish(7,out.id,out)));return out;
}
void install(Fixture& f) {
  f.upload.poll(1); // Only the outer port loop requests park.
  std::thread core([&]{f.pause.park_if_requested();});
  auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!f.pause.parked()&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
  const bool parked=f.pause.parked();if(parked)f.upload.poll(2);else f.pause.release();core.join();
  TEST_ASSERT_TRUE(parked);
}
}  // namespace

void wire_vectors_use_target_two_and_distinguish_verified_installed_and_loop_return() {
  Fixture f;Peer peer;peer.read_limit=1;firmingo::Session session(f.channel,identity,7,0,nullptr,&f.upload);
  exchange(session,peer,"hello-request","hello-response");
  exchange(session,peer,"begin-request","accepted-response");
  exchange(session,peer,"chunk","chunk-response");
  exchange(session,peer,"finish-request","verified-response");
  exchange(session,peer,"commit-request","committing-response");
  TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_FALSE(f.replacement.active());
  install(f);exchange(session,peer,"status-request","installed-response");
  f.upload.poll(3);exchange(session,peer,"status-request","installed-response"); // Old generation's returned loop is insufficient.
  f.runtime.tick();f.upload.poll(4);exchange(session,peer,"status-request","boot-confirmed-response");
  TEST_ASSERT_EQUAL_UINT(2,f.entries.setups);TEST_ASSERT_EQUAL_UINT(1,f.entries.stops);f.guards();
}
void ownership_offsets_abort_disconnect_and_duplicate_commit_have_no_unrequested_effects() {
  Fixture f;UploadStatus out;auto bytes=image();std::uint8_t hash[32];fm::sha256(bytes.data(),bytes.size(),hash);
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.begin(7,bytes.size(),hash,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::busy),int(f.upload.begin(8,bytes.size(),hash,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::wrong_owner),int(f.upload.chunk(8,1,0,bytes.data(),1,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::wrong_offset),int(f.upload.chunk(7,1,1,bytes.data(),1,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::invalid_argument),int(f.upload.chunk(7,1,0,bytes.data(),1025,out)));
  f.upload.disconnect(8);TEST_ASSERT_TRUE(f.upload.blocks_console());f.upload.disconnect(7);
  f.upload.status(1,out);TEST_ASSERT_EQUAL_INT(int(UploadState::aborted),int(out.state));
  TEST_ASSERT_EQUAL_INT(int(UploadError::disconnected),int(out.error));TEST_ASSERT_FALSE(f.upload.blocks_console());
  out=stage(f);TEST_ASSERT_EQUAL_UINT(2,out.id);
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.abort(7,2,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadState::aborted),int(out.state));
  out=stage(f);TEST_ASSERT_EQUAL_UINT(3,out.id);
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.commit(7,3,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::invalid_state),int(f.upload.commit(7,3,out)));
  TEST_ASSERT_EQUAL_INT(int(UploadError::invalid_state),int(f.upload.abort(7,3,out)));
  f.upload.disconnect(7);TEST_ASSERT_TRUE(f.upload.blocks_console());install(f);
  f.upload.status(3,out);TEST_ASSERT_EQUAL_INT(int(UploadState::installed),int(out.state));
  TEST_ASSERT_EQUAL_UINT(1,f.flash.erases);
}
void digest_and_old_image_failures_never_reach_flash() {
  for(bool bad_digest:{false,true}) {
    Fixture f;auto bytes=image();if(!bad_digest)bytes[3]='1';std::uint8_t hash[32];fm::sha256(bytes.data(),bytes.size(),hash);
    if (bad_digest) hash[0] ^= 1;
    UploadStatus out;
    TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.begin(7,bytes.size(),hash,out)));
    TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.chunk(7,1,0,bytes.data(),bytes.size(),out)));
    TEST_ASSERT_EQUAL_INT(int(bad_digest?UploadError::digest_mismatch:UploadError::invalid_image),int(f.upload.finish(7,1,out)));
    f.upload.poll(1);TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_FALSE(f.upload.blocks_console());
  }
}
void arm_is_required_at_begin_commit_and_outer_poll() {
  Fixture f;UploadStatus out;auto bytes=image();std::uint8_t hash[32];fm::sha256(bytes.data(),bytes.size(),hash);
  armed=false;TEST_ASSERT_EQUAL_INT(int(UploadError::unauthorized),int(f.upload.begin(7,bytes.size(),hash,out)));
  armed=true;out=stage(f);armed=false;
  TEST_ASSERT_EQUAL_INT(int(UploadError::unauthorized),int(f.upload.commit(7,out.id,out)));
  armed=true;TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.commit(7,out.id,out)));
  armed=false;f.upload.poll(1);f.upload.status(out.id,out);
  TEST_ASSERT_EQUAL_INT(int(UploadState::failed),int(out.state));TEST_ASSERT_EQUAL_INT(int(UploadError::unauthorized),int(out.error));
  TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_EQUAL_UINT(0,f.entries.stops);
}
void stage_lease_expires_across_clock_wrap_even_with_commit_queued() {
  Fixture f;f.upload.poll(UINT32_MAX-100);auto out=stage(f);
  f.upload.poll(29898);TEST_ASSERT_TRUE(f.upload.blocks_console());
  TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.commit(7,out.id,out)));
  f.upload.poll(29900);f.upload.status(out.id,out);
  TEST_ASSERT_EQUAL_INT(int(UploadState::failed),int(out.state));TEST_ASSERT_EQUAL_INT(int(UploadError::io_error),int(out.error));
  TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);TEST_ASSERT_FALSE(f.replacement.active());
}
void park_timeout_and_partial_flash_failures_retain_status_without_replay() {
  for(bool fail_flash:{false,true}) {
    Fixture f;auto out=stage(f);f.upload.commit(7,out.id,out);
    if(fail_flash){f.flash.fail=true;install(f);}else{f.upload.poll(0);f.upload.poll(1000);}
    f.upload.status(out.id,out);TEST_ASSERT_EQUAL_INT(int(UploadState::failed),int(out.state));
    TEST_ASSERT_EQUAL_INT(int(fail_flash?UploadError::install_failed:UploadError::park_timeout),int(out.error));
    TEST_ASSERT_EQUAL_INT(fail_flash,out.slot_touched);TEST_ASSERT_EQUAL_INT(!fail_flash,bool(f.runtime.active_generation()));
    const auto erases=f.flash.erases;f.upload.poll(1001);f.upload.poll(2000);TEST_ASSERT_EQUAL_UINT(erases,f.flash.erases);f.guards();
  }
}
void session_rejects_old_target_and_releases_console_after_owner_disconnect() {
  Fixture f;Peer peer;firmingo::Session session(f.channel,identity,7,0,nullptr,&f.upload);
  exchange(session,peer,"hello-request","hello-response");
  exchange(session,peer,"old-target-request","wrong-target-response");
  exchange(session,peer,"begin-request","accepted-response");
  exchange(session,peer,"chunk","chunk-response");exchange(session,peer,"finish-request","verified-response");
  peer.online=false;TEST_ASSERT_EQUAL_INT(int(firmingo::SessionResult::disconnected),int(session.poll(peer,0)));
  UploadStatus out;f.upload.status(1,out);TEST_ASSERT_EQUAL_INT(int(UploadState::aborted),int(out.state));
  TEST_ASSERT_FALSE(f.upload.blocks_console());TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);
  Peer fresh_peer;firmingo::Session fresh(f.channel,identity,8,0,nullptr,&f.upload);
  exchange(fresh,fresh_peer,"hello-request","hello-response");
  exchange(fresh,fresh_peer,"status-request","disconnected-response");
}
#ifdef FIRMINGO_STATEFUL_UPLOAD_FUZZ
void bounded_adapter_mutations_keep_guards_and_never_flash_in_callbacks() {
  std::uint32_t seed=0x46555032;unsigned verified=0,rejected=0;
  for(unsigned i=0;i<2000;++i){
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
    Fixture f;auto bytes=image();const auto fault=seed%5;std::uint8_t hash[32];
    fm::sha256(bytes.data(),bytes.size(),hash);if(fault==0)hash[seed%32]^=1;
    UploadStatus out;TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.begin(7,bytes.size(),hash,out)));
    if(fault==1) bytes[seed%bytes.size()]^=1;
    std::size_t at=0;
    while(at<bytes.size()){
      const auto count=std::min<std::size_t>(1+(seed%73),bytes.size()-at);
      TEST_ASSERT_EQUAL_INT(int(UploadError::wrong_owner),int(f.upload.chunk(8,1,at,bytes.data()+at,count,out)));
      TEST_ASSERT_EQUAL_INT(int(UploadError::none),int(f.upload.chunk(7,1,at,bytes.data()+at,count,out)));at+=count;
    }
    const auto result=f.upload.finish(7,1,out);
    if(result==UploadError::none){++verified;f.upload.commit(7,1,out);TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);f.upload.poll(0);f.upload.poll(1000);}
    else{++rejected;TEST_ASSERT_EQUAL_INT(int(UploadError::digest_mismatch),int(result));}
    TEST_ASSERT_EQUAL_UINT(0,f.flash.erases);f.guards();
  }
  TEST_ASSERT_TRUE(verified&&rejected);std::printf("seed=0x46555032 cases=2000 verified=%u rejected=%u\n",verified,rejected);
}
#endif
int main(){UNITY_BEGIN();
#ifdef FIRMINGO_STATEFUL_UPLOAD_FUZZ
  RUN_TEST(bounded_adapter_mutations_keep_guards_and_never_flash_in_callbacks);
#else
  RUN_TEST(wire_vectors_use_target_two_and_distinguish_verified_installed_and_loop_return);
  RUN_TEST(ownership_offsets_abort_disconnect_and_duplicate_commit_have_no_unrequested_effects);
  RUN_TEST(digest_and_old_image_failures_never_reach_flash);
  RUN_TEST(arm_is_required_at_begin_commit_and_outer_poll);
  RUN_TEST(stage_lease_expires_across_clock_wrap_even_with_commit_queued);
  RUN_TEST(park_timeout_and_partial_flash_failures_retain_status_without_replay);
  RUN_TEST(session_rejects_old_target_and_releases_console_after_owner_disconnect);
#endif
return UNITY_END();}
