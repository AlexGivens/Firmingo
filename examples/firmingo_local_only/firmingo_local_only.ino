// Local-only qualification fork of the preserved baseline.
// Local-only DHCP and a bounded raw-lwIP echo qualification path.
/*
  RP2040 local-only USB-NCM qualification sketch for iPhone/iPad

  Board targets: Nano RP2040 Connect and Raspberry Pi Pico, through explicit
  board manifests. Requires pinned Arduino-Pico 6.0.0.

  USB services:
    - USB Network Control Model (NCM) Ethernet

  Network services:
    - DHCP server: Pico 192.168.77.1, host lease 192.168.77.16+
    - HTTP diagnostic page: http://192.168.77.1/
    - Deterministic binary source stream: TCP port 5000
    - Exact byte echo: TCP port 5001

  This is qualification firmware. DHCP is vendored in Firmingo; stream and
  USB startup is deferred by the pinned Firmingo patch; hardware qualification
  is still required.
*/

#include <Arduino.h>
#include <NCMEthernetlwIP.h>
#include <WiFi.h>
#include <FirmingoLocalNetwork.h>
#include <core/version.h>
#include <ports/arduino_pico/board_identity.h>
#include <ports/arduino_pico/tcp_echo.h>
#include <LwipEthernet.h>
#include <lwip/init.h>
#include <lwip/udp.h>
#include <USB.h>
#include <pico/unique_id.h>

#if !defined(FIRMINGO_USB_DEFERRED_START) || !defined(DISABLE_USB_SERIAL) || !defined(FIRMINGO_USB_STARTUP_PATCH_VERSION)
#error "Build with tools/dev.py: qualification firmware requires the pinned deferred NCM-only USB core"
#endif

namespace {

constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t SOURCE_PORT = 5000;
constexpr uint16_t ECHO_PORT = 5001;
constexpr uint32_t STREAM_INTERVAL_MS = 50;
constexpr size_t PAYLOAD_SIZE = 256;
constexpr size_t RECORD_SIZE = 4 + 4 + 4 + 2 + 2 + PAYLOAD_SIZE;

const IPAddress PICO_IP(FIRMINGO_NET_A, FIRMINGO_NET_B, FIRMINGO_NET_C, 1);
const IPAddress SUBNET_MASK(255, 255, 255, 0);
const IPAddress NO_GATEWAY(0, 0, 0, 0);

constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;

NCMEthernetlwIP ethernet;
firmingo_dhcp_server_t dhcpServer;
ip_addr_t dhcpAddress;
ip_addr_t dhcpNetmask;

WiFiServer httpServer(PICO_IP, HTTP_PORT);
WiFiServer sourceServer(PICO_IP, SOURCE_PORT);
firmingo::TcpEcho echoService;

WiFiClient browserStreamClient;
WiFiClient sourceClient;


struct StartupSnapshot {
  uint32_t setupAt = 0, dhcpAt = 0, ncmAt = 0, servicesAt = 0, attachAt = 0;
  bool usbInitializedAtSetup = false;
} startup;
char deviceId[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

// Before networking, a 100 ms fault blink means startup failed. No USB attach
// follows failure. The normal LED remains a USB-mounted hint only.
[[noreturn]] void startupFault() {
  while (true) {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    delay(100);
  }
}

void sendDiagnostics(WiFiClient& client) {
  char body[512];
  const int length = snprintf(body, sizeof(body),
    "{\"firmware\":\"" FIRMINGO_VERSION "\",\"profile\":\"local-only\",\"board\":\"" FIRMINGO_BOARD_ID "\","
    "\"device_id\":\"%s\",\"usb_profile\":\"ncm-only\","
    "\"usb_initialized_at_setup\":%s,"
    "\"setup_ms\":%lu,\"dhcp_ready_ms\":%lu,\"ncm_ready_ms\":%lu,"
    "\"services_ready_ms\":%lu,\"attach_requested_ms\":%lu}\n",
    deviceId, startup.usbInitializedAtSetup ? "true" : "false",
    static_cast<unsigned long>(startup.setupAt), static_cast<unsigned long>(startup.dhcpAt),
    static_cast<unsigned long>(startup.ncmAt), static_cast<unsigned long>(startup.servicesAt),
    static_cast<unsigned long>(startup.attachAt));
  if (length <= 0 || length >= static_cast<int>(sizeof(body))) return;
  client.print(F("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: "));
  client.print(length);
  client.print(F("\r\nConnection: close\r\n\r\n"));
  client.write(reinterpret_cast<const uint8_t*>(body), static_cast<size_t>(length));
}

uint32_t browserSequence = 0;
uint32_t sourceSequence = 0;
uint32_t nextBrowserRecordAt = 0;
uint32_t nextSourceRecordAt = 0;

void putLE16(uint8_t *destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void putLE32(uint8_t *destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}

void buildRecord(uint8_t *record, uint32_t sequence) {
  record[0] = 'P';
  record[1] = 'I';
  record[2] = 'C';
  record[3] = 'O';
  putLE32(record + 4, sequence);
  putLE32(record + 8, millis());
  putLE16(record + 12, PAYLOAD_SIZE);
  putLE16(record + 14, 0);  // Reserved flags.

  for (size_t i = 0; i < PAYLOAD_SIZE; ++i) {
    record[16 + i] = static_cast<uint8_t>(i ^ sequence);
  }
}

bool readRequestLine(WiFiClient &client, char *line, size_t capacity) {
  const uint32_t deadline = millis() + 750;
  size_t used = 0;

  while (static_cast<int32_t>(deadline - millis()) > 0) {
    while (client.available()) {
      const int value = client.read();
      if (value < 0) {
        break;
      }
      if (value == '\n') {
        line[used] = '\0';
        return true;
      }
      if (value != '\r' && used + 1 < capacity) {
        line[used++] = static_cast<char>(value);
      }
    }
    delay(1);
  }

  line[used] = '\0';
  return false;
}

void drainHttpHeaders(WiFiClient &client) {
  const uint32_t deadline = millis() + 250;
  uint8_t newlines = 0;

  while (static_cast<int32_t>(deadline - millis()) > 0 && newlines < 2) {
    while (client.available()) {
      const int value = client.read();
      if (value == '\n') {
        ++newlines;
      } else if (value != '\r') {
        newlines = 0;
      }
    }
    delay(1);
  }
}

void sendDiagnosticPage(WiFiClient &client) {
  client.print(F(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n\r\n"
    "<!doctype html><meta name='viewport' content='width=device-width'>"
    "<title>Firmingo local-only network qualification</title>"
    "<style>body{font:17px system-ui;margin:2rem;max-width:44rem}"
    "code,output{font-family:ui-monospace,monospace}"
    ".ok{color:#087f23}.bad{color:#b42318}</style>"
    "<h1>Firmingo local-only USB Ethernet</h1>"
    "<p>Local-only qualification firmware. This page validates every deterministic stream record; check DHCP separately.</p>"
    "<p>Status: <output id=s>starting exact-byte stream...</output></p>"
    "<p>Validated records: <output id=n>0</output></p>"
    "<p>Validated bytes: <output id=b>0</output></p>"
    "<p>Current rate: <output id=r>0</output> bytes/s</p>"
    "<p>Next sequence: <output id=q>0</output></p>"
    "<button onclick='location.reload()'>Restart test</button>"
    "<script>"
    "let bytes=0,records=0,expected=0,lastBytes=0,last=performance.now(),pending=new Uint8Array(0);"
    "const s=document.querySelector('#s'),n=document.querySelector('#n'),b=document.querySelector('#b'),r=document.querySelector('#r'),q=document.querySelector('#q');"
    "const u16=(a,o)=>a[o]|a[o+1]<<8;"
    "const u32=(a,o)=>(a[o]|a[o+1]<<8|a[o+2]<<16|a[o+3]<<24)>>>0;"
    "function accept(chunk){const all=new Uint8Array(pending.length+chunk.length);all.set(pending);all.set(chunk,pending.length);let o=0;"
    "while(all.length-o>=272){if(all[o]!=80||all[o+1]!=73||all[o+2]!=67||all[o+3]!=79)throw Error('bad magic at validated byte '+bytes);"
    "const seq=u32(all,o+4);if(seq!==expected)throw Error('sequence '+seq+' expected '+expected);"
    "if(u16(all,o+12)!==256||u16(all,o+14)!==0)throw Error('bad record header at sequence '+seq);"
    "for(let i=0;i<256;i++)if(all[o+16+i]!==((i^seq)&255))throw Error('payload mismatch at sequence '+seq+' offset '+i);"
    "o+=272;bytes+=272;records++;expected=(expected+1)>>>0;}pending=all.slice(o);n.textContent=records;b.textContent=bytes;q.textContent=expected;"
    "if(records){s.textContent='exact records validated';s.className='ok'}}"
    "setInterval(()=>{const now=performance.now();r.textContent=Math.round((bytes-lastBytes)*1000/(now-last));last=now;lastBytes=bytes},1000);"
    "(async()=>{try{const response=await fetch('/stream',{cache:'no-store'});if(!response.ok)throw Error('HTTP '+response.status);"
    "const reader=response.body.getReader();for(;;){const x=await reader.read();if(x.done)throw Error('stream ended with '+pending.length+' trailing bytes');accept(x.value)}}"
    "catch(e){s.textContent=e.message;s.className='bad'}})();"
    "</script>"
  ));
}

void beginBrowserStream(WiFiClient &client) {
  if (browserStreamClient) {
    browserStreamClient.stop();
  }
  browserStreamClient = client;
  browserStreamClient.print(F(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/octet-stream\r\n"
    "Cache-Control: no-store\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Connection: keep-alive\r\n\r\n"
  ));
  browserSequence = 0;
  nextBrowserRecordAt = millis();
}

void serviceHttp() {
  WiFiClient incoming = httpServer.accept();
  if (!incoming) {
    return;
  }

  incoming.setNoDelay(true);
  char requestLine[96];
  if (!readRequestLine(incoming, requestLine, sizeof(requestLine))) {
    incoming.stop();
    return;
  }
  drainHttpHeaders(incoming);

  if (strncmp(requestLine, "GET /stream ", 12) == 0) {
    beginBrowserStream(incoming);
    return;
  }

  if (strncmp(requestLine, "GET /diagnostics ", 17) == 0) {
    sendDiagnostics(incoming);
  } else if (strncmp(requestLine, "GET / ", 6) == 0) {
    sendDiagnosticPage(incoming);
  } else {
    incoming.print(F(
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 10\r\n"
      "Connection: close\r\n\r\n"
      "not found\n"
    ));
  }
  delay(2);
  incoming.stop();
}

void serviceBrowserStream() {
  if (!browserStreamClient || !browserStreamClient.connected()) {
    browserStreamClient.stop();
    return;
  }
  if (static_cast<int32_t>(millis() - nextBrowserRecordAt) < 0) {
    return;
  }

  uint8_t record[RECORD_SIZE];
  buildRecord(record, browserSequence++);

  // HTTP chunk: hexadecimal length, CRLF, bytes, CRLF.
  browserStreamClient.print(RECORD_SIZE, HEX);
  browserStreamClient.print("\r\n");
  browserStreamClient.write(record, sizeof(record));
  browserStreamClient.print("\r\n");
  nextBrowserRecordAt = millis() + STREAM_INTERVAL_MS;
}

void acceptRawClients() {
  WiFiClient incomingSource = sourceServer.accept();
  if (incomingSource) {
    if (sourceClient) {
      sourceClient.stop();
    }
    sourceClient = incomingSource;
    sourceClient.setNoDelay(true);
    sourceSequence = 0;
    nextSourceRecordAt = millis();
  }


}

void serviceRawSource() {
  if (!sourceClient || !sourceClient.connected()) {
    sourceClient.stop();
    return;
  }
  if (static_cast<int32_t>(millis() - nextSourceRecordAt) < 0) {
    return;
  }
  if (sourceClient.availableForWrite() < static_cast<int>(RECORD_SIZE)) {
    return;
  }

  uint8_t record[RECORD_SIZE];
  buildRecord(record, sourceSequence++);
  sourceClient.write(record, sizeof(record));
  nextSourceRecordAt = millis() + STREAM_INTERVAL_MS;
}

void serviceEcho() {
  ethernet_arch_lwip_begin();
  echoService.poll(millis());
  ethernet_arch_lwip_end();
}

}  // namespace

void setup() {
  startup.setupAt = millis();
  startup.usbInitializedAtSetup = tusb_inited();
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  if (startup.usbInitializedAtSetup) startupFault();
  pico_get_unique_board_id_string(deviceId, sizeof(deviceId));

  // The patched core prepared USB synchronization before setup, leaving the
  // controller off. Prepare DHCP first; bind it to NCM after the netif exists.
  IP_ADDR4(&dhcpAddress, FIRMINGO_NET_A, FIRMINGO_NET_B, FIRMINGO_NET_C, 1);
  IP_ADDR4(&dhcpNetmask, 255, 255, 255, 0);
  lwip_init();
  ethernet_arch_lwip_begin();
  firmingo_dhcp_init(&dhcpServer, &dhcpAddress, &dhcpNetmask, nullptr);
  const bool dhcpReady = dhcpServer.udp != nullptr;
  ethernet_arch_lwip_end();
  if (!dhcpReady) startupFault();
  startup.dhcpAt = millis();

  // The patched NCM begin registers final descriptors without disconnecting
  // or connecting USB. Hold the lwIP lock while constructing its netif.
  ethernet_arch_lwip_begin();
  const bool configured = ethernet.config(PICO_IP, NO_GATEWAY, SUBNET_MASK, NO_GATEWAY);
  const bool ncmReady = configured && ethernet.begin();
  if (ncmReady) udp_bind_netif(dhcpServer.udp, ethernet.getNetIf());
  ethernet_arch_lwip_end();
  if (!ncmReady) startupFault();
  startup.ncmAt = millis();

  ethernet_arch_lwip_begin();
  httpServer.begin();
  sourceServer.begin();
  const bool echoReady = echoService.begin(&dhcpAddress, ECHO_PORT);
  const bool servicesReady = static_cast<bool>(httpServer) && static_cast<bool>(sourceServer) && echoReady;
  ethernet_arch_lwip_end();
  if (!servicesReady) startupFault();
  startup.servicesAt = millis();

  // One controller initialization exposes the final NCM-only descriptor.
  // No CDC, delay, terminal-open dependency, or explicit USB reconnect.
  startup.attachAt = millis();
  USB.begin();
}

void loop() {
  // USB-mounted hint only; does not establish NCM activation, DHCP or routing.
  digitalWrite(STATUS_LED_PIN, ethernet.linkStatus() == LinkON ? HIGH : LOW);

  serviceHttp();
  serviceBrowserStream();
  acceptRawClients();
  serviceRawSource();
  serviceEcho();
  delay(1);
}
