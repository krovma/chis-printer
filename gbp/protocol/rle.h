#pragma once

#include <cstddef>
#include <cstdint>

namespace gbp::protocol {

// Game Boy Printer DATA compression: literal runs of 1..128 bytes and
// repeated runs of 2..129 bytes. State is reset for each DATA packet.
class RleDecoder {
  public:
    void reset(bool compressed);
    std::size_t feed(std::uint8_t input, std::uint8_t output[129]);
    bool complete() const;

  private:
    enum class State : std::uint8_t { Control, Literal, RepeatValue };
    State state_ = State::Control;
    std::uint8_t remaining_ = 0;
    bool compressed_ = false;
};

}  // namespace gbp::protocol
