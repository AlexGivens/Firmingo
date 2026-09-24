#include "core/protocol.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace firmingo::protocol;
static uint32_t seed=0x464d4750;
static unsigned iteration;
static void require(bool condition) {
  if(!condition){std::fprintf(stderr,"protocol mutation failed: seed=464d4750 iteration=%u\n",iteration);std::abort();}
}
static uint32_t random32(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main() {
  for(iteration=0;iteration<20000;++iteration) {
    std::array<uint8_t,max_frame> bytes{};std::array<uint8_t,max_payload> payload{};
    for(auto& b:payload)b=uint8_t(random32());
    const auto type=uint8_t(1+random32()%5);
    const auto request=(type==3 || type==5)?0:random32()|1;
    const auto length=random32()%(max_payload+1);
    auto size=encode(bytes.data(),bytes.size(),Type(type),request,payload.data(),length);require(size!=0);
    if(iteration%4)for(unsigned n=0;n<1+iteration%5;++n)bytes[random32()%size]^=uint8_t(1u<<(random32()%8));
    if(iteration%7==0)size=random32()%(size+1);
    Decoder d;const auto limit=iteration%3?4096:512;require(d.negotiate(limit));
    std::size_t offset=0;
    while(offset<size) {
      const auto needed=d.needed();const auto chunk=1+random32()%513;
      const auto n=d.feed(bytes.data()+offset,std::min<std::size_t>(chunk,size-offset));
      require(n<=needed && n<=size-offset);offset+=n;
      if(d.state()==Decode::ready) {
        require(std::memcmp(bytes.data(),"FMGO",4)==0 && bytes[4]==1 && get16(bytes.data()+6)==0);
        require(d.size()<=d.limit() && offset==header_size+d.size());
        require(d.type()==Type(bytes[5]) && d.request()==get32(bytes.data()+8));
        require(std::memcmp(d.payload(),bytes.data()+header_size,d.size())==0);
        require(d.needed()==0 && d.feed(bytes.data(),size)==0);break;
      } else if(d.state()!=Decode::incomplete) {
        require(offset==header_size && d.needed()==0 && d.feed(bytes.data(),size)==0);break;
      } else require(n!=0);
    }
    d.reset();require(!d.started() && d.limit()==std::size_t(limit) && d.needed()==header_size);
  }
  std::printf("protocol frame mutation: 20000 cases pass, seed=464d4750\n");
}
