/*
  Arduino Nano RP2040 Connect USB-NCM proof of concept for iPhone/iPad

  Board target: Arduino Nano RP2040 Connect (ABX00052/ABX00053).
  Requires Earle F. Philhower's Arduino-Pico core 5.6.0 or newer.
  Tested API target: Arduino-Pico 6.0.0.

  USB services:
    - CDC serial console (provided by the core)
    - USB Network Control Model (NCM) Ethernet

  Network services:
    - DHCP server: Pico 192.168.7.1, host lease 192.168.7.16+
    - HTTP diagnostic page: http://192.168.7.1/
    - Deterministic binary source stream: TCP port 5000
    - Exact byte echo: TCP port 5001

  This is proof-of-concept code. The DHCP server interface used here is an
  internal Arduino-Pico API and should be wrapped or vendored for production.
*/

#include <Arduino.h>
#include <NCMEthernetlwIP.h>
#include <WiFi.h>
#include <dhcpserver/dhcpserver.h>
#include <lwip/init.h>
#include <lwip/udp.h>

#if !defined(ARDUINO_NANO_RP2040_CONNECT)
#error "Select Earle Philhower core: Arduino Nano RP2040 Connect"
#endif

namespace {

constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t SOURCE_PORT = 5000;
constexpr uint16_t ECHO_PORT = 5001;
constexpr uint32_t STREAM_INTERVAL_MS = 50;
constexpr size_t PAYLOAD_SIZE = 256;
constexpr size_t RECORD_SIZE = 4 + 4 + 4 + 2 + 2 + PAYLOAD_SIZE;

const IPAddress PICO_IP(192, 168, 7, 1);
const IPAddress SUBNET_MASK(255, 255, 255, 0);
const IPAddress NO_GATEWAY(0, 0, 0, 0);

// The Nano RP2040 Connect variant maps LED_BUILTIN to D13 / RP2040 GPIO 6.
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;

NCMEthernetlwIP ethernet;
dhcp_server_t dhcpServer;
ip_addr_t dhcpAddress;
ip_addr_t dhcpNetmask;

WiFiServer httpServer(HTTP_PORT);
WiFiServer sourceServer(SOURCE_PORT);
WiFiServer echoServer(ECHO_PORT);

WiFiClient browserStreamClient;
WiFiClient sourceClient;
WiFiClient echoClient;

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
    "<title>Nano RP2040 Connect USB NCM test</title>"
    "<style>body{font:17px system-ui;margin:2rem;max-width:44rem}"
    "code,output{font-family:ui-monospace,monospace}"
    ".ok{color:#087f23}.bad{color:#b42318}</style>"
    "<h1>Nano RP2040 Connect USB Ethernet</h1>"
    "<p>If this page loaded over <code>192.168.7.1</code>, USB NCM, DHCP, IP, TCP, and HTTP are working.</p>"
    "<p>Status: <output id=s>starting byte stream...</output></p>"
    "<p>Body bytes received: <output id=b>0</output></p>"
    "<p>Current rate: <output id=r>0</output> bytes/s</p>"
    "<p>Rolling checksum: <output id=c>0</output></p>"
    "<button onclick='location.reload()'>Restart test</button>"
    "<script>"
    "let bytes=0,sum=0,lastBytes=0,last=performance.now();"
    "const s=document.querySelector('#s'),b=document.querySelector('#b'),r=document.querySelector('#r'),c=document.querySelector('#c');"
    "setInterval(()=>{const now=performance.now();r.textContent=Math.round((bytes-lastBytes)*1000/(now-last));last=now;lastBytes=bytes},1000);"
    "(async()=>{try{const response=await fetch('/stream',{cache:'no-store'});if(!response.ok)throw Error('HTTP '+response.status);"
    "s.textContent='receiving';s.className='ok';const reader=response.body.getReader();"
    "for(;;){const x=await reader.read();if(x.done)throw Error('stream ended');bytes+=x.value.length;"
    "for(const v of x.value)sum=(sum+v)>>>0;b.textContent=bytes;c.textContent=sum.toString(16).padStart(8,'0')}}"
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

  if (strncmp(requestLine, "GET / ", 6) == 0) {
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

  WiFiClient incomingEcho = echoServer.accept();
  if (incomingEcho) {
    if (echoClient) {
      echoClient.stop();
    }
    echoClient = incomingEcho;
    echoClient.setNoDelay(true);
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
  if (!echoClient || !echoClient.connected()) {
    echoClient.stop();
    return;
  }

  uint8_t buffer[256];
  while (echoClient.available()) {
    const int count = echoClient.read(buffer, sizeof(buffer));
    if (count <= 0) {
      break;
    }
    echoClient.write(buffer, static_cast<size_t>(count));
  }
}

}  // namespace

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Prepare lwIP and open the DHCP listener before ethernet.begin() exposes
  // the NCM interface to the USB host. This prevents iPadOS from sending its
  // first DHCP request during the short interval when no server is listening.
  // The listener initially accepts packets on any interface because the NCM
  // netif does not exist until ethernet.begin() completes.
  IP_ADDR4(&dhcpAddress, 192, 168, 7, 1);
  IP_ADDR4(&dhcpNetmask, 255, 255, 255, 0);
  lwip_init();
  dhcp_server_init(
    &dhcpServer,
    &dhcpAddress,
    &dhcpNetmask,
    nullptr
  );

  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println(F("Nano RP2040 Connect USB NCM byte-stream proof of concept"));

  if (dhcpServer.udp == nullptr) {
    Serial.println(F("ERROR: unable to start DHCP server"));
    while (true) {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(100);
    }
  }

  // The Pico is the endpoint, not an Internet router. The bundled DHCP server
  // nevertheless advertises the Nano as gateway/DNS; this is acceptable for a
  // direct proof of concept but should be customized in production.
  ethernet.config(PICO_IP, NO_GATEWAY, SUBNET_MASK, PICO_IP);
  if (!ethernet.begin()) {
    Serial.println(F("ERROR: unable to start USB NCM"));
    while (true) {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(100);
    }
  }

  // Now that the NCM netif exists, restrict the already-running DHCP listener
  // to that interface. It was deliberately started before USB enumeration.
  dhcpServer.netif = ethernet.getNetIf();
  udp_bind_netif(dhcpServer.udp, ethernet.getNetIf());

  httpServer.begin();
  sourceServer.begin();
  echoServer.begin();

  Serial.println(F("USB NCM started"));
  Serial.println(F("Safari test: http://192.168.7.1/"));
  Serial.println(F("Raw source: 192.168.7.1:5000"));
  Serial.println(F("Raw echo:   192.168.7.1:5001"));
}

void loop() {
  // Solid LED means the USB host has configured the NCM interface.
  digitalWrite(STATUS_LED_PIN, ethernet.linkStatus() == LinkON ? HIGH : LOW);

  serviceHttp();
  serviceBrowserStream();
  acceptRawClients();
  serviceRawSource();
  serviceEcho();
  delay(1);
}
