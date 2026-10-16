// Experimental managed-sketch resident: never package as a 0.1.0 release image.
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
#include <hardware/flash.h>
#include <hardware/sync.h>
#ifdef FIRMINGO_FLASH_CHECKPOINT
#include <hardware/gpio.h>
#include <hardware/timer.h>
#if !defined(FIRMINGO_STATEFUL_UPLOAD_PROOF) || (FIRMINGO_FLASH_CHECKPOINT != 1 && FIRMINGO_FLASH_CHECKPOINT != 2)
#error "Flash checkpoints require an explicit FMS2 upload fault-test build"
#endif
// Retained ELF facts distinguish these destructive test images from normal residents.
extern "C" __attribute__((used)) const uint32_t firmingo_flash_checkpoint_contract[4] = {
    0x31504346, 1, FIRMINGO_FLASH_CHECKPOINT, 60000};
#endif
#include <atomic>
#include <abi.h>
#ifdef FIRMINGO_STATEFUL_ALLOCATION_PROOF
#include <stateful_abi.h>
#endif
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
#include <stateful_runtime.h>
#include <stateful_image.cpp>
#include <stateful_runtime.cpp>
#endif
#ifdef FIRMINGO_STATEFUL_UPLOAD_PROOF
#include <stateful_upload.h>
#include <stateful_upload.cpp>
#endif
#include <image.h>
#include <console.h>
#include <stage.h>
#include <install.h>
#include <switch.h>
#include <fmgo_upload.h>
// Compile the same validation code used by the native image tests.
#include <image.cpp>
#include <stage.cpp>
#include <install.cpp>
#define FIRMINGO_MANAGED_EMBEDDED 1
#include <core1_pause.cpp>
#include <switch.cpp>
#include <fmgo_upload.cpp>

#if !defined(FIRMINGO_USB_DEFERRED_START) || !defined(DISABLE_USB_SERIAL) || !defined(FIRMINGO_USB_STARTUP_PATCH_VERSION)
#error "Build with the pinned deferred NCM-only Arduino-Pico 6.0.0 overlay"
#endif
extern "C" const FirmingoSketchApiV1 firmingo_sketch_api;
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
#ifndef FIRMINGO_STATEFUL_ALLOCATION_PROOF
#error "Stateful runtime proof requires the owned arena and candidate API"
#endif
extern "C" const FirmingoSketchApiV2 firmingo_sketch_api_v2;
extern "C" uint8_t firmingo_sketch_state[kFirmingoStateCapacityBytes];
#endif
namespace {
constexpr uint16_t PROTOCOL_PORT = 7420;
// Arduino-Pico uses RP2040 GPIO numbers in pinMode/digitalRead. On this
// Nano variant D2 is GPIO25; numeric 2 instead selects the D20/A6 signal.
static_assert(D2 == 25u, "Nano RP2040 Connect D2 mapping changed");
constexpr uint8_t PROOF_ARM_PIN = D2;  // D2-to-GND arms a network upload.
constexpr uint8_t STATUS_LED_PIN = LED_BUILTIN;
const IPAddress PICO_IP(FIRMINGO_NET_A, FIRMINGO_NET_B, FIRMINGO_NET_C, 1);
const IPAddress SUBNET_MASK(255,255,255,0), NO_GATEWAY(0,0,0,0);
NCMEthernetlwIP ethernet;
firmingo_dhcp_server_t dhcpServer;
ip_addr_t dhcpAddress, dhcpNetmask;
firmingo_managed::SketchConsole sketchConsole;
class ManagedApplicationEndpoint final : public firmingo::ApplicationEndpoint {
 public:
  bool diagnostics(firmingo::BackendDiagnostics& value) const override {
    if (!firmingo::ApplicationEndpoint::diagnostics(value)) return false;
    const auto counters = sketchConsole.counters();
    value.has_sketch_console = true;
    value.sketch_input_discarded = counters.input_discarded;
    value.sketch_output_discarded = counters.output_discarded;
    value.sketch_output_rejected = counters.output_rejected;
    value.sketch_output_peak = counters.output_peak;
    value.sketch_loop_boundaries = counters.loop_boundaries;
    value.sketch_epoch = counters.epoch;
    value.sketch_acknowledged_epoch = counters.acknowledged_epoch;
    value.sketch_input_enabled = counters.input_enabled;
    value.sketch_output_enabled = counters.output_enabled;
    return true;
  }
};
// Application code consumes and produces bytes through this bounded endpoint.
ManagedApplicationEndpoint backend;
firmingo::Channel channel(backend);
firmingo::MemorySamples memorySamples;
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
#ifdef FIRMINGO_STATEFUL_UPLOAD_PROOF
#if defined(FIRMINGO_FLASH_CHECKPOINT) && FIRMINGO_FLASH_CHECKPOINT == 1
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION "-statefulflash-erase-exp1"};
#elif defined(FIRMINGO_FLASH_CHECKPOINT) && FIRMINGO_FLASH_CHECKPOINT == 2
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION "-statefulflash-page-exp1"};
#else
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION "-statefulupload-exp1"};
#endif
#else
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION "-statefulboot-exp1"};
#endif
#else
firmingo::Identity identity{{},{},FIRMINGO_BOARD_ID,FIRMINGO_VERSION "-m9exp1"};
#endif
uint8_t applicationBytes[firmingo::ApplicationEndpoint::capacity];
firmingo_managed::Core1Pause sketchPause;
#ifndef FIRMINGO_STATEFUL_RUNTIME_PROOF
firmingo_managed::CapsuleStage capsuleStage;
std::atomic<bool> sketchReady{false};
std::atomic<uint32_t> sketchGeneration{0};
std::atomic<uint32_t> sketchReturnedGeneration{0};
firmingo_managed::ImageInfo sketchImage{};
#endif
static_assert(FLASH_SECTOR_SIZE == firmingo_managed::kProofSectorBytes,
              "Nano proof sector size changed");
static_assert(FLASH_PAGE_SIZE == firmingo_managed::kProofPageBytes,
              "Nano proof page size changed");
class NanoFlash final : public firmingo_managed::ProofFlashPort {
 public:
  bool begin() override {
    if (active_) return false;
    irq_state_ = save_and_disable_interrupts();
    active_ = true;
    return true;
  }
  void end() override {
    if (!active_) return;
    active_ = false;
    restore_interrupts(irq_state_);
  }
  bool erase(uint32_t offset, size_t size) override {
    if (!active_ || offset != firmingo_managed::kSlotAddress -
                                 firmingo_managed::kFlashXipAddress ||
        size != firmingo_managed::kProofSectorBytes) return false;
    flash_range_erase(offset, size);
#if defined(FIRMINGO_FLASH_CHECKPOINT) && FIRMINGO_FLASH_CHECKPOINT == 1
    return hold_after_completed_command();
#endif
    return true;
  }
  bool program(uint32_t offset, const uint8_t* page, size_t size) override {
    const uint32_t start = firmingo_managed::kSlotAddress -
                           firmingo_managed::kFlashXipAddress;
    const uintptr_t source = reinterpret_cast<uintptr_t>(page);
    if (!active_ || !page || size != firmingo_managed::kProofPageBytes ||
        offset < start || offset - start > firmingo_managed::kProofSectorBytes - size ||
        (offset - start) % size || source < 0x20000000 ||
        source > 0x20042000 - size)
      return false;
    flash_range_program(offset, page, size);
#if defined(FIRMINGO_FLASH_CHECKPOINT) && FIRMINGO_FLASH_CHECKPOINT == 2
    if (offset == start) return hold_after_completed_command();
#endif
    return true;
  }
  const uint8_t* slot_data() const override {
    return reinterpret_cast<const uint8_t*>(firmingo_managed::kSlotAddress);
  }
 private:
#ifdef FIRMINGO_FLASH_CHECKPOINT
  // XIP has already resumed when this hook runs. Core 1 stays parked and IRQs
  // stay masked; hardware-timer busy waits need neither interrupts nor USB.
  // A blinking D13 marks an unfinished installation BETWEEN chip commands.
  // Expiry returns failure, so no later erase/program command can be issued.
  bool hold_after_completed_command() {
    static_assert(LED_BUILTIN == 6u, "Nano D13 checkpoint mapping changed");
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    for (unsigned pulse = 0; pulse < 120; ++pulse) {
      gpio_put(STATUS_LED_PIN, true);
      busy_wait_us_32(250000);
      gpio_put(STATUS_LED_PIN, false);
      busy_wait_us_32(250000);
    }
    return false;
  }
#endif
  uint32_t irq_state_ = 0;
  bool active_ = false;
};
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
class NanoStatefulExecution final : public firmingo_managed::stateful::ExecutionPort {
 public:
  void stopped() override {
    backend.discard();
    sketchConsole.stop(applicationBytes, sizeof(applicationBytes));
  }
  void invoke(uint32_t entry) override { reinterpret_cast<void (*)()>(entry)(); }
};
NanoStatefulExecution statefulExecution;
firmingo_managed::stateful::Runtime statefulRuntime(
    {firmingo_sketch_state, sizeof(firmingo_sketch_state),
     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(firmingo_sketch_state))},
    {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&firmingo_sketch_api_v2)),
     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(firmingo_sketch_state)),
     sizeof(firmingo_sketch_state), 4096}, statefulExecution);
#ifdef FIRMINGO_STATEFUL_UPLOAD_PROOF
NanoFlash statefulFlash;
firmingo_managed::stateful::Replacement statefulReplacement(statefulRuntime, sketchPause, statefulFlash);
bool statefulUploadArmed() { return digitalRead(PROOF_ARM_PIN) == LOW; }
firmingo_managed::stateful::Upload statefulUpload(
    {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&firmingo_sketch_api_v2)),
     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(firmingo_sketch_state)),
     sizeof(firmingo_sketch_state), 4096}, statefulRuntime, statefulReplacement, statefulUploadArmed);
firmingo::TcpSessionServer protocolService(channel, &memorySamples, &statefulUpload);
#else
// Boot-only candidate: no upload service or FMS1/FMS2 network target is exposed.
firmingo::TcpSessionServer protocolService(channel, &memorySamples);
#endif
#else
class NanoSketchRuntime final : public firmingo_managed::SketchRuntime {
 public:
  void stop() override {
    sketchGeneration.store(0, std::memory_order_release);
    sketchReady.store(false, std::memory_order_release);
    sketchReturnedGeneration.store(0, std::memory_order_release);
    backend.discard();
    sketchConsole.stop(applicationBytes, sizeof(applicationBytes));
  }
  void start(const firmingo_managed::ImageInfo& next) override {
    sketchImage = next;
    sketchReturnedGeneration.store(0, std::memory_order_release);
    sketchReady.store(true, std::memory_order_release);
    sketchGeneration.store(next_generation_, std::memory_order_release);
    if (++next_generation_ == 0) next_generation_ = 1;
  }
 private:
  uint32_t next_generation_ = 2;
};
NanoFlash proofFlash;
NanoSketchRuntime sketchRuntime;
firmingo_managed::SketchSwitch sketchSwitch(capsuleStage, sketchPause,
                                             proofFlash, sketchRuntime);
bool uploadArmed() { return digitalRead(PROOF_ARM_PIN) == LOW; }
bool generationReturned() {
  const uint32_t generation = sketchGeneration.load(std::memory_order_acquire);
  return generation && sketchReturnedGeneration.load(std::memory_order_acquire) == generation;
}
firmingo_managed::FmgoUpload uploadService(capsuleStage, sketchSwitch,
    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&firmingo_sketch_api)),
    uploadArmed, generationReturned);
firmingo::TcpSessionServer protocolService(channel,&memorySamples,&uploadService);
uint32_t core1RunningGeneration = 0;  // Core 1 only.
#endif
uint32_t clockMs() { return millis(); }
[[noreturn]] void startupFault();

void sketchPinMode(uint32_t pin, uint32_t mode) {
  if (pin < 30 && pin != PROOF_ARM_PIN && mode == 1)
    ::pinMode(pin, static_cast<PinMode>(mode));
}
void sketchDigitalWrite(uint32_t pin, uint32_t value) {
  if (pin < 30 && pin != PROOF_ARM_PIN && value <= 1)
    ::digitalWrite(pin, static_cast<PinStatus>(value));
}
uint32_t sketchMillis() { return ::millis(); }
void sketchDelay(uint32_t milliseconds) { ::delay(milliseconds); }
uint32_t sketchConsoleWrite(const uint8_t* data, uint32_t size) {
  return sketchConsole.sketch_write(data, size);
}
uint32_t sketchConsoleWriteAvailable() {
  return sketchConsole.sketch_write_available();
}
int32_t sketchConsoleRead() {
  return sketchConsole.sketch_read();
}
uint32_t sketchConsoleAvailable() {
  return sketchConsole.sketch_available();
}

void transferSketchBytes() {
  if (!sketchConsole.poll(backend, applicationBytes, sizeof(applicationBytes)))
    startupFault();
}

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

// The separately linked capsule records this *exact* resident address.
extern "C" __attribute__((used)) const FirmingoSketchApiV1 firmingo_sketch_api = {
    kFirmingoApiMagic, 1, sizeof(FirmingoSketchApiV1),
    sketchPinMode, sketchDigitalWrite, sketchMillis, sketchDelay,
    sketchConsoleWrite, sketchConsoleWriteAvailable,
    sketchConsoleRead, sketchConsoleAvailable};

#ifdef FIRMINGO_STATEFUL_ALLOCATION_PROOF
// A real .bss allocation, accounted in the resident's static RAM use. The linker
// roots the candidate table explicitly; its pointer retains the arena.
// Allocation-only builds retain FMS1; the separate runtime flag enables FMS2 boot.
extern "C" {
alignas(4) uint8_t firmingo_sketch_state[kFirmingoStateCapacityBytes];
extern __attribute__((used)) const FirmingoSketchApiV2 firmingo_sketch_api_v2 = {
    {kFirmingoApiMagic, 2, sizeof(FirmingoSketchApiV2),
     sketchPinMode, sketchDigitalWrite, sketchMillis, sketchDelay,
     sketchConsoleWrite, sketchConsoleWriteAvailable,
     sketchConsoleRead, sketchConsoleAvailable},
    firmingo_sketch_state, sizeof(firmingo_sketch_state), 4096};
}
static_assert(sizeof(FirmingoSketchApiV2) == 52, "candidate ARM ABI layout changed");
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
extern "C" __attribute__((used)) const uint32_t firmingo_stateful_boot_mode = 1;
#endif
#endif

void setup1() {}

void loop1() {
  sketchPause.park_if_requested();
  sketchConsole.sketch_boundary();
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
  if (!statefulRuntime.tick()) delay(1);
#else
  const uint32_t generation = sketchGeneration.load(std::memory_order_acquire);
  if (!generation) {
    core1RunningGeneration = 0;
    delay(1);
    return;
  }
  if (generation != core1RunningGeneration) {
    core1RunningGeneration = generation;
    reinterpret_cast<void (*)()>(sketchImage.setup_address)();
  }
  reinterpret_cast<void (*)()>(sketchImage.loop_address)();
  sketchReturnedGeneration.store(generation, std::memory_order_release);
#endif
}

void setup() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(PROOF_ARM_PIN, INPUT_PULLUP);
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
  const auto* slot = reinterpret_cast<const uint8_t*>(firmingo_managed::kSlotAddress);
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
  // Header-derived exact view is bounded to the one 4 KiB proof sector. The
  // runtime initializes RAM before publishing any entries to the other core.
  statefulRuntime.boot_from_slot(slot, firmingo_managed::kProofSectorBytes);
  USB.begin();
#else
  const auto result = firmingo_managed::validate_image(
      slot, firmingo_managed::kSlotBytes,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&firmingo_sketch_api)),
      &sketchImage);
  USB.begin(); // One final NCM-only attachment, with DHCP/listener already ready.
  if (result == firmingo_managed::ImageError::ok) {
    sketchReady.store(true, std::memory_order_release);
    sketchGeneration.store(1, std::memory_order_release);
  }
#endif
}

void loop() {
  // Core 1 owns the built-in LED when a valid sketch is running.
#ifdef FIRMINGO_STATEFUL_RUNTIME_PROOF
  if (!statefulRuntime.active_generation())
#else
  if (!sketchReady.load(std::memory_order_acquire))
#endif
    digitalWrite(STATUS_LED_PIN,ethernet.linkStatus() == LinkON ? HIGH : LOW);
  ethernet_arch_lwip_begin();
  // Sample the C heap and approximate current core-0 stack before/after work.
  // lwIP has a separate fixed pool; these samples do not measure its usage.
  sampleRuntime();
  protocolService.poll(millis());
  sampleRuntime();
  ethernet_arch_lwip_end();

  transferSketchBytes();
#ifdef FIRMINGO_STATEFUL_UPLOAD_PROOF
  statefulUpload.poll(millis()); // All pause/flash work is outside the lwIP lock.
#endif
#ifndef FIRMINGO_STATEFUL_RUNTIME_PROOF
  uploadService.poll(millis()); // Pause/flash/install only outside the lwIP lock.
#endif
  delay(1);
}
