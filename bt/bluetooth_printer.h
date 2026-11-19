// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "config/firmware_config.h"
#include "m3/escpos.h"

namespace bt {

enum class PrintStatus : std::uint8_t { Disabled, Idle, Connecting, Printing, Success, Error };

enum class TraceKind : std::uint8_t {
    JobStart, HciState, HciLink, PinRequest, SspConfirmation,
    SdpStart, SdpService, SdpComplete, InquiryStart, InquiryMatch,
    InquiryComplete, NameRequest, NameComplete, RfcommOpen,
    RfcommClosed, FlowControl, JobComplete, Failure,
};

struct TraceEvent {
    std::uint32_t uptime_ms = 0;
    TraceKind kind = TraceKind::HciState;
    std::uint32_t value = 0;
    std::uint32_t extra = 0;
};

const char* trace_kind_name(TraceKind kind);

// Bluetooth Classic SPP transport for QS thermal printers.  BTstack owns the
// asynchronous link; the application only starts/cancels a job and observes
// the byte counters from its main loop.
class BluetoothPrinter {
  public:
    bool initialize(const config::M3Config& config);
    // Consecutive GBR blocks share one ESC/POS image stream. Only the first
    // block initializes the printer, and only the last block feeds the paper.
    bool start(const std::uint8_t* image, std::size_t size, std::uint8_t palette,
               bool first_block, bool last_block);
    void cancel();

    bool initialized() const { return initialized_.load(std::memory_order_acquire); }
    bool hci_ready() const { return hci_ready_.load(std::memory_order_acquire); }
    PrintStatus status() const { return status_.load(std::memory_order_acquire); }
    std::size_t progress() const { return progress_.load(std::memory_order_acquire); }
    std::size_t total() const { return total_.load(std::memory_order_acquire); }
    bool pop_trace(TraceEvent& event);
    std::uint32_t trace_drops() const { return trace_drops_.load(std::memory_order_acquire); }

  private:
    static void packet_handler(std::uint8_t packet_type, std::uint16_t channel,
                               std::uint8_t* packet, std::uint16_t size);
    static void sdp_handler(std::uint8_t packet_type, std::uint16_t channel,
                            std::uint8_t* packet, std::uint16_t size);
    void handle_packet(std::uint8_t packet_type, std::uint16_t channel,
                       std::uint8_t* packet, std::uint16_t size);
    void handle_sdp(std::uint8_t packet_type, std::uint16_t channel,
                    std::uint8_t* packet, std::uint16_t size);
    void begin_sdp_query();
    void begin_inquiry();
    void query_next_remote_name();
    void send_next_band();
    void fail();
    void trace(TraceKind kind, std::uint32_t value = 0, std::uint32_t extra = 0);

    struct InquiryDevice {
        std::array<std::uint8_t, 6> address{};
        std::uint8_t page_scan_repetition_mode = 0;
        std::uint16_t clock_offset = 0;
    };
    static constexpr std::size_t MAX_INQUIRY_DEVICES = 16;
    static constexpr std::uint8_t TRACE_CAPACITY = 32;

    static BluetoothPrinter* instance_;
    std::array<std::uint8_t, 6> address_{};
    std::array<char, 32> printer_name_{};
    std::array<char, 8> pin_{};
    std::atomic<PrintStatus> status_{PrintStatus::Disabled};
    std::atomic<std::size_t> progress_{0};
    std::atomic<std::size_t> total_{0};
    std::atomic_bool initialized_{false};
    std::atomic_bool hci_ready_{false};
    std::atomic_bool request_pending_{false};
    std::array<TraceEvent, TRACE_CAPACITY> trace_events_{};
    std::atomic<std::uint8_t> trace_head_{0};
    std::atomic<std::uint8_t> trace_tail_{0};
    std::atomic<std::uint32_t> trace_drops_{0};

    std::uint16_t rfcomm_cid_ = 0;
    std::uint16_t rfcomm_mtu_ = 0;
    std::uint8_t rfcomm_server_channel_ = 0;
    bool header_sent_ = false;
    bool footer_sent_ = false;
    bool printer_flow_paused_ = false;
    bool link_key_retry_done_ = false;
    bool query_pending_ = false;
    bool inquiry_attempted_ = false;
    bool inquiry_pending_ = false;
    bool inquiry_match_found_ = false;
    bool name_query_pending_ = false;
    std::size_t inquiry_count_ = 0;
    std::size_t name_query_index_ = 0;
    std::array<InquiryDevice, MAX_INQUIRY_DEVICES> inquiry_devices_{};
    std::size_t tx_length_ = 0;
    std::size_t tx_offset_ = 0;
    m3::EscPosRasterEncoder encoder_{};
    std::array<std::uint8_t, m3::BAND_BYTES> band_buffer_{};
};

}  // namespace bt
