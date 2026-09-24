#include "ports/arduino_pico/local_dhcp.h"
#include <lwip/udp.h>
#include "unity.h"
#include <stdlib.h>
#include <string.h>

static firmingo_dhcp_server_t server;
static struct udp_pcb pcb;
static struct netif nic = {7};
static ip_addr_t ip = {{192,168,77,1}}, mask = {{255,255,255,0}}, destination;
static uint8_t sent[548];
static unsigned sends, removes, outstanding;
static u16_t sent_len;
static uint64_t now;
static int fail_alloc, fail_new, bind_error, send_error;
uint64_t get_absolute_time(void) { return now * 1000; }
struct udp_pcb *udp_new(void) { return fail_new ? NULL : &pcb; }
void udp_remove(struct udp_pcb *p) { TEST_ASSERT_EQUAL_PTR(&pcb,p); ++removes; }
void udp_recv(struct udp_pcb *p, udp_recv_fn fn, void *arg) { p->callback=fn; p->arg=arg; }
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *a, u16_t port) {
    (void)p; TEST_ASSERT_EQUAL_UINT(67,port); TEST_ASSERT_EQUAL_UINT(0,a->bytes[0]); return bind_error;
}
void udp_bind_netif(struct udp_pcb *p, struct netif *n) { p->netif=n; }
struct pbuf *pbuf_alloc(int layer, u16_t size, int type) {
    (void)layer; (void)type;
    if(fail_alloc) return NULL;
    struct pbuf *p=calloc(1,sizeof(*p)); TEST_ASSERT_NOT_NULL(p);
    p->payload=calloc(1,size ? size : 1); TEST_ASSERT_NOT_NULL(p->payload);
    p->tot_len=p->len=size; ++outstanding; return p;
}
uint8_t pbuf_free(struct pbuf *p) {
    while(p) { struct pbuf *n=p->next; free(p->payload); free(p); --outstanding; p=n; }
    return 1;
}
u16_t pbuf_copy_partial(const struct pbuf *p, void *out, u16_t size, u16_t offset) {
    u16_t copied=0;
    for(;p && copied<size;p=p->next) {
        if(offset>=p->len) { offset-=p->len; continue; }
        u16_t n=p->len-offset; if(n>size-copied) n=size-copied;
        memcpy((uint8_t *)out+copied,(uint8_t *)p->payload+offset,n);
        copied+=n; offset=0;
    }
    return copied;
}
err_t udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *a, u16_t port) {
    (void)p; TEST_ASSERT_EQUAL_UINT(68,port);
    if(send_error) return send_error;
    TEST_ASSERT_LESS_OR_EQUAL_UINT(sizeof(sent),b->tot_len);
    sent_len=pbuf_copy_partial(b,sent,b->tot_len,0); destination=*a; ++sends; return 0;
}
void setUp(void) {
    memset(&server,0,sizeof(server)); memset(&pcb,0,sizeof(pcb)); memset(sent,0,sizeof(sent));
    sends=removes=outstanding=now=0; sent_len=0; fail_alloc=fail_new=bind_error=send_error=0;
    TEST_ASSERT_EQUAL_INT(0,firmingo_dhcp_init(&server,&ip,&mask,&nic));
}
void tearDown(void) { firmingo_dhcp_deinit(&server); TEST_ASSERT_EQUAL_UINT(0,outstanding); }

typedef struct { uint8_t bytes[600]; size_t len; } request_t;
static void opt(request_t *r,uint8_t code,const uint8_t *data,uint8_t n) {
    r->bytes[r->len++]=code; r->bytes[r->len++]=n; memcpy(r->bytes+r->len,data,n); r->len+=n;
}
static request_t request(uint8_t type,uint8_t client) {
    request_t r={{0},240}; r.bytes[0]=1; r.bytes[1]=1; r.bytes[2]=6;
    r.bytes[4]=0x12; r.bytes[5]=0x34; r.bytes[6]=0x56; r.bytes[7]=0x78;
    r.bytes[10]=0x80; r.bytes[28]=2; r.bytes[33]=client;
    memcpy(r.bytes+236,"\x63\x82\x53\x63",4);
    opt(&r,53,&type,1); return r;
}
static void finish(request_t *r) { r->bytes[r->len++]=255; }
static void deliver_split(const request_t *r,size_t split,int allocation_failure,u16_t port) {
    struct pbuf *p=pbuf_alloc(0,(u16_t)split,0);
    memcpy(p->payload,r->bytes,split);
    if(split<r->len) {
        p->next=pbuf_alloc(0,(u16_t)(r->len-split),0);
        memcpy(p->next->payload,r->bytes+split,r->len-split); p->tot_len=(u16_t)r->len;
    }
    fail_alloc=allocation_failure;
    pcb.callback(pcb.arg,&pcb,p,&ip,port);
    fail_alloc=0; TEST_ASSERT_EQUAL_UINT(0,outstanding);
}
static void deliver(request_t *r) { finish(r); deliver_split(r,r->len,0,68); }
static const uint8_t *reply_option(uint8_t wanted) {
    for(size_t i=240;i<sent_len;) {
        uint8_t code=sent[i++]; if(code==255) return NULL; if(!code) continue;
        TEST_ASSERT_LESS_THAN_UINT(sent_len,i);
        uint8_t n=sent[i++]; TEST_ASSERT_LESS_OR_EQUAL_UINT(sent_len,i+n);
        if(code==wanted) return sent+i-1; i+=n;
    }
    TEST_FAIL_MESSAGE("missing END"); return NULL;
}
static void type_is(uint8_t type) { const uint8_t *o=reply_option(53); TEST_ASSERT_NOT_NULL(o); TEST_ASSERT_EQUAL_UINT(type,o[1]); }
static void request_ip(request_t *r,uint8_t last,int select) {
    uint8_t a[]={192,168,77,last}; opt(r,50,a,4); if(select) opt(r,54,ip.bytes,4);
}
static void acquire(uint8_t client,uint8_t last) {
    request_t d=request(1,client); deliver(&d);
    request_t r=request(3,client); request_ip(&r,last,1); deliver(&r); type_is(5);
}

void replies_are_local_only_exact_bytes_even_when_routes_are_requested(void) {
    request_t r=request(1,1); uint8_t requested[]={1,3,6,51,54,58,59,121,249};
    opt(&r,55,requested,sizeof(requested)); opt(&r,3,ip.bytes,4); opt(&r,6,ip.bytes,4); deliver(&r);
    TEST_ASSERT_EQUAL_UINT(1,sends); type_is(2);
    const uint8_t expected[]={53,1,2,54,4,192,168,77,1,1,4,255,255,255,0,
        51,4,0,0,2,88,58,4,0,0,1,44,59,4,0,0,2,13,255};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,sent+240,sizeof(expected));
    TEST_ASSERT_NULL(reply_option(3)); TEST_ASSERT_NULL(reply_option(6)); TEST_ASSERT_NULL(reply_option(121));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(r.bytes+4,sent+4,4);
    const uint8_t offered[]={192,168,77,16}; TEST_ASSERT_EQUAL_UINT8_ARRAY(offered,sent+16,4);
    TEST_ASSERT_EQUAL_UINT(255,destination.bytes[0]); TEST_ASSERT_EQUAL_UINT(300,sent_len);
    request_t ack=request(3,1); request_ip(&ack,16,1); deliver(&ack); type_is(5);
    TEST_ASSERT_NULL(reply_option(3)); TEST_ASSERT_NULL(reply_option(6));
}
void reordered_padded_options_and_every_pbuf_split_work(void) {
    request_t r=request(1,1); r.len=240; r.bytes[r.len++]=0;
    uint8_t vendor[]={7,9,1}, type=1; opt(&r,60,vendor,3); r.bytes[r.len++]=0; opt(&r,53,&type,1); finish(&r);
    for(size_t split=0;split<=r.len;++split) {
        unsigned before=sends; deliver_split(&r,split,0,68); TEST_ASSERT_EQUAL_UINT(before+1,sends); type_is(2);
    }
}
void malformed_truncated_packets_do_not_allocate_or_reply(void) {
    request_t good=request(1,1); finish(&good);
    for(size_t n=0;n<good.len;++n) { request_t r=good; r.len=n; deliver_split(&r,n,0,68); }
    TEST_ASSERT_EQUAL_UINT(0,sends);
    for(unsigned kind=0;kind<9;++kind) {
        request_t r=request(1,1);
        switch(kind) {
        case 0:r.bytes[236]^=1;break;
        case 1:r.bytes[0]=2;break;
        case 2:r.bytes[2]=16;break;
        case 3:r.bytes[24]=1;break;
        case 4:r.bytes[241]=255;break;
        case 5:{uint8_t t=1;opt(&r,53,&t,1);break;}
        case 6:opt(&r,50,ip.bytes,3);break;
        case 7:{uint8_t overload=1;opt(&r,52,&overload,1);break;}
        case 8:r.len=599;break;
        }
        deliver(&r);
    }
    TEST_ASSERT_EQUAL_UINT(0,sends);
    firmingo_dhcp_lease_t empty[8]={{0}}; TEST_ASSERT_EQUAL_MEMORY(empty,server.lease,sizeof(empty));
}
void discover_request_retry_renew_rebind_and_expiry(void) {
    acquire(1,16); TEST_ASSERT_EQUAL_UINT(2,server.lease[0].state);
    now=1000; request_t retry=request(3,1); request_ip(&retry,16,1); deliver(&retry); type_is(5);
    for(int rebind=0;rebind<2;++rebind) {
        now+=1000; request_t renewal=request(3,1); memcpy(renewal.bytes+12,ip.bytes,4); renewal.bytes[15]=16;
        if(!rebind) renewal.bytes[10]=0;
        deliver(&renewal); type_is(5); TEST_ASSERT_EQUAL_UINT(16,destination.bytes[3]);
        TEST_ASSERT_EQUAL_UINT(now,server.lease[0].since_ms);
    }
    now+=0x100000000ull + 600000; request_t next=request(1,2); deliver(&next); TEST_ASSERT_EQUAL_UINT(16,sent[19]);
}
void reservations_pool_exhaustion_and_offer_expiry(void) {
    for(uint8_t c=1;c<=8;++c) { request_t r=request(1,c); deliver(&r); TEST_ASSERT_EQUAL_UINT(15+c,sent[19]); }
    request_t full=request(1,9); deliver(&full); TEST_ASSERT_EQUAL_UINT(8,sends);
    now=30000; full=request(1,9); deliver(&full); TEST_ASSERT_EQUAL_UINT(9,sends); TEST_ASSERT_EQUAL_UINT(16,sent[19]);
}
void another_server_is_ignored_and_wrong_subnet_is_naked(void) {
    acquire(1,16); unsigned before=sends;
    request_t r=request(3,1); request_ip(&r,16,0); uint8_t other[]={192,168,77,2};opt(&r,54,other,4);deliver(&r);
    TEST_ASSERT_EQUAL_UINT(before,sends);
    r=request(3,1);uint8_t wrong[]={192,168,7,16};opt(&r,50,wrong,4);deliver(&r);type_is(6);
    TEST_ASSERT_NULL(reply_option(51));TEST_ASSERT_NULL(reply_option(3));TEST_ASSERT_NULL(reply_option(6));
    TEST_ASSERT_EQUAL_UINT(0,sent[19]); TEST_ASSERT_EQUAL_UINT(255,destination.bytes[0]);
}
void release_decline_and_clock_wrap(void) {
    now=0xfffffff0u; acquire(1,16);
    now=0x100000014ull;request_t renewal=request(3,1);memcpy(renewal.bytes+12,ip.bytes,4);renewal.bytes[15]=16;deliver(&renewal);type_is(5);
    request_t release=request(7,1);memcpy(release.bytes+12,ip.bytes,4);release.bytes[15]=16;opt(&release,54,ip.bytes,4);deliver(&release);
    TEST_ASSERT_EQUAL_UINT(0,server.lease[0].state);
    request_t d=request(1,2);deliver(&d);request_t decline=request(4,2);request_ip(&decline,16,1);deliver(&decline);
    TEST_ASSERT_EQUAL_UINT(3,server.lease[0].state);
    d=request(1,3);deliver(&d);TEST_ASSERT_EQUAL_UINT(17,sent[19]);
    now+=60000;d=request(1,4);deliver(&d);TEST_ASSERT_EQUAL_UINT(16,sent[19]);
}
void client_identifier_is_used_and_echoed(void) {
    uint8_t id[]={0,4,8,12};request_t r=request(1,1);opt(&r,61,id,sizeof(id));deliver(&r);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(id,reply_option(61)+1,sizeof(id));
    r=request(3,2);request_ip(&r,16,1);opt(&r,61,id,sizeof(id));deliver(&r);type_is(5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(id,reply_option(61)+1,sizeof(id));
}
void allocation_bind_and_send_failures_release_resources(void) {
    request_t r=request(1,1);finish(&r);deliver_split(&r,r.len,1,68);
    TEST_ASSERT_EQUAL_UINT(0,sends);TEST_ASSERT_EQUAL_UINT(0,server.lease[0].state);
    send_error=-1;deliver_split(&r,r.len,0,68);TEST_ASSERT_EQUAL_UINT(0,server.lease[0].state);TEST_ASSERT_EQUAL_UINT(2,server.send_errors);
    firmingo_dhcp_deinit(&server);bind_error=-2;TEST_ASSERT_EQUAL_INT(-2,firmingo_dhcp_init(&server,&ip,&mask,&nic));TEST_ASSERT_NULL(server.udp);
    bind_error=0;fail_new=1;TEST_ASSERT_NOT_EQUAL(0,firmingo_dhcp_init(&server,&ip,&mask,&nic));TEST_ASSERT_NULL(server.udp);
}
void inform_does_not_allocate_and_unsupported_config_is_rejected(void) {
    request_t r=request(8,1);memcpy(r.bytes+12,ip.bytes,4);r.bytes[15]=40;deliver(&r);type_is(5);
    TEST_ASSERT_NULL(reply_option(51));TEST_ASSERT_NULL(reply_option(3));TEST_ASSERT_NULL(reply_option(6));TEST_ASSERT_EQUAL_UINT(40,destination.bytes[3]);
    TEST_ASSERT_EQUAL_UINT(0,server.lease[0].state);
    firmingo_dhcp_deinit(&server);ip_addr_t bad={{255,255,252,0}};
    TEST_ASSERT_NOT_EQUAL(0,firmingo_dhcp_init(&server,&ip,&bad,&nic));TEST_ASSERT_NULL(server.udp);
}
#ifdef FIRMINGO_DHCP_FUZZ
static uint32_t random_state = 0x464d474f;
static uint32_t random_word(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
void bounded_mutation_fuzz(void) {
    // Fixed seed printed below, for reproducibility. This is mutation stress,
    // not a substitute for the focused protocol tests or coverage-guided fuzzing.
    for(unsigned iteration=0;iteration<20000;++iteration) {
        request_t r=request((iteration % 8) + 1,1);
        if(iteration % 2) request_ip(&r,16,1);
        finish(&r);
        unsigned mutations=random_word()%12;
        for(unsigned m=0;m<mutations;++m) r.bytes[random_word()%sizeof(r.bytes)]=(uint8_t)random_word();
        if(iteration%3==0) r.len=random_word()%sizeof(r.bytes);
        unsigned before=sends;
        deliver_split(&r,random_word()%(r.len+1),0,68);
        if(sends!=before) {
            TEST_ASSERT_NULL(reply_option(3)); TEST_ASSERT_NULL(reply_option(6));
            TEST_ASSERT_NULL(reply_option(121)); TEST_ASSERT_NULL(reply_option(249));
        }
        now+=1000;
    }
}
int main(void) {
    printf("DHCP mutation fuzz seed=0x464d474f, iterations=20000\n");
    UNITY_BEGIN(); RUN_TEST(bounded_mutation_fuzz); return UNITY_END();
}
#else
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(replies_are_local_only_exact_bytes_even_when_routes_are_requested);
    RUN_TEST(reordered_padded_options_and_every_pbuf_split_work);
    RUN_TEST(malformed_truncated_packets_do_not_allocate_or_reply);
    RUN_TEST(discover_request_retry_renew_rebind_and_expiry);
    RUN_TEST(reservations_pool_exhaustion_and_offer_expiry);
    RUN_TEST(another_server_is_ignored_and_wrong_subnet_is_naked);
    RUN_TEST(release_decline_and_clock_wrap);
    RUN_TEST(client_identifier_is_used_and_echoed);
    RUN_TEST(allocation_bind_and_send_failures_release_resources);
    RUN_TEST(inform_does_not_allocate_and_unsupported_config_is_rejected);
    return UNITY_END();
}
#endif
