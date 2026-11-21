// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <atomic>
#include <cassert>
#include <vector>

#include "types.h"
#include "gbp/host/print_job.h"

namespace {

struct Call {
    Gbp::Protocol::Command command;
    std::vector<uint8> payload;
};

class Printer final : public Gbp::Host::PacketTransport {
  public:
    bool send(const Gbp::Protocol::Command command, const uint8* payload,
              const uint16 length, uint8& status) override {
        calls.push_back({command, payload == nullptr ? std::vector<uint8>{} :
                                      std::vector<uint8>{payload, payload + length}});
        if (!connected) {
            return false;
        }
        status = error_status;
        if (command == Gbp::Protocol::Command::Print) {
            busy_inquiries = 2;
        }
        if (command == Gbp::Protocol::Command::Inquiry && always_busy) {
            status |= Gbp::Protocol::STATUS_BUSY;
        } else if (command == Gbp::Protocol::Command::Inquiry && busy_inquiries != 0) {
            status |= Gbp::Protocol::STATUS_BUSY;
            --busy_inquiries;
        }
        return true;
    }

    void pause_ms(const uint32 milliseconds) override {
        assert(milliseconds == 100);
        ++pauses;
    }

    std::vector<Call> calls;
    usize pauses = 0;
    uint32 busy_inquiries = 0;
    uint8 error_status = 0;
    bool connected = true;
    bool always_busy = false;
};

}  // namespace

int main() {
    Gbp::Host::PrintJob job;
    std::atomic<usize> sent{999};
    Printer printer;
    const Gbr::Format::PrintControl control{2, 0xA5, 0x2D, 0x7F};
    std::vector<uint8> image(9 * 640 + 320, 0xA5);

    assert(job.run(printer, image.data(), image.size(), control, sent) == Gbp::Host::PrintResult::Success);
    assert(sent.load() == image.size());
    usize init_count = 0;
    usize data_count = 0;
    usize empty_data_count = 0;
    std::vector<Gbr::Format::PrintControl> print_controls;
    for (const auto &call : printer.calls) {
        if (call.command == Gbp::Protocol::Command::Init) {
            ++init_count;
        }
        if (call.command == Gbp::Protocol::Command::Data) {
            if (call.payload.empty()) {
                ++empty_data_count;
            } else {
                ++data_count;
            }
        }
        if (call.command == Gbp::Protocol::Command::Print) {
            assert(call.payload.size() == 4);
            print_controls.push_back({call.payload[0], call.payload[1], call.payload[2], call.payload[3]});
        }
    }
    assert(init_count == 2 && data_count == 10 && empty_data_count == 2);
    assert(print_controls.size() == 2);
    assert((print_controls[0] == Gbr::Format::PrintControl{2, 0xA0, 0x2D, 0x7F}));
    assert((print_controls[1] == Gbr::Format::PrintControl{2, 0x05, 0x2D, 0x7F}));
    assert(printer.pauses == 4);

    Printer m2_printer;
    const auto m2_control = Gbr::Format::m2_print_control({255, 0x13, 0xE4, 0x40});
    assert(job.run(m2_printer, image.data(), 320, m2_control, sent) == Gbp::Host::PrintResult::Success);
    for (const auto &call : m2_printer.calls) {
        if (call.command == Gbp::Protocol::Command::Print) {
            assert((call.payload == std::vector<uint8>{1, 0x13, 0xE4, 0x40}));
        }
    }

    assert(job.run(printer, nullptr, 320, control, sent) == Gbp::Host::PrintResult::InvalidImage);
    assert(sent.load() == 0);
    Printer missing;
    missing.connected = false;
    assert(job.run(missing, image.data(), 320, control, sent) == Gbp::Host::PrintResult::NoPrinter);
    Printer error;
    error.error_status = 0x20;
    assert(job.run(error, image.data(), 320, control, sent) == Gbp::Host::PrintResult::PrinterError);
    Printer busy;
    busy.always_busy = true;
    assert(job.run(busy, image.data(), 320, control, sent) == Gbp::Host::PrintResult::Timeout);
    assert(busy.pauses == 50);
}
