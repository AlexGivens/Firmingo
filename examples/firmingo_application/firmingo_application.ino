// Minimal Firmingo application-stream reference: NCM, local-only DHCP and FMGO.
#include <Arduino.h>
#include <NCMEthernetlwIP.h>
#include <FirmingoLocalNetwork.h>
#include <core/application.h>
#include <core/version.h>
#include <ports/arduino_pico/board_identity.h>
#include <ports/arduino_pico/tcp_session.h>
#include <LwipEthernet.h>
#include <lwip/init.h>
#include <lwip/udp.h>
#include <USB.h>
#include <pico/unique_id.h>
#include <pico/rand.h>

#if !defined(FIRMINGO_USB_DEFERRED_START) || !defined(DISABLE_USB_SERIAL) || !defined(FIRMINGO_USB_STARTUP_PATCH_VERSION)
#error "Build with tools/dev.py: requires the pinned deferred NCM-only core"
#endif
namespace {
constexpr uint16_t PROTOCOL_PORT = 7420;
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;
const IPAddress PICO_IP(FIRMINGO_NET_A, FIRMINGO_NET_B, FIRMINGO_NET_C, 1);
const IPAddress SUBNET_MASK(255,255,255,0), NO_GATEWAY(0,0,0,0);
NCMEthernetlwIP ethernet;
firmingo_dhcp_server_t dhcpServer;
ip_addr_t dhcpAddress, dhcpNetmask;
// Application code consumes and produces bytes through this bounded endpoint.
firmingo::ApplicationEndpoint backend;
firmingo::Channel channel(backend);
firmingo::MemorySamples memorySamples;
firmingo::TcpSessionServer protocolService(channel,&memorySamples);
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION};
uint8_t applicationBytes[firmingo::ApplicationEndpoint::capacity];
uint32_t clockMs() { return millis(); }

void sampleRuntime() {
  const int heap = rp2040.getFreeHeap(), stack = rp2040.getFreeStack();
  if (heap >= 0 && stack >= 0) memorySamples.observe(heap,stack);
  memorySamples.observe_transport({
      ethernet.receiveWorkerRuns(), ethernet.receivedFrames(),
      ethernet.deferredReceiveCallbacks(), ethernet.receiveBatchPeak(),
      ethernet.receiveMutexContentions(), ethernet.receiveBudgetExhaustions(),
      ethernet.receiveWakeRequests(),
      protocolService.accepts(),
      protocolService.receive_callbacks(), protocolService.sent_callbacks(),
      protocolService.errors(), protocolService.received_bytes(),
      protocolService.sent_bytes()});
}

void prepareIdentity() {
  pico_get_unique_board_id_string(identity.device_id, sizeof(identity.device_id));
  // SDK board-ID formatter uses uppercase; wire identity requires lowercase.
  for (char& c : identity.device_id) if (c >= 'A' && c <= 'F') c += 'a'-'A';
  const uint64_t boot = get_rand_64(); // One boot nonce, shared by all connections.
  snprintf(identity.boot_id, sizeof(identity.boot_id), "%08lx%08lx",
           static_cast<unsigned long>(boot >> 32),
           static_cast<unsigned long>(static_cast<uint32_t>(boot)));
}
[[noreturn]] void startupFault() {
  while (true) {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    delay(100); // Startup failed; USB is never attached.
  }
}
}

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  if (tusb_inited()) startupFault();
  prepareIdentity();
  IP_ADDR4(&dhcpAddress,FIRMINGO_NET_A,FIRMINGO_NET_B,FIRMINGO_NET_C,1);
  IP_ADDR4(&dhcpNetmask,255,255,255,0);
  lwip_init();
  ethernet_arch_lwip_begin();
  firmingo_dhcp_init(&dhcpServer,&dhcpAddress,&dhcpNetmask,nullptr);
  const bool dhcpReady = dhcpServer.udp != nullptr;
  ethernet_arch_lwip_end();
  if (!dhcpReady) startupFault();

  ethernet_arch_lwip_begin();
  const bool configured = ethernet.config(PICO_IP,NO_GATEWAY,SUBNET_MASK,NO_GATEWAY);
  const bool ncmReady = configured && ethernet.begin();
  if (ncmReady) udp_bind_netif(dhcpServer.udp,ethernet.getNetIf());
  ethernet_arch_lwip_end();
  if (!ncmReady) startupFault();

  ethernet_arch_lwip_begin();
  const bool protocolReady = protocolService.begin(&dhcpAddress,PROTOCOL_PORT,identity,clockMs);
  ethernet_arch_lwip_end();
  if (!protocolReady) startupFault();
  USB.begin(); // One final NCM-only attachment, with DHCP/listener already ready.
}

void loop() {
  // Mounted hint only; does not establish DHCP, a session or Internet routing.
  digitalWrite(STATUS_LED_PIN,ethernet.linkStatus() == LinkON ? HIGH : LOW);
  ethernet_arch_lwip_begin();
  // Sample the C heap and approximate current core-0 stack before/after work.
  // lwIP has a separate fixed pool; these samples do not measure its usage.
  sampleRuntime();
  protocolService.poll(millis());
  sampleRuntime();
  ethernet_arch_lwip_end();

  // Reference application: explicitly consume and reproduce opaque bytes.
  // Read no more than the outbound queue can accept, so saturation is
  // backpressure rather than loss.
  const size_t count = backend.read_from_host(
      applicationBytes, min(sizeof(applicationBytes), backend.available_for_write()));
  if (count && backend.write_to_host(applicationBytes,count) != count) startupFault();
  delay(1);
}
