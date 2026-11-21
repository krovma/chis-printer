// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "types.h"
#include "gbr/format.h"
#include "gbp/protocol/packet.h"
#include "gbp/protocol/packet_writer.h"
#include "gbp/protocol/rle.h"
#include "gbp/terminal/printer_session.h"
#include "gbp/terminal/m1_state.h"
#include "gbp/terminal/receiver.h"
#include "storage/gbr_filename.h"

namespace {

std::vector<uint8> packet(const Gbp::Protocol::Command command,
                          const uint8 compression,
                          const std::vector<uint8>& payload,
                          const bool bad_checksum = false) {
    const auto length = static_cast<uint16>(payload.size());
    uint16 checksum = static_cast<uint8>(command) + compression +
                      static_cast<uint8>(length) + static_cast<uint8>(length >> 8);
    std::vector<uint8> bytes{
        0x88, 0x33, static_cast<uint8>(command), compression,
        static_cast<uint8>(length), static_cast<uint8>(length >> 8)};
    for (const auto byte : payload) {
        bytes.push_back(byte);
        checksum += byte;
    }
    if (bad_checksum) {
        ++checksum;
    }
    bytes.push_back(static_cast<uint8>(checksum));
    bytes.push_back(static_cast<uint8>(checksum >> 8));
    bytes.push_back(0);
    bytes.push_back(0);
    return bytes;
}

void send(Gbp::Terminal::Receiver& receiver, const std::vector<uint8>& bytes) {
    for (const auto byte : bytes) {
        receiver.feed(byte);
    }
}

void send(Gbp::Terminal::M1State& state, const std::vector<uint8>& bytes,
          const uint32 now_us = 0) {
    for (const auto byte : bytes) {
        if (state.receiver().feed(byte)) {
            state.core1_on_receiver_change(now_us);
        }
    }
}

uint8 reply_status(Gbp::Terminal::PrinterSession& session, const std::vector<uint8>& bytes,
                          const uint32 now_us = 100, const bool full_after_checksum = false) {
    uint8 device_id = 0;
    uint8 status = 0;
    for (usize i = 0; i < bytes.size(); ++i) {
        const auto reply = session.feed(bytes[i], full_after_checksum && i >= bytes.size() - 2, now_us);
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
    assert(device_id == Gbp::Protocol::DEVICE_ID);
    return status;
}

}  // namespace

int main() {
    using Gbp::Protocol::Command;
    std::array<uint8, 10> init_encoded{};
    const std::array<uint8, 10> expected_init{0x88, 0x33, 0x01, 0, 0, 0, 0x01, 0, 0, 0};
    assert(Gbp::Protocol::write_packet(Command::Init, false, nullptr, 0,
                                       init_encoded.data(), init_encoded.size()) == init_encoded.size());
    assert(init_encoded == expected_init);

    std::array<uint8, 16> encoded{};
    const uint8 print_options[4]{1, 0, 0xE4, 0x40};
    assert(Gbp::Protocol::write_packet(Command::Print, false, print_options, 4,
                                       encoded.data(), encoded.size()) == 14);
    const auto expected_print = packet(Command::Print, 0, {1, 0, 0xE4, 0x40});
    assert(std::equal(expected_print.begin(), expected_print.end(), encoded.begin()));
    assert(Gbp::Protocol::write_packet(Command::Print, false, print_options, 4,
                                       encoded.data(), 13) == 0);

    static Gbp::Terminal::ImageBuffer image{};
    Gbp::Terminal::Receiver receiver(image);
    assert(receiver.state() == Gbp::Terminal::ViewState::Waiting);

    send(receiver, packet(Command::Init, 0, {}));
    assert(receiver.state() == Gbp::Terminal::ViewState::Receiving);

    std::vector<uint8> tile_row(320, 0);
    tile_row[0] = 0x80;
    tile_row[1] = 0x00;
    send(receiver, packet(Command::Data, 0, tile_row, true));
    assert(receiver.data_frames() == 0);

    const Gbr::Format::PrintControl custom_print{0, 0x13, 0x2D, 0x7F};
    const auto header = Gbr::Format::make_gbr_header(160, 16, custom_print);
    const std::array<uint8, 12> expected_header{'K', 'G', 'B', 'R', 2, 160, 16, 0,
                                                        0, 0x13, 0x2D, 0x7F};
    assert(header == expected_header);
    assert((Gbr::Format::make_gbr_file_header(2) ==
            std::array<uint8, 6>{'K', 'G', 'B', 'R', 3, 2}));
    assert((Gbr::Format::make_gbr_block_header(160, 16, custom_print) ==
            std::array<uint8, 8>{2, 160, 16, 0, 0, 0x13, 0x2D, 0x7F}));
    assert((Gbr::Format::m2_print_control(custom_print) ==
            Gbr::Format::PrintControl{1, 0x13, 0x2D, 0x7F}));

    uint64 file_number = 0;
    assert(Storage::parse_gbr_filename("00099.GBR", file_number) && file_number == 99);
    assert(Storage::parse_gbr_filename("100000.gbr", file_number) && file_number == 100000);
    assert(!Storage::parse_gbr_filename("1234.gbr", file_number));
    assert(!Storage::parse_gbr_filename("123ab.gbr", file_number));
    assert(!Storage::parse_gbr_filename("99999.gbp", file_number));
    char path[40]{};
    assert(Storage::format_gbr_path(100000, path, sizeof(path)));
    assert(std::strcmp(path, "0:/DCIM/100000.gbr") == 0);

    static Gbp::Terminal::M1State m1;
    send(m1, packet(Command::Init, 0, {}));
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {0, 0x13, 0x2D, 0x7F}, true));
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Receiving);
    send(m1, packet(Command::Print, 0, {0, 0x13, 0x2D, 0x7F}));
    assert(m1.receiver().state() == Gbp::Terminal::ViewState::Waiting);
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Preview);
    Gbp::Terminal::M1State::Frame first{};
    assert(m1.acquire_frame(first));
    assert(first.size == 320 && first.data[0] == 0x80);
    assert(first.print_control == custom_print);
    assert(first.palette == 0x1E);

    send(m1, packet(Command::Inquiry, 0, {}));
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Preview);
    send(m1, packet(Command::Init, 0, {}));
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Receiving);
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}));
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Waiting);
    assert(m1.snapshot().discarded_frames == 1);
    Gbp::Terminal::M1State::Frame discarded{};
    assert(!m1.acquire_frame(discarded));

    m1.release_frame();
    send(m1, packet(Command::Init, 0, {}));
    send(m1, packet(Command::Data, 0, tile_row));
    send(m1, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}));
    Gbp::Terminal::M1State::Frame second{};
    assert(m1.acquire_frame(second));
    assert(second.data != first.data && second.generation == first.generation + 1);
    m1.request_confirmation(first.generation);
    m1.core1_process_confirmation();
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Preview);
    m1.request_confirmation(second.generation);
    m1.core1_process_confirmation();
    assert(m1.snapshot().view == Gbp::Terminal::ViewState::Waiting);
    m1.release_frame();
    assert(receiver.image_size() == 0);

    static Gbp::Terminal::M1State timed;
    const auto init = packet(Command::Init, 0, {});
    const auto data = packet(Command::Data, 0, tile_row);
    const auto print = packet(Command::Print, 0, {7, 0x13, 0xE4, 0x40});
    send(timed, init);
    send(timed, data);
    send(timed, print);
    Gbp::Terminal::M1State::Frame timed_frame{};
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

    static Gbp::Terminal::M1State disabled{0};
    send(disabled, init);
    send(disabled, data);
    send(disabled, print);
    assert(disabled.acquire_frame(timed_frame) && !timed_frame.append_to_previous);
    disabled.release_frame();

    static Gbp::Terminal::M1State cutoff{100};
    send(cutoff, init);
    send(cutoff, data);
    cutoff.core1_on_print_complete(1'000);
    assert(cutoff.snapshot(1'050).cut_pending);
    assert(cutoff.snapshot(1'050).cut_remaining_us == 50);
    assert(!cutoff.core1_expire_completion_window(1'101));  // The frame has not been published yet.
    send(cutoff, print, 1'102);
    assert(cutoff.acquire_frame(timed_frame));
    assert(cutoff.core1_expire_completion_window(1'102));
    assert(cutoff.snapshot(1'102).cut_sequence == 1);
    assert(!cutoff.snapshot(1'102).cut_pending);
    assert(!cutoff.core1_expire_completion_window(1'103));
    cutoff.release_frame();

    static Gbp::Terminal::M1State immediate{0};
    send(immediate, init);
    send(immediate, data);
    send(immediate, print);
    assert(immediate.acquire_frame(timed_frame));
    immediate.core1_on_print_complete(100);
    assert(!immediate.core1_expire_completion_window(100));
    assert(immediate.core1_expire_completion_window(101));
    assert(immediate.snapshot(101).cut_sequence == 1);
    immediate.release_frame();

    static Gbp::Terminal::M1State late_group{100};
    send(late_group, init);
    send(late_group, data);
    send(late_group, print);
    assert(late_group.acquire_frame(timed_frame));
    late_group.release_frame();
    late_group.request_confirmation(timed_frame.generation);
    late_group.core1_process_confirmation();
    late_group.core1_on_print_complete(1'000);
    send(late_group, init, 1'200);
    send(late_group, data, 1'200);
    assert(late_group.snapshot(1'200).cut_sequence == 1);
    late_group.core1_pause_after_cut();
    assert(late_group.snapshot(1'200).view == Gbp::Terminal::ViewState::Waiting);

    static Gbp::Terminal::M1State stalled;
    send(stalled, init);
    send(stalled, data);
    const auto partial_data = packet(Command::Data, 0, tile_row);
    send(stalled, std::vector<uint8>(partial_data.begin(), partial_data.begin() + 20));
    assert(stalled.receiver().image_size() > 320);
    stalled.receiver().reset();
    stalled.core1_on_receive_timeout(5'000'001);
    assert(stalled.snapshot().view == Gbp::Terminal::ViewState::Waiting);
    assert(stalled.snapshot().frames == 0);
    assert(stalled.receiver().image_size() == 0);
    auto fresh_row = tile_row;
    fresh_row[0] = 0x40;
    send(stalled, packet(Command::Data, 0, fresh_row));
    send(stalled, print);
    Gbp::Terminal::M1State::Frame fresh_frame{};
    assert(stalled.acquire_frame(fresh_frame));
    assert(fresh_frame.size == 320 && fresh_frame.data[0] == 0x40);
    assert(!fresh_frame.append_to_previous);
    stalled.release_frame();
    send(stalled, packet(Command::Break, 0, {}));
    assert(stalled.snapshot().view == Gbp::Terminal::ViewState::Waiting);

    static Gbp::Terminal::M1State interrupted_append{10'000'000};
    send(interrupted_append, init);
    send(interrupted_append, data);
    send(interrupted_append, print);
    Gbp::Terminal::M1State::Frame saved_frame{};
    assert(interrupted_append.acquire_frame(saved_frame));
    interrupted_append.release_frame();
    interrupted_append.request_confirmation(saved_frame.generation);
    interrupted_append.core1_process_confirmation();
    interrupted_append.core1_on_print_complete(1'000);
    send(interrupted_append, init, 2'000);
    send(interrupted_append, data, 2'000);
    assert(!interrupted_append.snapshot(2'000).cut_pending);
    interrupted_append.receiver().reset();
    interrupted_append.core1_on_receive_timeout(5'002'000);
    assert(interrupted_append.snapshot(5'002'000).view == Gbp::Terminal::ViewState::Waiting);
    assert(!interrupted_append.snapshot(5'002'000).cut_pending);
    assert(interrupted_append.snapshot(5'002'000).cut_sequence == 1);
    send(interrupted_append, init, 5'003'000);
    send(interrupted_append, data, 5'003'000);
    send(interrupted_append, print, 5'003'000);
    Gbp::Terminal::M1State::Frame separate_frame{};
    assert(interrupted_append.acquire_frame(separate_frame));
    assert(!separate_frame.append_to_previous);
    interrupted_append.release_frame();

    static Gbp::Terminal::M1State broken_append{100};
    send(broken_append, init);
    send(broken_append, data);
    send(broken_append, print);
    assert(broken_append.acquire_frame(saved_frame));
    broken_append.release_frame();
    broken_append.request_confirmation(saved_frame.generation);
    broken_append.core1_process_confirmation();
    broken_append.core1_on_print_complete(1'000);
    send(broken_append, init, 1'050);
    send(broken_append, data, 1'050);
    assert(!broken_append.snapshot(1'050).cut_pending);
    send(broken_append, packet(Command::Break, 0, {}), 1'101);
    assert(broken_append.snapshot(1'101).view == Gbp::Terminal::ViewState::Waiting);
    assert(broken_append.snapshot(1'101).cut_sequence == 1);

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

    const std::vector<uint8> compressed{0xFF, 0xA5, 0xFF, 0xA5, 0xBC, 0xA5};
    send(receiver, packet(Command::Data, 1, compressed));
    assert(receiver.data_frames() == 2);
    assert(receiver.image_size() == 640);
    assert(receiver.image_data()[320] == 0xA5);
    assert(receiver.image_data()[639] == 0xA5);

    send(receiver, packet(Command::Print, 0, {1, 0, 0, 0x40}));
    assert(receiver.state() == Gbp::Terminal::ViewState::Preview);
    assert(receiver.palette() == 0xE4);
    const Gbr::Format::PrintControl zero_palette_print{1, 0, 0, 0x40};
    assert(receiver.print_control() == zero_palette_print);
    assert(receiver.buffer_full());
    receiver.confirm_preview();
    assert(receiver.state() == Gbp::Terminal::ViewState::Waiting);
    assert(receiver.image_size() == 0);
    assert(receiver.data_frames() == 0);

    Gbp::Terminal::PrinterSession session;
    const auto inquiry_packet = packet(Command::Inquiry, 0, {});
    const auto empty_data_packet = packet(Command::Data, 0, {});
    const auto full_data_packet = packet(Command::Data, 0, std::vector<uint8>(0x280, 0));
    const auto print_packet = packet(Command::Print, 0, {1, 0, 0xE4, 0x40});
    constexpr uint8 ready = Gbp::Protocol::STATUS_UNPROCESSED;
    constexpr uint8 printing = Gbp::Protocol::STATUS_FULL | Gbp::Protocol::STATUS_BUSY;
    constexpr uint8 done = Gbp::Protocol::STATUS_FULL;
    session.set_simulated_print_time(500'000);
    assert(reply_status(session, init, 100) == 0);
    assert(reply_status(session, full_data_packet, 200) == 0);
    assert(reply_status(session, inquiry_packet, 300) == ready);
    assert(reply_status(session, empty_data_packet, 400) == ready);
    // Core1 may mark the receive buffer full while PRINT's reply bytes clock out.
    assert(reply_status(session, print_packet, 1'000, true) == ready);
    assert(reply_status(session, inquiry_packet, 2'000) == printing);
    uint32 completed_at = 0;
    assert(!session.take_print_completion(500'999, completed_at));
    assert(reply_status(session, inquiry_packet, 500'999) == printing);
    assert(reply_status(session, inquiry_packet, 501'000) == done);
    assert(session.take_print_completion(501'000, completed_at) && completed_at == 501'000);
    assert(!session.take_print_completion(501'001, completed_at));
    for (int byte = 0; byte < 16; ++byte) {
        session.feed(0, false, 501'001);
    }
    assert(reply_status(session, inquiry_packet, 501'001) == done);

    session.reset();
    assert(reply_status(session, init, 600'000) == 0);
    assert(reply_status(session, full_data_packet, 600'010) == 0);
    session.abort_receive();
    assert(reply_status(session, inquiry_packet, 600'020) == 0);

    session.reset();
    session.set_simulated_print_time(10'000'000);
    assert(reply_status(session, init, 700'000) == 0);
    assert(reply_status(session, full_data_packet, 700'010) == 0);
    assert(reply_status(session, print_packet, 700'020) == ready);
    session.feed(0x88, false, 700'030);
    session.abort_receive();
    assert(reply_status(session, inquiry_packet, 5'700'020) == printing);
    assert(!session.take_print_completion(10'700'019, completed_at));
    assert(session.take_print_completion(10'700'020, completed_at));
    assert(reply_status(session, inquiry_packet, 10'700'020) == done);

    session.reset();
    assert(reply_status(session, init, 600'000) == 0);
    assert(reply_status(session, full_data_packet, 600'010) == 0);
    assert(reply_status(session, print_packet, 600'020) == ready);
    assert(reply_status(session, init, 600'100) == 0);
    assert(!session.take_print_completion(1'200'000, completed_at));

    session.set_simulated_print_time(100);
    assert(reply_status(session, full_data_packet, 699'000) == 0);
    assert(reply_status(session, print_packet, 700'000) == ready);
    assert(reply_status(session, init, 700'100) == 0);
    assert(session.take_print_completion(700'100, completed_at));

    session.reset();
    assert(reply_status(session, init, 0xFFFF'FFE0) == 0);
    assert(reply_status(session, full_data_packet, 0xFFFF'FFE1) == 0);
    assert(reply_status(session, print_packet, 0xFFFF'FFF0) == ready);
    assert(!session.take_print_completion(0x0000'0053, completed_at));
    assert(session.take_print_completion(0x0000'0054, completed_at));
    assert(reply_status(session, inquiry_packet, 0x0000'0054) == done);

    session.reset();
    session.set_simulated_print_time(0);
    assert(reply_status(session, init, 900) == 0);
    assert(reply_status(session, full_data_packet, 950) == 0);
    assert(reply_status(session, print_packet, 1'000) == ready);
    assert(session.take_print_completion(1'000, completed_at));
    assert(reply_status(session, inquiry_packet, 1'000) == done);
    assert(!session.take_print_completion(1'001, completed_at));

    session.reset();
    session.set_simulated_print_time(100);
    assert(reply_status(session, init, 1'800) == 0);
    assert(reply_status(session, full_data_packet, 1'900) == 0);
    assert(reply_status(session, print_packet, 2'000) == ready);
    assert(reply_status(session, inquiry_packet, 2'100) == done);
    assert(session.take_print_completion(2'100, completed_at));
    session.reset();
    assert(reply_status(session, init, 2'800) == 0);
    assert(reply_status(session, full_data_packet, 2'900) == 0);
    assert(reply_status(session, packet(Command::Print, 0, {1, 0, 0xE4, 0x40}, true), 3'000) ==
           static_cast<uint8>(ready | Gbp::Protocol::STATUS_CHECKSUM_ERROR));
    assert(reply_status(session, inquiry_packet, 3'050) == ready);
    assert((reply_status(session, packet(Command::Print, 0, {}), 3'100) &
            Gbp::Protocol::STATUS_BUSY) == 0);
    assert(!session.take_print_completion(3'200, completed_at));

    Gbp::Protocol::RleDecoder decoder;
    decoder.reset(true);
    std::array<uint8, 129> output{};
    assert(decoder.feed(0x01, output.data()) == 0);
    assert(decoder.feed(0x12, output.data()) == 1);
    assert(!decoder.complete());
    assert(decoder.feed(0x34, output.data()) == 1);
    assert(decoder.complete());
}
