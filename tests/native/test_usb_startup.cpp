#include "unity.h"
#include "core/session.h"
#include "core/version.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <string>

// Fake low-level effects; the generated include contains the actual patched
// core USB block, USB prepare/begin, NCM begin and reference sketch setup.
static bool initialized, dhcpOk, configOk, ncmOk, httpOk, sourceOk, echoOk, protocolOk;
static unsigned mutexInitializations, controllerStarts, connects, disconnects, serialStarts;
static unsigned interfaces, lwipLocks;
static unsigned ncmQueuedPackets, ncmRenewedPackets, ncmXmitAttempts, ncmWakeRequests;
static bool usbMutexAvailable, usbMutexHeld, wakeRequestedWithUsbMutexHeld;
static uint32_t tick;
static std::vector<std::string> events;
struct mutex_t {};
void mutex_init(mutex_t*) { ++mutexInitializations; }
bool tusb_inited() { return initialized; }
void tusb_init() {
  TEST_ASSERT_EQUAL_UINT(0,lwipLocks);
  ++controllerStarts; initialized=true; events.push_back("controller");
}
void queue_init(void*, unsigned, unsigned) {}
struct async_context_t {};
struct async_when_pending_worker_t { void* user_data=nullptr; bool work_pending=false; };
using Worker = async_when_pending_worker_t;
void* __getEthernetContext() { return nullptr; }
void async_context_add_when_pending_worker(void*, Worker*) {}
bool mutex_try_enter(mutex_t*, void*) {
  if (!usbMutexAvailable) return false;
  usbMutexHeld=true; return true;
}
void mutex_exit(mutex_t*) { usbMutexHeld=false; }
void async_context_set_work_pending(async_context_t*, Worker* worker) {
  ++ncmWakeRequests;
  wakeRequestedWithUsbMutexHeld |= usbMutexHeld;
  worker->work_pending=true;
}
int user_irq_claim_unused(bool) { return 1; }
void irq_set_exclusive_handler(int, void(*)()) {}
void irq_set_enabled(int,bool) {}
void add_alarm_in_us(int,int64_t(*)(int,void*),void*,bool) {}
constexpr int USB_TASK_INTERVAL=1000, NCMETHERNET_XMIT_QUEUE_LENGTH=12, TUD_CDC_NCM_DESC_LEN=86;
struct pbuf {};
struct netif {};
struct USBClass {
  bool _prepared=false;
  mutex_t mutex;
  int usbTaskIRQ=0;
  void prepare(); void begin();
  void disconnect() { ++disconnects; }
  void connect() { ++connects; }
  void setupDescHIDReport() {}
  void setupUSBDescriptor() { events.push_back("descriptors"); }
  uint8_t registerEndpointIn() { return 1; }
  uint8_t registerEndpointOut() { return 1; }
  uint8_t registerString(const char*) { return 1; }
  uint8_t registerInterface(unsigned n,void(*)(int,uint8_t*,int,void*),void*,unsigned,int,int) {
    interfaces+=n; return 1;
  }
  static void usbIRQ() {}
  static int64_t timerTask(int,void*) { return 0; }
} USB;
struct SerialStub { void begin(int) { ++serialStarts; } } Serial;
struct NCMEthernet {
  int _xmit_queue=0;
  Worker _recv_irq_worker{};
  uint8_t _epIn=0,_epOut=0,_epNotif=0,_strID=0,_strMac=0,_id=0;
  char macAddrStr[13]{};
  bool _holding_both_mutexes=false, _tud_recv_cb_called=false;
  uint32_t _recv_worker_runs=0, _recv_frames=0, _recv_deferred=0, _recv_batch_peak=0;
  uint32_t _recv_mutex_contentions=0;
  uint32_t _recv_budget_exhaustions=0, _recv_wake_requests=0;
  bool begin(const uint8_t*, netif*);
  static void _set_recv_pending() {}
  void _try_process_xmit_queue() { ++ncmXmitAttempts; }
  static void _usb_interface_cb(int,uint8_t*,int,void*) {}
};
struct NCMEthernetlwIP {
  static void _call_irq(async_context_t*,async_when_pending_worker_t*);
};
static NCMEthernet* _ncm_ethernet_instance;
static uint8_t tud_network_mac_address[6];
void tud_network_recv_renew() {
  if (!ncmQueuedPackets) return;
  --ncmQueuedPackets; ++ncmRenewedPackets;
  _ncm_ethernet_instance->_tud_recv_cb_called=true;
}
void tud_task() {}
[[noreturn]] void panic(const char* text) { throw std::runtime_error(text); }
struct FakeIP {};
struct Ethernet : NCMEthernet {
  bool config(FakeIP,FakeIP,FakeIP,FakeIP) { return configOk; }
  bool begin() {
    events.push_back("ncm");
    const uint8_t mac[6]={2,3,4,5,6,7};
    return ncmOk && NCMEthernet::begin(mac,nullptr);
  }
  void* getNetIf() { return nullptr; }
} ethernet;
struct Dhcp { void* udp=nullptr; } dhcpServer;
static FakeIP dhcpAddress,dhcpNetmask,PICO_IP,NO_GATEWAY,SUBNET_MASK;
struct Server {
  std::string name;
  bool ok=false;
  void begin() { events.push_back(name); ok=name=="http" ? httpOk : sourceOk; }
  explicit operator bool() const { return ok; }
} httpServer{"http"},sourceServer{"source"};
struct Echo { bool begin(FakeIP*,unsigned) { events.push_back("echo"); return echoOk; } } echoService;
struct Snapshot {
  uint32_t setupAt=0,dhcpAt=0,ncmAt=0,servicesAt=0,attachAt=0;
  bool usbInitializedAtSetup=false;
} startup;
static char deviceId[17];
constexpr int STATUS_LED_PIN=6,OUTPUT=1,LOW=0,ECHO_PORT=5001;
constexpr int FIRMINGO_NET_A=192,FIRMINGO_NET_B=168,FIRMINGO_NET_C=77;
#define IP_ADDR4(...)
uint32_t millis() { return ++tick; }
void pinMode(int,int) {} void digitalWrite(int,int) {}
void lwip_init() {}
void ethernet_arch_lwip_begin() { ++lwipLocks; }
void ethernet_arch_lwip_end() { --lwipLocks; }
void udp_bind_netif(void*,void*) {}
void firmingo_dhcp_init(Dhcp* d,FakeIP*,FakeIP*,void*) {
  events.push_back("dhcp"); d->udp=dhcpOk ? d : nullptr;
}
void pico_get_unique_board_id_string(char* text,unsigned size) { std::strncpy(text,"ABCDEF3455667788",size); }
static firmingo::Identity identity{{}, {}, "nano_rp2040_connect", FIRMINGO_VERSION};
static uint64_t boot_nonce;
uint64_t get_rand_64() { return ++boot_nonce; }
uint32_t clockMs() { return millis(); }
constexpr int PROTOCOL_PORT=7420;
struct Protocol {
  bool begin(FakeIP*, unsigned port, const firmingo::Identity& id, uint32_t(*clock)()) {
    TEST_ASSERT_EQUAL_UINT(PROTOCOL_PORT,port);
    TEST_ASSERT_TRUE(lwipLocks==1);
    TEST_ASSERT_EQUAL_STRING("abcdef3455667788",id.device_id);
    TEST_ASSERT_TRUE(id.boot_id[0]!=0 && clock!=nullptr);
    events.push_back("protocol"); return protocolOk;
  }
} protocolService;
struct StartupFailure {};
[[noreturn]] void startupFault() { throw StartupFailure{}; }
#include "usb_startup.inc"

void setUp() {
  initialized=false;
  dhcpOk=configOk=ncmOk=httpOk=sourceOk=echoOk=protocolOk=true;
  mutexInitializations=controllerStarts=connects=disconnects=serialStarts=interfaces=lwipLocks=tick=0;
  ncmQueuedPackets=ncmRenewedPackets=ncmXmitAttempts=ncmWakeRequests=0;
  usbMutexAvailable=true; usbMutexHeld=wakeRequestedWithUsbMutexHeld=false;
  _ncm_ethernet_instance=nullptr;
  USB=USBClass{}; ethernet=Ethernet{}; dhcpServer=Dhcp{}; startup=Snapshot{};
  events.clear();
}
void tearDown() {}
void usb_preparation_and_controller_start_are_idempotent() {
  USB.prepare(); USB.prepare();
  TEST_ASSERT_EQUAL_UINT(1,mutexInitializations);
  TEST_ASSERT_FALSE(initialized);
  USB.begin(); USB.begin();
  TEST_ASSERT_EQUAL_UINT(1,controllerStarts);
  TEST_ASSERT_EQUAL_UINT(1,mutexInitializations);
}
void receive_worker_reschedules_after_its_ten_packet_budget() {
  _ncm_ethernet_instance=&ethernet;
  ncmQueuedPackets=11;
  async_when_pending_worker_t worker;
  NCMEthernetlwIP::_call_irq(nullptr,&worker);
  TEST_ASSERT_EQUAL_UINT(10,ncmRenewedPackets);
  TEST_ASSERT_EQUAL_UINT(1,ncmQueuedPackets);
  TEST_ASSERT_TRUE(worker.work_pending);
  TEST_ASSERT_EQUAL_UINT(1,ncmWakeRequests);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_worker_runs);
  TEST_ASSERT_EQUAL_UINT(10,ethernet._recv_batch_peak);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_budget_exhaustions);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_wake_requests);
  TEST_ASSERT_FALSE(wakeRequestedWithUsbMutexHeld);
  TEST_ASSERT_FALSE(ethernet._holding_both_mutexes);
  worker.work_pending=false;
  NCMEthernetlwIP::_call_irq(nullptr,&worker);
  TEST_ASSERT_EQUAL_UINT(11,ncmRenewedPackets);
  TEST_ASSERT_EQUAL_UINT(0,ncmQueuedPackets);
  TEST_ASSERT_FALSE(worker.work_pending);
  TEST_ASSERT_EQUAL_UINT(1,ncmWakeRequests);
  TEST_ASSERT_EQUAL_UINT(2,ethernet._recv_worker_runs);
  TEST_ASSERT_EQUAL_UINT(10,ethernet._recv_batch_peak);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_budget_exhaustions);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_wake_requests);
  TEST_ASSERT_FALSE(ethernet._holding_both_mutexes);
}
void receive_worker_records_usb_mutex_contention_without_processing() {
  _ncm_ethernet_instance=&ethernet;
  usbMutexAvailable=false;
  async_when_pending_worker_t worker;
  NCMEthernetlwIP::_call_irq(nullptr,&worker);
  TEST_ASSERT_TRUE(worker.work_pending);
  TEST_ASSERT_EQUAL_UINT(1,ethernet._recv_mutex_contentions);
  TEST_ASSERT_EQUAL_UINT(0,ethernet._recv_worker_runs);
  TEST_ASSERT_EQUAL_UINT(0,ethernet._recv_batch_peak);
  TEST_ASSERT_EQUAL_UINT(0,ncmWakeRequests);
  TEST_ASSERT_FALSE(usbMutexHeld);
}
#ifdef FIRMINGO_USB_DEFERRED_START
void final_ncm_descriptor_attaches_after_all_services() {
  core_usb_start();
  TEST_ASSERT_FALSE(initialized);
  TEST_ASSERT_EQUAL_UINT(1,mutexInitializations);
  setup();
  TEST_ASSERT_TRUE(initialized);
  TEST_ASSERT_FALSE(startup.usbInitializedAtSetup);
  TEST_ASSERT_EQUAL_UINT(1,controllerStarts);
  TEST_ASSERT_EQUAL_UINT(2,interfaces);
  TEST_ASSERT_EQUAL_UINT(0,serialStarts);
  TEST_ASSERT_EQUAL_UINT(0,connects);
  TEST_ASSERT_EQUAL_UINT(0,disconnects);
  const std::vector<std::string> expected{"dhcp","ncm","http","source","echo","descriptors","controller"};
  TEST_ASSERT_TRUE(events==expected);
  TEST_ASSERT_TRUE(startup.dhcpAt<=startup.ncmAt && startup.ncmAt<=startup.servicesAt && startup.servicesAt<=startup.attachAt);
}
void every_startup_failure_keeps_controller_detached() {
  bool* failures[]={&dhcpOk,&configOk,&ncmOk,&httpOk,&sourceOk,&echoOk};
  for(auto* failure:failures) {
    setUp(); core_usb_start(); *failure=false;
    bool fault=false;
    try { setup(); } catch(StartupFailure&) { fault=true; }
    TEST_ASSERT_TRUE(fault);
    TEST_ASSERT_FALSE(initialized);
    TEST_ASSERT_EQUAL_UINT(0,controllerStarts);
    TEST_ASSERT_EQUAL_UINT(0,lwipLocks);
  }
}
void application_reference_attaches_only_after_protocol_listener() {
  core_usb_start(); reference_setup();
  const std::vector<std::string> expected{"dhcp","ncm","protocol","descriptors","controller"};
  TEST_ASSERT_TRUE(events==expected);
  TEST_ASSERT_EQUAL_UINT(1,controllerStarts);
  TEST_ASSERT_EQUAL_UINT(0,connects+disconnects+serialStarts);
  TEST_ASSERT_EQUAL_UINT(2,interfaces);
}
void application_startup_failures_keep_usb_detached() {
  bool* failures[]={&dhcpOk,&configOk,&ncmOk,&protocolOk};
  for(auto* failure:failures) {
    setUp(); core_usb_start(); *failure=false; bool fault=false;
    try { reference_setup(); } catch(StartupFailure&) { fault=true; }
    TEST_ASSERT_TRUE(fault); TEST_ASSERT_FALSE(initialized);
    TEST_ASSERT_EQUAL_UINT(0,controllerStarts+lwipLocks);
  }
  setUp(); core_usb_start(); USB.begin();
  bool fault=false; try { reference_setup(); } catch(StartupFailure&) { fault=true; }
  TEST_ASSERT_TRUE(fault); TEST_ASSERT_EQUAL_UINT(0,interfaces);
}
void boot_nonce_changes_without_changing_device_identity() {
  prepareIdentity(); std::string first=identity.boot_id;
  prepareIdentity(); TEST_ASSERT_TRUE(first!=identity.boot_id);
  TEST_ASSERT_EQUAL_STRING("abcdef3455667788",identity.device_id);
  TEST_ASSERT_EQUAL_UINT(16,std::strlen(identity.boot_id));
}
void late_ncm_registration_is_rejected_without_reconnect() {
  core_usb_start(); USB.begin();
  TEST_ASSERT_FALSE(ethernet.begin());
  TEST_ASSERT_EQUAL_UINT(0,interfaces);
  TEST_ASSERT_EQUAL_UINT(0,connects);
  TEST_ASSERT_EQUAL_UINT(0,disconnects);
}
#else
void default_core_still_starts_usb_and_cdc_before_setup() {
  core_usb_start();
  TEST_ASSERT_TRUE(initialized);
  TEST_ASSERT_EQUAL_UINT(1,controllerStarts);
  TEST_ASSERT_EQUAL_UINT(1,serialStarts);
  TEST_ASSERT_TRUE(ethernet.begin());
  TEST_ASSERT_EQUAL_UINT(1,disconnects);
  TEST_ASSERT_EQUAL_UINT(1,connects);
  TEST_ASSERT_EQUAL_UINT(2,interfaces);
}
#endif
int main() {
  UNITY_BEGIN();
  RUN_TEST(usb_preparation_and_controller_start_are_idempotent);
  RUN_TEST(receive_worker_reschedules_after_its_ten_packet_budget);
  RUN_TEST(receive_worker_records_usb_mutex_contention_without_processing);
#ifdef FIRMINGO_USB_DEFERRED_START
  RUN_TEST(final_ncm_descriptor_attaches_after_all_services);
  RUN_TEST(every_startup_failure_keeps_controller_detached);
  RUN_TEST(application_reference_attaches_only_after_protocol_listener);
  RUN_TEST(application_startup_failures_keep_usb_detached);
  RUN_TEST(boot_nonce_changes_without_changing_device_identity);
  RUN_TEST(late_ncm_registration_is_rejected_without_reconnect);
#else
  RUN_TEST(default_core_still_starts_usb_and_cdc_before_setup);
#endif
  return UNITY_END();
}
