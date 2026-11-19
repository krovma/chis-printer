// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "gbr/format.h"
#include "gbp/protocol/packet.h"
#include "gbp/protocol/packet_writer.h"
#include "gbp/protocol/rle.h"
#include "gbp/terminal/printer_session.h"
#include "gbp/terminal/m1_state.h"
#include "gbp/terminal/receiver.h"
#include "storage/gbr_filename.h"

namespace {

std::vector<std::uint8_t> packet(const gbp::protocol::Command command,
                                 const std::uint8_t compression,
                                 const std::vector<std::uint8_t>& payload,
                                 const bool bad_checksum = false) {
    const auto length = static_cast<std::uint16_t>(payload.size());
    std::uint16_t checksum = static_cast<std::uint8_t>(command) + compression +
                             static_cast<std::uint8_t>(length) + static_cast<std::uint8_t>(length >> 8);
    std::vector<std::uint8_t> bytes{
        0x88, 0x33, static_cast<std::uint8_t>(command), compression,
        static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(length >> 8)};
    for (const auto byte : payload) {
        bytes.push_back(byte);
        checksum += byte;
    }
    if (bad_checksum) {
        ++checksum;
    }
    bytes.push_back(static_cast<std::uint8_t>(checksum));
    bytes.push_back(static_cast<std::uint8_t>(checksum >> 8));
    bytes.push_back(0);
    bytes.push_back(0);
    return bytes;
}

void send(gbp::terminal::Receiver& receiver, const std::vector<std::uint8_t>& bytes) {
    for (const auto byte : bytes) {
        receiver.feed(byte);
    }
}

void send(gbp::terminal::M1State& state, const std::vector<std::uint8_t>& bytes,
          const std::uint32_t now_us = 0) {
    for (const auto byte : bytes) {
        if (state.receiver().feed(byte)) {
            state.core1_on_receiver_change(now_us);
        }
    }
}

std::uint8_t reply_status(gbp::terminal::PrinterSession& session, const std::vector<std::uint8_t>& bytes) {
    std::uint8_t device_id = 0;
    std::uint8_t status = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto reply = session.feed(bytes[i], false);
        if (i == bytes.size() - 3) {
            device_id = reply.next_tx;
        }
        if (i == bytes.size() - 2) {
            status = reply.next_tx;
        }
        if (i == bytes.size() - 1) {
            assert(reply.packet_end);
        }
    }
    assert(device_id == gbp::protocol::DEVICE_ID);
    return status;
}

}  // namespace

int main() {
    using gbp::protocol::Command;
    std::array<std::uint8_t, 10> init_encoded{};
    const std::array<std::uint8_t, 10> expected_init{0x88, 0x33, 0x01, 0, 0, 0, 0x01, 0, 0, 0};
    assert(gbp::protocol::write_packet(Command::Init, false, nullptr, 0,
                                       init_encoded.data(), init_encoded.size()) == init_encoded.size());
    assert(init_encoded == expected_init);

    std::array<std::uint8_t, 16> encoded{};
    const std::uint8_t print_options[4]{1, 0, 0xE4, 0x40};
    assert(gbp::protocol::write_packet(Command::Print, false, print_options, 4,
                                       encoded.data(), encoded.size()) == 14);
    const auto expected_print = packet(Command::Print, 0, {1, 0, 0xE4, 0x40});
    assert(std::equal(expected_print.begin(), expected_print.end(), encoded.begin()));
    assert(gbp::protocol::write_packet(Command::Print, false, print_options, 4,
                                       encoded.data(), 13) == 0);

    static gbp::terminal::ImageBuffer image{};
    gbp::terminal::Receiver receiver(image);
    assert(receiver.state() == gbp::terminal::ViewState::Waiting);

    send(receiver, packet(Command::Init, 0, {}));
    assert(receiver.state() == gbp::terminal::ViewState::Receiving);

    std::vector<std::uint8_t> tile_row(320, 0);
    tile_row[0] = 0x80;
    tile_row[1] = 0x00;
    send(receiver, packet(Command::Data, 0, tile_row, true));
    assert(receiver.data_frames() == 0);

    const gbr::format::PrintControl custom_print{0, 0x13, 0x2D, 0x7F};
    const auto header = gbr::format::make_gbr_header(160, 16, custom_print);
    const std::array<std::uint8_t, 12> expected_header{'K', 'G', 'B', 'R', 2, 160, 16, 0,
                                                        0, 0x13, 0x2D, 0x7F};
    assert(header == expected_header);
    assert((gbr::format::make_gbr_file_header(2) ==
            std::array<std::uint8_t, 6>{'K', 'G', 'B', 'R', 3, 2}));
    assert((gbr::format::make_gbr_block_header(160, 16, custom_print) ==
            std::array<std::uint8_t, 8>{2, 160, 16, 0, 0, 0x13, 0x2D, 0x7F}));
    assert((gbr::format::m2_print_control(custom_print) ==
            gbr::format::PrintControl{1, 0x13, 0x2D, 0x7F}));

    std::uint64_t file_number = 0;
    assert(storage::parse_gbr_filename("00099.GBR", file_number) && file_number == 99);
    assert(storage::parse_gbr_filename("100000.gbr", file_number) && file_number == 100000);
    assert(!storage::parse_gbr_filename("1234.gbr", file_number));
    assert(!storage::parse_gbr_filename("123ab.gbr", file_number));
    assert(!storage::parse_gbr_filename("99999.gbp", file_number));
    char path[40]{};
    assert(storage::format_gbr_path(100000, path, sizeof(path)));
    assert(std::strcmp(path, "0:/DCIM/100000.gbr") == 0);

    static gbp::terminal::M1State m1;
    send(m1, packet(Command::Init, 0, {}));
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {0, 0x13, 0x2D, 0x7F}, true));
    assert(m1.snapshot().view == gbp::terminal::ViewState::Receiving);
    send(m1, packet(Command::Print, 0, {0, 0x13, 0x2D, 0x7F}));
    assert(m1.receiver().state() == gbp::terminal::ViewState::Waiting);
    assert(m1.snapshot().view == gbp::terminal::ViewState::Preview);
    gbp::terminal::M1State::Frame first{};
    assert(m1.acquire_frame(first));
    assert(first.size == 320 && first.data[0] == 0x80);
    assert(first.print_control == custom_print);
    assert(first.palette == 0x1E);

    send(m1, packet(Command::Inquiry, 0, {}));
    assert(m1.snapshot().view == gbp::terminal::ViewState::Preview);
    send(m1, packet(Command::Init, 0, {}));
    assert(m1.snapshot().view == gbp::terminal::ViewState::Receiving);
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}));
    assert(m1.snapshot().view == gbp::terminal::ViewState::Waiting);
    gbp::terminal::M1State::Frame discarded{};
    assert(!m1.acquire_frame(discarded));

    m1.release_frame();
    send(m1, packet(Command::Init, 0, {}));
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}));
    gbp::terminal::M1State::Frame second{};
    assert(m1.acquire_frame(second));
    assert(second.data != first.data && second.generation == first.generation + 1);
    m1.request_confirmation(first.generation);
    m1.core1_process_confirmation();
    assert(m1.snapshot().view == gbp::terminal::ViewState::Preview);
    m1.request_confirmation(second.generation);
    m1.core1_process_confirmation();
    assert(m1.snapshot().view == gbp::terminal::ViewState::Waiting);
    m1.release_frame();
    assert(receiver.image_size() == 0);

    static gbp::terminal::M1State timed;
    const auto init = packet(Command::Init, 0, {});
    const auto data = packet(Command::Data, 0, tile_row);
    const auto print = packet(Command::Print, 0, {7, 0x13, 0xE4, 0x40});
    send(timed, init);
    send(timed, data);
    send(timed, print);
    gbp::terminal::M1State::Frame timed_frame{};
    assert(timed.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    timed.release_frame();

    timed.core1_on_print_complete(1'000'000);
    send(timed, init, 1'500'000);
    send(timed, data, 2'000'000);
    send(timed, print, 2'000'000);
    assert(timed.acquire_frame(timed_frame) && timed_frame.append_to_previous);
    timed.release_frame();

    timed.core1_on_print_complete(3'000'000);
    send(timed, init, 3'500'000);
    send(timed, data, 4'000'001);
    send(timed, print, 4'000'001);
    assert(timed.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    timed.release_frame();

    timed.core1_on_print_complete(0xFFFF'FF00);
    send(timed, init, 0xFFFF'FF80);
    send(timed, data, 0x0000'0100);
    send(timed, print, 0x0000'0100);
    assert(timed.acquire_frame(timed_frame) && timed_frame.append_to_previous);
    timed.release_frame();

    timed.core1_on_print_complete(100);
    timed.core1_expire_completion_window(1'000'101);
    send(timed, init, 0xFFFF'FF80);
    send(timed, data, 0x0000'0100);
    send(timed, print, 0x0000'0100);
    assert(timed.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    timed.release_frame();

    static gbp::terminal::M1State disabled{0};
    send(disabled, init);
    send(disabled, data);
    send(disabled, print);
    assert(disabled.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    disabled.release_frame();
    disabled.core1_on_print_complete(100);
    send(disabled, init, 101);
    send(disabled, data, 101);
    send(disabled, print, 101);
    assert(disabled.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    disabled.release_frame();

    send(receiver, packet(Command::Data, 0, tile_row));
    assert(receiver.data_frames() == 1);
    assert(receiver.image_size() == 320);
    assert(receiver.image_data()[0] == 0x80);

    const std::vector<std::uint8_t> compressed{0xFF, 0xA5, 0xFF, 0xA5, 0xBC, 0xA5};
    send(receiver, packet(Command::Data, 1, compressed));
    assert(receiver.data_frames() == 2);
    assert(receiver.image_size() == 640);
    assert(receiver.image_data()[320] == 0xA5);
    assert(receiver.image_data()[639] == 0xA5);

    send(receiver, packet(Command::Print, 0, {1, 0, 0, 0x40}));
    assert(receiver.state() == gbp::terminal::ViewState::Preview);
    assert(receiver.palette() == 0xE4);
    const gbr::format::PrintControl zero_palette_print{1, 0, 0, 0x40};
    assert(receiver.print_control() == zero_palette_print);
    assert(receiver.buffer_full());
    receiver.confirm_preview();
    assert(receiver.state() == gbp::terminal::ViewState::Waiting);
    assert(receiver.image_size() == 0);
    assert(receiver.data_frames() == 0);

    gbp::terminal::PrinterSession session;
    assert(reply_status(session, packet(Command::Init, 0, {})) == 0);
    assert(reply_status(session, packet(Command::Data, 0, tile_row, true)) &
           gbp::protocol::STATUS_CHECKSUM_ERROR);
    assert((reply_status(session, packet(Command::Data, 0, tile_row)) &
            gbp::protocol::STATUS_CHECKSUM_ERROR) == 0);
    reply_status(session, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}));
    assert(reply_status(session, packet(Command::Inquiry, 0, {})) == 0);
    assert(reply_status(session, packet(Command::Inquiry, 0, {})) == 0);
    assert((reply_status(session, packet(Command::Inquiry, 0, {})) &
            (gbp::protocol::STATUS_FULL | gbp::protocol::STATUS_BUSY)) ==
           (gbp::protocol::STATUS_FULL | gbp::protocol::STATUS_BUSY));
    assert((reply_status(session, packet(Command::Inquiry, 0, {})) &
            gbp::protocol::STATUS_BUSY) == 0);

    gbp::protocol::RleDecoder decoder;
    decoder.reset(true);
    std::array<std::uint8_t, 129> output{};
    assert(decoder.feed(0x01, output.data()) == 0);
    assert(decoder.feed(0x12, output.data()) == 1);
    assert(!decoder.complete());
    assert(decoder.feed(0x34, output.data()) == 1);
    assert(decoder.complete());
}
