// Native checker for actual independently linked FMS2 capsules. Calls production
// validation/initialization; it never maps target pointers or executes ARM code.
#include "stateful_image.h"
#include "stateful_runtime.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace sf = firmingo_managed::stateful;
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::uint32_t number(const char* text) {
  char* end = nullptr;
  errno = 0;
  const auto value = std::strtoull(text, &end, 0);
  require(text[0] && text[0] != '-' && end && !*end && !errno && value <= 0xffffffffu,
          "expected uint32 argument");
  return static_cast<std::uint32_t>(value);
}

// A host observation port, never an ARM function-pointer call. It checks actual
// linked initialized data and zero tail before modeling setup/loop RAM writes.
class ObservedEntries final : public sf::ExecutionPort {
 public:
  ObservedEntries(std::uint8_t* ram, std::size_t capacity,
                  const std::uint8_t* data, sf::ImageInfo image)
      : ram_(ram), capacity_(capacity), data_(data), image_(image) {}
  void stopped() override {}
  void invoke(std::uint32_t address) override {
    if (address == image_.setup_address) {
      ++setups;
      require(std::equal(data_, data_ + image_.data_bytes, ram_), "runtime setup saw wrong initialized data");
      require(std::all_of(ram_ + image_.data_bytes, ram_ + capacity_,
                          [](std::uint8_t b) { return b == 0; }), "runtime setup saw uncleared BSS/tail");
      ram_[capacity_ - 1] = 39;
    } else {
      require(address == image_.loop_address, "runtime invoked unexpected entry");
      ++loops;
      ++ram_[capacity_ - 1];
    }
  }
  unsigned setups = 0, loops = 0;
 private:
  std::uint8_t* ram_;
  std::size_t capacity_;
  const std::uint8_t* data_;
  sf::ImageInfo image_;
};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr, "Usage: managed_stateful_check capsule.fms api-address state-address capacity-bytes\n");
    return 2;
  }
  try {
    const sf::Contract contract{number(argv[2]), number(argv[3]), number(argv[4]), 4096};
    require(contract.state_capacity_bytes && contract.state_capacity_bytes <= 4096,
            "proof checker limits arena to 4 KiB");
    std::ifstream input(argv[1], std::ios::binary);
    require(input.is_open(), "cannot open capsule");
    std::vector<std::uint8_t> image(4097);
    input.read(reinterpret_cast<char*>(image.data()), image.size());
    image.resize(static_cast<std::size_t>(input.gcount()));
    require(!input.bad(), "capsule read failed");
    const auto checked = sf::validate_image(image.data(), image.size(), contract);
    require(checked.error == sf::Error::ok, "capsule rejected");
    const auto capacity = contract.state_capacity_bytes;
    std::vector<std::uint8_t> guarded(capacity + 32, 0xa5);
    auto* ram = guarded.data() + 16;
    sf::Generation owner({ram, capacity, contract.state_address});
    const auto verify = [&]() {
      require(std::all_of(guarded.begin(), guarded.begin() + 16,
                          [](std::uint8_t b) { return b == 0xa5; }) &&
              std::all_of(guarded.end() - 16, guarded.end(),
                          [](std::uint8_t b) { return b == 0xa5; }), "arena guard modified");
      require(std::equal(image.begin() + checked.image.data_load_offset, image.end(), ram),
              "initialized data differs from linked flash payload");
      require(std::all_of(ram + checked.image.data_bytes, ram + capacity,
                          [](std::uint8_t b) { return b == 0; }), "BSS or unused arena not cleared");
    };
    require(owner.begin(image.data(), image.size(), contract, 1).error == sf::Error::ok,
            "generation one rejected");
    verify();
    // Host changes model successive loops. No ARM setup/loop execution is claimed.
    std::fill(ram, ram + capacity, 0x3c);
    require(owner.begin(image.data(), image.size(), contract, 2).error == sf::Error::active_generation,
            "active generation silently reinitialized");
    require(std::all_of(ram, ram + capacity, [](std::uint8_t b) { return b == 0x3c; }),
            "active state changed without stop");
    owner.stop();
    require(owner.begin(image.data(), image.size(), contract, 2).error == sf::Error::ok,
            "replacement generation rejected");
    verify();
    owner.stop();
    ObservedEntries entries(ram, capacity, image.data() + checked.image.data_load_offset, checked.image);
    sf::Runtime runtime({ram, capacity, contract.state_address}, contract, entries);
    auto slot = image;
    slot.resize(4096, 0xff);
    require(runtime.boot_from_slot(slot.data(), slot.size()).error == sf::Error::ok,
            "runtime could not derive exact boot view from padded slot");
    require(!runtime.loop_returned() && runtime.active_generation() == 1, "runtime published bad boot state");
    require(runtime.tick() && runtime.tick(), "runtime did not invoke host observation port");
    require(entries.setups == 1 && entries.loops == 2 && ram[capacity - 1] == 41,
            "runtime lost loop state or repeated setup");
    require(runtime.loop_returned(), "runtime did not record completed first loop");
    std::printf("FMS2 linked capsule: code=%u data=%u bss=%u; guarded initialization, generation replacement and host boot dispatch passed; ARM NOT executed\n",
                checked.image.code_bytes, checked.image.data_bytes, checked.image.bss_bytes);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
