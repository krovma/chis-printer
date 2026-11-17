// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gbp/host/print_job.h"

namespace {

struct Call {
    gbp::protocol::Command command;
    std::vector<std::uint8_t> payload;
};

class Printer final : public gbp::host::PacketTransport {
  public:
    bool send(const gbp::protocol::Command command, const std::uint8_t* payload,
              const std::uint16_t length, std::uint8_t& status) override {
        calls.push_back({command, payload == nullptr ? std::vector<std::uint8_t>{} :
                                      std::vector<std::uint8_t>{payload, payload + length}});
        if (!connected) return false;
        status = error_status;
        if (command == gbp::protocol::Command::Print) busy_inquiries = 2;
        if (command == gbp::protocol::Command::Inquiry && always_busy) {
            status |= gbp::protocol::STATUS_BUSY;
        } else if (command == gbp::protocol::Command::Inquiry && busy_inquiries != 0) {
            status |= gbp::protocol::STATUS_BUSY;
            --busy_inquiries;
        }
        return true;
    }

    void pause_ms(const std::uint32_t milliseconds) override {
        assert(milliseconds == 100);
        ++pauses;
    }

    std::vector<Call> calls;
    std::size_t pauses = 0;
    std::uint32_t busy_inquiries = 0;
    std::uint8_t error_status = 0;
    bool connected = true;
    bool always_busy = false;
};

}  // namespace

int main() {
    gbp::host::PrintJob job;
    std::atomic<std::size_t> sent{999};
    Printer printer;
    const gbp::format::PrintControl control{2, 0xA5, 0x2D, 0x7F};
    std::vector<std::uint8_t> image(9 * 640 + 320, 0xA5);

    assert(job.run(printer, image.data(), image.size(), control, sent) == gbp::host::PrintResult::Success);
    assert(sent.load() == image.size());
    std::size_t init_count = 0;
    std::size_t data_count = 0;
    std::size_t empty_data_count = 0;
    std::vector<gbp::format::PrintControl> print_controls;
    for (const auto& call : printer.calls) {
        if (call.command == gbp::protocol::Command::Init) ++init_count;
        if (call.command == gbp::protocol::Command::Data) {
            if (call.payload.empty()) ++empty_data_count;
            else ++data_count;
        }
        if (call.command == gbp::protocol::Command::Print) {
            assert(call.payload.size() == 4);
            print_controls.push_back({call.payload[0], call.payload[1], call.payload[2], call.payload[3]});
        }
    }
    assert(init_count == 2 && data_count == 10 && empty_data_count == 2);
    assert(print_controls.size() == 2);
    assert((print_controls[0] == gbp::format::PrintControl{2, 0xA0, 0x2D, 0x7F}));
    assert((print_controls[1] == gbp::format::PrintControl{2, 0x05, 0x2D, 0x7F}));
    assert(printer.pauses == 4);

    assert(job.run(printer, nullptr, 320, control, sent) == gbp::host::PrintResult::InvalidImage);
    assert(sent.load() == 0);
    Printer missing;
    missing.connected = false;
    assert(job.run(missing, image.data(), 320, control, sent) == gbp::host::PrintResult::NoPrinter);
    Printer error;
    error.error_status = 0x20;
    assert(job.run(error, image.data(), 320, control, sent) == gbp::host::PrintResult::PrinterError);
    Printer busy;
    busy.always_busy = true;
    assert(job.run(busy, image.data(), 320, control, sent) == gbp::host::PrintResult::Timeout);
    assert(busy.pauses == 50);
}
