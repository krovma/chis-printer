// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include <array>
#include <atomic>
#include <memory>

#include "types.h"
#include "config/printer_config.h"
#include "btprinter/escpos.h"
#include "btprinter/tspl.h"

namespace BtPrinter {

enum class PrintStatus : uint8 { Disabled, Idle, Printing, Success, Error };
enum class ConnectionState : uint8 { Disconnected, Connecting, Connected, Error };

enum class TraceKind : uint8 {
    ConnectRequest, DisconnectRequest, ConnectionTimeout,
    JobStart, HciState, HciLink, PinRequest, SspConfirmation,
    SdpStart, SdpService, SdpComplete, InquiryStart, InquiryMatch,
    InquiryComplete, NameRequest, NameComplete, RfcommOpen,
    RfcommClosed, FlowControl, JobComplete, Failure,
};

struct TraceEvent {
    uint32 uptime_ms = 0;
    TraceKind kind = TraceKind::HciState;
    uint32 value = 0;
    uint32 extra = 0;
};

const char* trace_kind_name(TraceKind kind);

// Bluetooth Classic SPP transport for QS thermal printers.  BTstack owns the
// asynchronous link; the application only starts/cancels a job and observes
// the byte counters from its main loop.
class BluetoothPrinter {
  public:
    bool initialize(const Config::M3Config& config);
    bool allocate_print_buffers(const Config::M3Config& config);
    // Release the print workspace after the job has finished or been canceled.
    void release_print_buffers();
    bool connect(bool forget_link_key);
    void disconnect();
    void service();
    // Consecutive GBR blocks share a print job, with optional feed between blocks.
    bool start(const uint8* image, usize size, uint8 palette,
               bool first_block, bool last_block,
               usize before_feed_dots = 0, usize after_feed_dots = 0,
               uint16 roll_label_length_mm = 0);

    bool initialized() const { return initialized_.load(std::memory_order_acquire); }
    bool hci_ready() const { return hci_ready_.load(std::memory_order_acquire); }
    // True only while the printer's RFCOMM channel is open.
    bool connected() const { return connected_.load(std::memory_order_acquire); }
    ConnectionState connection_state() const { return connection_state_.load(std::memory_order_acquire); }
    PrintStatus status() const { return status_.load(std::memory_order_acquire); }
    usize progress() const { return progress_.load(std::memory_order_acquire); }
    usize total() const { return total_.load(std::memory_order_acquire); }
    bool pop_trace(TraceEvent& event);
    uint32 trace_drops() const { return trace_drops_.load(std::memory_order_acquire); }

  private:
    static void packet_handler(uint8 packet_type, uint16 channel,
                               uint8* packet, uint16 size);
    static void sdp_handler(uint8 packet_type, uint16 channel,
                            uint8* packet, uint16 size);
    void handle_packet(uint8 packet_type, uint16 channel,
                       uint8* packet, uint16 size);
    void handle_sdp(uint8 packet_type, uint16 channel,
                    uint8* packet, uint16 size);
    void begin_sdp_query();
    void begin_inquiry();
    void select_discovered_address(const uint8* address);
    void query_next_remote_name();
    void close_link();
    void send_next_chunk();
    bool request_can_send_now();
    void fail_connection();
    void fail_print(const char* reason);
    void fail_print_link(const char* reason);
    void trace(TraceKind kind, uint32 value = 0, uint32 extra = 0);

    struct InquiryDevice {
        std::array<uint8, 6> address{};
        uint8 page_scan_repetition_mode = 0;
        uint16 clock_offset = 0;
    };
    struct PrintBuffers {
        Config::M3AdvanceConfig advance;
        Config::PrintProtocol protocol;
        Config::PaperType paper;
        Config::CenteringMode centering;
        uint16 media_width_mm;
        uint16 label_length_mm;
        uint16 label_gap_mm;
        uint8 print_direction;
        EscPosRasterEncoder escpos{};
        TsplRasterEncoder tspl{};
        PrintCommandEncoder* encoder = nullptr;
        std::array<uint8, MAX_BAND_BYTES> tx{};
        usize length = 0;
        usize offset = 0;
    };
    static constexpr usize MAX_INQUIRY_DEVICES = 16;
    static constexpr uint8 TRACE_CAPACITY = 32;

    static BluetoothPrinter* instance_;
    std::array<uint8, 6> address_{};
    bool address_known_ = false;
    std::array<char, 32> printer_name_{};
    std::array<char, 8> pin_{};
    std::atomic<PrintStatus> status_{PrintStatus::Disabled};
    std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};
    std::atomic_bool connected_{false};
    std::atomic<usize> progress_{0};
    std::atomic<usize> total_{0};
    std::atomic_bool initialized_{false};
    std::atomic_bool hci_ready_{false};
    std::atomic_bool connect_requested_{false};
    std::atomic_bool print_pending_{false};
    std::array<TraceEvent, TRACE_CAPACITY> trace_events_{};
    std::atomic<uint8> trace_head_{0};
    std::atomic<uint8> trace_tail_{0};
    std::atomic<uint32> trace_drops_{0};

    uint16 rfcomm_cid_ = 0;
    uint16 rfcomm_mtu_ = 0;
    uint16 hci_connection_handle_ = 0xffff;
    uint8 rfcomm_server_channel_ = 0;
    bool connect_queued_ = false;
    bool forget_link_key_queued_ = false;
    bool rfcomm_open_pending_ = false;
    bool disconnect_pending_ = false;
    uint64 connect_started_ms_ = 0;
    bool printer_flow_paused_ = false;
    bool link_key_retry_done_ = false;
    bool query_pending_ = false;
    bool inquiry_attempted_ = false;
    bool inquiry_pending_ = false;
    bool inquiry_match_found_ = false;
    bool name_query_pending_ = false;
    usize inquiry_count_ = 0;
    usize name_query_index_ = 0;
    std::array<InquiryDevice, MAX_INQUIRY_DEVICES> inquiry_devices_{};
    std::unique_ptr<PrintBuffers> print_buffers_{};
};

}  // namespace BtPrinter
