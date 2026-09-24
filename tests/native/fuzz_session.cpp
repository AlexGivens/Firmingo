#include "core/session.h"
#include "core/json.h"
#include "core/loopback.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace firmingo;
using namespace firmingo::protocol;
static uint32_t seed=0x53455331;
static unsigned iteration;
static uint32_t random32(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void require(bool condition){if(!condition){std::fprintf(stderr,"session mutation failure seed=53455331 iteration=%u\n",iteration);std::abort();}}
struct Peer : ByteIO {
  std::array<uint8_t,2048> bytes{};
  std::size_t size=0,offset=0,read_chunk=1,write_chunk=1,written=0;
  Decoder output;
  bool connected() const override {return true;}
  std::size_t read(uint8_t* data,std::size_t capacity) override {
    const auto n=std::min({capacity,size-offset,read_chunk});std::memcpy(data,bytes.data()+offset,n);offset+=n;return n;
  }
  std::size_t write(const uint8_t* data,std::size_t count) override {
    const auto n=std::min(count,write_chunk);written+=n;std::size_t p=0;
    while(p<n){const auto consumed=output.feed(data+p,n-p);require(consumed!=0);p+=consumed;
      if(output.state()==Decode::ready){
        require(output.size()<=512);
        if(output.type()==Type::response){json::Document d;require(d.parse(output.payload(),output.size()));bool ok;const auto index=d.find("ok");require(index>=0 && d.boolean(unsigned(index),ok));}
        else require(output.type()==Type::serial && output.size()>4 && get32(output.payload())==1);
        output.reset();
      }else require(output.state()==Decode::incomplete);
    }
    return n;
  }
  void command(const char* body,uint32_t id){const auto n=encode(bytes.data()+size,bytes.size()-size,Type::request,id,reinterpret_cast<const uint8_t*>(body),std::strlen(body));require(n!=0);size+=n;}
};
int main() {
  const char* commands[]={"{\"op\":\"serial.status\",\"channel_id\":1}","{\"op\":\"serial.close\",\"channel_id\":1}",
    "{\"op\":\"device.reset\",\"scope\":\"device\"}","{\"op\":\"serial.control\",\"channel_id\":1,\"control\":\"break\",\"value\":true}",
    "{\"op\":\"serial.open\",\"channel_id\":1,\"config\":{\"baud\":115200}}","{\"op\":\"flash.commit\",\"nested\":[{\"x\":1},false,null]}",
    "{\"op\":\"serial.status\",\"channel_id\":1,\"\\u006fp\":\"hello\"}"};
  for(iteration=0;iteration<20000;++iteration){
    Peer peer;peer.read_chunk=1+random32()%256;peer.write_chunk=random32()%529;
    peer.command("{\"op\":\"hello\",\"max_payload\":512}",1);peer.command("{\"op\":\"serial.open\",\"channel_id\":1}",2);
    const auto start=peer.size;
    if(iteration%3){peer.command(commands[random32()%7],3);}
    else {std::array<uint8_t,512> p{};put32(p.data(),1);for(unsigned i=4;i<p.size();++i)p[i]=uint8_t(random32());
      const auto n=encode(peer.bytes.data()+peer.size,peer.bytes.size()-peer.size,Type::serial,0,p.data(),p.size());require(n!=0);peer.size+=n;}
    if(iteration%4){const auto first=iteration%2?start:0;for(unsigned n=0;n<1+iteration%5;++n)peer.bytes[first+random32()%(peer.size-first)]^=uint8_t(1u<<(random32()%8));}
    if(iteration%11==0)peer.size=random32()%(peer.size+1);
    Loopback backend;Channel channel(backend);const Identity identity{"a1b2c3d4e5f60718","0123456789abcdef","nano_rp2040_connect","session-dev-v1"};
    const uint32_t epoch=iteration%2?0:0xfffffff0;
    Session session(channel,identity,1,epoch);
    for(unsigned tick=0;tick<128;++tick){const auto result=session.poll(peer,epoch+tick*100);require(peer.offset<=peer.size && peer.written<=128*(header_size+512));
      require(channel.owner()<=1 && channel.pending_rx()<=256 && channel.pending_tx()<=256);if(result!=SessionResult::running)break;}
    session.close();require(channel.owner()==0);
    std::array<uint8_t,512> json_bytes{};
    const auto* chosen=commands[random32()%7];const auto size=std::strlen(chosen);std::memcpy(json_bytes.data(),chosen,size);
    for(unsigned n=0;n<iteration%5;++n)json_bytes[random32()%size]^=uint8_t(1u<<(random32()%8));
    json::Document document;
    if(document.parse(json_bytes.data(),iteration%7?size:random32()%(size+1))){
      require(document.count()<=json::Document::max_tokens);
      for(unsigned i=0;i<document.count();++i){const auto& token=document.token(i);require(token.start<token.end && token.end<=size && token.next<=document.count());
        if(token.kind==json::Kind::string){char text[65];require(document.string(i,text,sizeof(text)));}}
    }
  }
  std::printf("JSON/session mutation: 20000 cases pass, seed=53455331\n");
}
