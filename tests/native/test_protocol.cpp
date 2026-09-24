#include "core/protocol.h"
#include "unity.h"
#include <array>
#include <cstdio>
#include <string>
#include <vector>
using namespace firmingo::protocol;
void setUp() {}
void tearDown() {}
static std::vector<uint8_t> fixture(const char* name) {
  const auto path = std::string(FIXTURE_DIR) + "/" + name;
  FILE* f = std::fopen(path.c_str(), "r"); TEST_ASSERT_NOT_NULL(f);
  std::vector<uint8_t> bytes; unsigned value;
  while (std::fscanf(f, "%2x", &value) == 1) bytes.push_back(uint8_t(value));
  const bool ended = std::feof(f); std::fclose(f); TEST_ASSERT_TRUE(ended);
  return bytes;
}
static void consume(Decoder& d, const uint8_t* bytes, std::size_t length) {
  std::size_t offset=0;
  while(offset<length) {
    const auto n=d.feed(bytes+offset,length-offset);
    TEST_ASSERT_NOT_EQUAL(0,n); offset+=n;
  }
}
void shared_wire_fixtures_match_encoder_and_decoder() {
  const char* names[]={"hello-request.hex","binary-data.hex"};
  const char hello[]="{\"op\":\"hello\"}";
  const uint8_t binary[]={0,0,0,1,0,13,10,255};
  for(unsigned i=0;i<2;++i) {
    auto expected=fixture(names[i]); uint8_t bytes[max_frame];
    const auto n=encode(bytes,sizeof(bytes),i?Type::serial:Type::request,i?0:1,
                        i?binary:reinterpret_cast<const uint8_t*>(hello),i?sizeof(binary):sizeof(hello)-1);
    TEST_ASSERT_EQUAL_UINT(expected.size(),n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(),bytes,n);
    Decoder d; consume(d,expected.data(),expected.size());
    TEST_ASSERT_EQUAL_INT(int(Decode::ready),int(d.state()));
    TEST_ASSERT_EQUAL_UINT32(i?8:14,d.size());
    TEST_ASSERT_EQUAL_UINT32(i?0:1,d.request());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes+header_size,d.payload(),d.size());
  }
}
void every_split_preserves_empty_small_and_maximum_frames() {
  std::array<uint8_t,max_payload> payload;
  for(unsigned i=0;i<max_payload;++i)payload[i]=uint8_t(i);
  uint8_t bytes[max_frame];
  for(std::size_t length:{std::size_t(0),std::size_t(14),std::size_t(256),max_payload}) {
    const auto size=encode(bytes,sizeof(bytes),Type::request,0x12345678,payload.data(),length);
    for(std::size_t split=0;split<=size;++split) {
      Decoder d; consume(d,bytes,split); consume(d,bytes+split,size-split);
      TEST_ASSERT_EQUAL_INT(int(Decode::ready),int(d.state()));
      TEST_ASSERT_EQUAL_UINT32(0x12345678,d.request());
      TEST_ASSERT_EQUAL_UINT32(length,d.size());
      if(length)TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(),d.payload(),length);
    }
  }
}
void coalesced_frames_leave_tail_for_next_frame() {
  auto a=fixture("hello-request.hex"), b=fixture("binary-data.hex");
  std::vector<uint8_t> bytes=a; bytes.insert(bytes.end(),b.begin(),b.end());
  Decoder d; auto n=d.feed(bytes.data(),bytes.size());
  TEST_ASSERT_EQUAL_UINT(header_size,n);
  n+=d.feed(bytes.data()+n,bytes.size()-n);
  TEST_ASSERT_EQUAL_UINT(a.size(),n);
  TEST_ASSERT_EQUAL_INT(int(Decode::ready),int(d.state()));
  TEST_ASSERT_EQUAL_UINT(0,d.feed(bytes.data()+n,bytes.size()-n));
  d.reset(); consume(d,bytes.data()+n,bytes.size()-n);
  TEST_ASSERT_EQUAL_UINT32(0,d.request()); TEST_ASSERT_EQUAL_UINT32(1,get32(d.payload()));
}
void all_header_faults_stop_before_collecting_payload() {
  for(unsigned fault=0;fault<10;++fault) {
    uint8_t bytes[max_frame]; encode(bytes,sizeof(bytes),Type::request,1,nullptr,0);
    Decode expected=Decode::malformed;
    switch(fault) {
      case 0:bytes[0]=0;break;
      case 1:bytes[3]=0;break;
      case 2:bytes[5]=0;break;
      case 3:bytes[5]=6;break;
      case 4:bytes[6]=1;break;
      case 5:bytes[7]=1;break;
      case 6:put32(bytes+8,0);break;
      case 7:bytes[4]=2;expected=Decode::unsupported_version;break;
      case 8:put32(bytes+12,4097);expected=Decode::oversized;break;
      case 9:put32(bytes+12,0xffffffff);expected=Decode::oversized;break;
    }
    Decoder d; TEST_ASSERT_EQUAL_UINT(header_size,d.feed(bytes,sizeof(bytes)));
    TEST_ASSERT_EQUAL_INT(int(expected),int(d.state()));
    TEST_ASSERT_EQUAL_UINT(0,d.needed()); TEST_ASSERT_EQUAL_UINT(0,d.feed(bytes,sizeof(bytes)));
    d.reset(); encode(bytes,sizeof(bytes),Type::request,1,nullptr,0);
    consume(d,bytes,header_size); TEST_ASSERT_EQUAL_INT(int(Decode::ready),int(d.state()));
  }
}
void request_id_rules_cover_every_type() {
  for(uint8_t type=1;type<=5;++type) {
    for(uint32_t request:{uint32_t(0),uint32_t(1),uint32_t(0xffffffff)}) {
      uint8_t bytes[max_frame]; const bool valid=(type==3 || type==5)?request==0:request!=0;
      const auto n=encode(bytes,sizeof(bytes),Type(type),request,nullptr,0);
      TEST_ASSERT_EQUAL_UINT(valid?header_size:0,n);
      encode(bytes,sizeof(bytes),Type::request,1,nullptr,0);bytes[5]=type;put32(bytes+8,request);
      Decoder d; consume(d,bytes,header_size);
      TEST_ASSERT_EQUAL_INT(int(valid?Decode::ready:Decode::malformed),int(d.state()));
    }
  }
}
void negotiation_enforces_limits_only_between_frames() {
  Decoder d;
  TEST_ASSERT_FALSE(d.negotiate(0));TEST_ASSERT_FALSE(d.negotiate(511));TEST_ASSERT_FALSE(d.negotiate(65537));
  TEST_ASSERT_TRUE(d.negotiate(65536));TEST_ASSERT_EQUAL_UINT(4096,d.limit());
  TEST_ASSERT_TRUE(d.negotiate(512));TEST_ASSERT_EQUAL_UINT(512,d.limit());
  uint8_t bytes[max_frame]; std::array<uint8_t,513> payload{};
  encode(bytes,sizeof(bytes),Type::request,1,payload.data(),512);
  TEST_ASSERT_EQUAL_UINT(1,d.feed(bytes,1));TEST_ASSERT_FALSE(d.negotiate(4096));
  consume(d,bytes+1,header_size+512-1);TEST_ASSERT_FALSE(d.negotiate(4096));
  d.reset();TEST_ASSERT_EQUAL_UINT(512,d.limit());
  encode(bytes,sizeof(bytes),Type::request,1,payload.data(),513);
  consume(d,bytes,header_size);TEST_ASSERT_EQUAL_INT(int(Decode::oversized),int(d.state()));
  TEST_ASSERT_FALSE(d.negotiate(4096));d.reset();TEST_ASSERT_TRUE(d.negotiate(4096));
}
void encoder_checks_bounds_before_touching_output() {
  std::array<uint8_t,max_frame> bytes; bytes.fill(0xa5);
  TEST_ASSERT_EQUAL_UINT(0,encode(bytes.data(),bytes.size(),Type::request,1,bytes.data(),4097));
  TEST_ASSERT_EQUAL_UINT(0,encode(bytes.data(),15,Type::request,1,nullptr,0));
  TEST_ASSERT_EQUAL_UINT(0,encode(bytes.data(),bytes.size(),Type::request,1,nullptr,1));
  TEST_ASSERT_EQUAL_UINT(0,encode(bytes.data(),bytes.size(),Type(6),1,nullptr,0));
  TEST_ASSERT_EQUAL_UINT(0,encode(nullptr,bytes.size(),Type::request,1,nullptr,0));
  TEST_ASSERT_EACH_EQUAL_UINT8(0xa5,bytes.data(),bytes.size());
  uint8_t value=7;
  TEST_ASSERT_EQUAL_UINT(0,encode(bytes.data(),16,Type::request,1,&value,1));
  TEST_ASSERT_EQUAL_UINT(17,encode(bytes.data(),17,Type::request,1,&value,1));
  TEST_ASSERT_EQUAL_UINT8(0xa5,bytes[17]);
}
void incomplete_and_ready_frames_stay_bounded_until_reset() {
  Decoder d; auto bytes=fixture("hello-request.hex");
  TEST_ASSERT_EQUAL_UINT(0,d.feed(nullptr,3));TEST_ASSERT_FALSE(d.started());
  for(std::size_t i=0;i<bytes.size();++i) {
    TEST_ASSERT_EQUAL_UINT(1,d.feed(bytes.data()+i,1));
    TEST_ASSERT_EQUAL_INT(int(i+1==bytes.size()?Decode::ready:Decode::incomplete),int(d.state()));
  }
  TEST_ASSERT_EQUAL_UINT(0,d.needed());TEST_ASSERT_EQUAL_UINT(0,d.feed(bytes.data(),bytes.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data()+header_size,d.payload(),14);
  d.reset();TEST_ASSERT_FALSE(d.started());TEST_ASSERT_EQUAL_UINT(header_size,d.needed());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(shared_wire_fixtures_match_encoder_and_decoder);
  RUN_TEST(every_split_preserves_empty_small_and_maximum_frames);
  RUN_TEST(coalesced_frames_leave_tail_for_next_frame);
  RUN_TEST(all_header_faults_stop_before_collecting_payload);
  RUN_TEST(request_id_rules_cover_every_type);
  RUN_TEST(negotiation_enforces_limits_only_between_frames);
  RUN_TEST(encoder_checks_bounds_before_touching_output);
  RUN_TEST(incomplete_and_ready_frames_stay_bounded_until_reset);
  return UNITY_END();
}
