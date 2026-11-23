// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "btprinter/bluetooth_printer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "types.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#if defined(KP_BT_DEBUG)
#define KP_BT_LOG_DEBUG(...) std::printf(__VA_ARGS__)
#define KP_BT_TRACE_DEBUG(...) trace(__VA_ARGS__)
#else
#define KP_BT_LOG_DEBUG(...) ((void)0)
#define KP_BT_TRACE_DEBUG(...) ((void)0)
#endif

namespace BtPrinter {
namespace {
constexpr uint64 CONNECTION_TIMEOUT_MS = 60'000;
}

const char* trace_kind_name(const TraceKind kind) {
    switch (kind) {
        case TraceKind::ConnectRequest: return "connect_request";
        case TraceKind::DisconnectRequest: return "disconnect_request";
        case TraceKind::ConnectionTimeout: return "connection_timeout";
        case TraceKind::JobStart: return "job_start";
        case TraceKind::HciState: return "hci_state";
        case TraceKind::HciLink: return "hci_link";
        case TraceKind::PinRequest: return "pin_request";
        case TraceKind::SspConfirmation: return "ssp_confirm";
        case TraceKind::SdpStart: return "sdp_start";
        case TraceKind::SdpService: return "sdp_service";
        case TraceKind::SdpComplete: return "sdp_complete";
        case TraceKind::InquiryStart: return "inquiry_start";
        case TraceKind::InquiryMatch: return "inquiry_match";
        case TraceKind::InquiryComplete: return "inquiry_complete";
        case TraceKind::NameRequest: return "name_request";
        case TraceKind::NameComplete: return "name_complete";
        case TraceKind::RfcommOpen: return "rfcomm_open";
        case TraceKind::RfcommClosed: return "rfcomm_closed";
        case TraceKind::FlowControl: return "flow_control";
        case TraceKind::JobComplete: return "job_complete";
        case TraceKind::Failure: return "failure";
    }
    return "unknown";
}

BluetoothPrinter* BluetoothPrinter::instance_ = nullptr;

void BluetoothPrinter::trace(const TraceKind kind, const uint32 value,
                             const uint32 extra) {
    const uint8 head = trace_head_.load(std::memory_order_relaxed);
    const uint8 next = static_cast<uint8>((head + 1u) % TRACE_CAPACITY);
    if (next == trace_tail_.load(std::memory_order_acquire)) {
        trace_drops_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    trace_events_[head] = {time_us_32() / 1000u, kind, value, extra};
    trace_head_.store(next, std::memory_order_release);
}

bool BluetoothPrinter::pop_trace(TraceEvent& event) {
    const uint8 tail = trace_tail_.load(std::memory_order_relaxed);
    if (tail == trace_head_.load(std::memory_order_acquire)) {
        return false;
    }
    event = trace_events_[tail];
    trace_tail_.store(static_cast<uint8>((tail + 1u) % TRACE_CAPACITY),
                      std::memory_order_release);
    return true;
}

bool BluetoothPrinter::allocate_print_buffers(const Config::M3Config& config) {
    if (print_buffers_) {
        return true;
    }
    const auto settings = config.resolved_advance();
    std::unique_ptr<PrintBuffers> buffers{
        new (std::nothrow) PrintBuffers{settings, config.paper_type, config.centering}};
    if (!buffers) {
        std::printf("ERROR: M3 BT print buffer allocation failed\n");
        return false;
    }
    buffers->encoder = protocol_encoder(config.protocol, buffers->escpos);
    if (!Config::printer_type_supports_scale(config.printer_type, settings.image_scale_quarters) ||
        settings.printable_width_dots < Config::scaled_dots(SOURCE_WIDTH, settings.image_scale_quarters) ||
        buffers->encoder == nullptr ||
        config.label_length_mm == 0 || config.label_length_mm > Config::MAX_LABEL_LENGTH_MM ||
        config.label_gap_mm > Config::MAX_LABEL_GAP_MM ||
        (config.centering != Config::CenteringMode::Printer &&
         config.centering != Config::CenteringMode::Firmware) ||
        (config.paper_type != Config::PaperType::Roll && config.paper_type != Config::PaperType::Label)) {
        std::printf("ERROR: M3 BT invalid printer profile type=%u width=%u scale_quarters=%u\n",
                    static_cast<unsigned>(config.printer_type),
                    static_cast<unsigned>(settings.printable_width_dots),
                    static_cast<unsigned>(settings.image_scale_quarters));
        return false;
    }
    const bool active = initialized();
    if (active) cyw43_arch_lwip_begin();
    print_buffers_ = std::move(buffers);
    if (active) cyw43_arch_lwip_end();
    return true;
}

void BluetoothPrinter::release_print_buffers() {
    const bool active = initialized();
    if (active) cyw43_arch_lwip_begin();
    print_pending_.store(false, std::memory_order_release);
    status_.store(active ? PrintStatus::Idle : PrintStatus::Disabled, std::memory_order_release);
    progress_.store(0, std::memory_order_release);
    total_.store(0, std::memory_order_release);
    print_buffers_.reset();
    if (active) cyw43_arch_lwip_end();
}

bool BluetoothPrinter::initialize(const Config::M3Config& config) {
    if (initialized_.load(std::memory_order_acquire)) {
        KP_BT_LOG_DEBUG("M3 BT init already ready t=%lu ms\n",
                        static_cast<unsigned long>(time_us_32() / 1000u));
        return true;
    }
    printer_name_ = config.printer_name;
    std::memset(pin_.data(), 0, pin_.size());
    std::memcpy(pin_.data(), config.pin.data(), pin_.size() - 1);
    if (pin_[0] == '\0') {
        std::printf("ERROR: M3 BT init invalid PIN\n");
        connection_state_.store(ConnectionState::Error, std::memory_order_release);
        return false;
    }

    instance_ = this;
    KP_BT_LOG_DEBUG("M3 BT cyw43_arch_init begin t=%lu ms\n",
                    static_cast<unsigned long>(time_us_32() / 1000u));
    const int arch_result = cyw43_arch_init();
    KP_BT_LOG_DEBUG("M3 BT cyw43_arch_init end rc=%d t=%lu ms\n", arch_result,
                    static_cast<unsigned long>(time_us_32() / 1000u));
    if (arch_result != 0) {
        std::printf("ERROR: M3 BT cyw43_arch_init rc=%d\n", arch_result);
        status_.store(PrintStatus::Error, std::memory_order_release);
        connection_state_.store(ConnectionState::Error, std::memory_order_release);
        return false;
    }
    // These guards are no-ops in polling mode, but keep BTstack calls safe if
    // a background CYW43 architecture is selected in the future.
    KP_BT_LOG_DEBUG("M3 BT context lock begin\n");
    cyw43_arch_lwip_begin();
    KP_BT_LOG_DEBUG("M3 BT context lock acquired\n");
    l2cap_init();
    KP_BT_LOG_DEBUG("M3 BT l2cap ready\n");
    rfcomm_init();
    KP_BT_LOG_DEBUG("M3 BT rfcomm ready\n");
    static btstack_packet_callback_registration_t registration;
    registration.callback = &BluetoothPrinter::packet_handler;
    hci_add_event_handler(&registration);
    KP_BT_LOG_DEBUG("M3 BT HCI handler ready; power begin\n");
    const int power_result = hci_power_control(HCI_POWER_ON);
    KP_BT_LOG_DEBUG("M3 BT HCI power requested rc=%d\n", power_result);
    cyw43_arch_lwip_end();
    KP_BT_LOG_DEBUG("M3 BT context lock released\n");
    initialized_.store(true, std::memory_order_release);
    status_.store(power_result == ERROR_CODE_SUCCESS ? PrintStatus::Idle : PrintStatus::Error,
                  std::memory_order_release);
    if (power_result != ERROR_CODE_SUCCESS) {
        std::printf("ERROR: M3 BT HCI power rc=%d\n", power_result);
        connection_state_.store(ConnectionState::Error, std::memory_order_release);
    }
    return power_result == ERROR_CODE_SUCCESS;
}

void BluetoothPrinter::close_link() {
    if (disconnect_pending_) {
        return;
    }
    if (hci_connection_handle_ != HCI_CON_HANDLE_INVALID) {
        const uint16 handle = hci_connection_handle_;
        disconnect_pending_ = true;
        const uint8 result = gap_disconnect(handle);
        if (result == ERROR_CODE_SUCCESS || result == ERROR_CODE_COMMAND_DISALLOWED) {
            return;
        }
        disconnect_pending_ = false;
        hci_connection_handle_ = HCI_CON_HANDLE_INVALID;
    }
    if (rfcomm_cid_ != 0) {
        const uint16 cid = rfcomm_cid_;
        disconnect_pending_ = true;
        if (rfcomm_disconnect(cid) == ERROR_CODE_SUCCESS) {
            return;
        }
        disconnect_pending_ = false;
    }
    rfcomm_cid_ = 0;
    rfcomm_mtu_ = 0;
    hci_connection_handle_ = HCI_CON_HANDLE_INVALID;
}

bool BluetoothPrinter::connect(const bool forget_link_key) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    cyw43_arch_lwip_begin();
    KP_BT_TRACE_DEBUG(TraceKind::ConnectRequest, forget_link_key ? 1u : 0u,
                      static_cast<uint32>(connection_state_.load(std::memory_order_acquire)));
    const int power_result = hci_power_control(HCI_POWER_ON);
    if (power_result != ERROR_CODE_SUCCESS) {
        trace(TraceKind::Failure, static_cast<uint32>(power_result));
        connection_state_.store(ConnectionState::Error, std::memory_order_release);
        cyw43_arch_lwip_end();
        return false;
    }
    connect_requested_.store(false, std::memory_order_release);
    print_pending_.store(false, std::memory_order_release);
    status_.store(PrintStatus::Idle, std::memory_order_release);
    if (inquiry_pending_) {
        gap_inquiry_stop();
    }
    connect_queued_ = true;
    forget_link_key_queued_ = forget_link_key;
    connect_started_ms_ = time_us_64() / 1000u;
    connected_.store(false, std::memory_order_release);
    connection_state_.store(ConnectionState::Connecting, std::memory_order_release);
    close_link();
    cyw43_arch_lwip_end();
    return true;
}

void BluetoothPrinter::disconnect() {
    const bool initialized = initialized_.load(std::memory_order_acquire);
    if (initialized) {
        cyw43_arch_lwip_begin();
    }
    if (initialized) {
        KP_BT_TRACE_DEBUG(TraceKind::DisconnectRequest,
                          static_cast<uint32>(connection_state_.load(std::memory_order_acquire)));
    }
    connect_requested_.store(false, std::memory_order_release);
    connect_queued_ = false;
    forget_link_key_queued_ = false;
    print_pending_.store(false, std::memory_order_release);
    status_.store(initialized ? PrintStatus::Idle : PrintStatus::Disabled, std::memory_order_release);
    if (inquiry_pending_) {
        gap_inquiry_stop();
    }
    connected_.store(false, std::memory_order_release);
    connection_state_.store(ConnectionState::Disconnected, std::memory_order_release);
    if (initialized) {
        close_link();
    }
    if (initialized) {
        cyw43_arch_lwip_end();
    }
}

void BluetoothPrinter::service() {
    if (!initialized_.load(std::memory_order_acquire) ||
        connection_state_.load(std::memory_order_acquire) != ConnectionState::Connecting) {
        return;
    }
    cyw43_arch_lwip_begin();
    if (time_us_64() / 1000u - connect_started_ms_ >= CONNECTION_TIMEOUT_MS) {
        trace(TraceKind::ConnectionTimeout, static_cast<uint32>(query_pending_),
              static_cast<uint32>(rfcomm_open_pending_));
        if (inquiry_pending_) {
            gap_inquiry_stop();
        }
        fail_connection();
        cyw43_arch_lwip_end();
        return;
    }
    if (!hci_ready_.load(std::memory_order_acquire) || query_pending_ || inquiry_pending_ || name_query_pending_ ||
        rfcomm_open_pending_ || disconnect_pending_ || rfcomm_cid_ != 0 ||
        hci_connection_handle_ != HCI_CON_HANDLE_INVALID || !sdp_client_ready()) {
        cyw43_arch_lwip_end();
        return;
    }
    if (connect_queued_) {
        connect_queued_ = false;
        connect_requested_.store(true, std::memory_order_release);
        inquiry_attempted_ = false;
        inquiry_match_found_ = false;
        inquiry_count_ = 0;
        name_query_index_ = 0;
        rfcomm_server_channel_ = 0;
        link_key_retry_done_ = false;
        if (address_known_ && forget_link_key_queued_) {
            gap_drop_link_key_for_bd_addr(address_.data());
        }
    }
    if (connect_requested_.load(std::memory_order_acquire)) {
        if (address_known_) {
            begin_sdp_query();
        } else {
            begin_inquiry();
        }
    }
    cyw43_arch_lwip_end();
}

bool BluetoothPrinter::start(const uint8* image, const usize size,
                             const uint8 palette, const bool first_block,
                             const bool last_block, const usize before_feed_dots,
                             const usize after_feed_dots) {
    if (!connected() || image == nullptr || size == 0) {
        return false;
    }
    cyw43_arch_lwip_begin();
    auto* buffers = print_buffers_.get();
    if (buffers == nullptr || !connected() || rfcomm_cid_ == 0 || status() == PrintStatus::Printing) {
        cyw43_arch_lwip_end();
        return false;
    }
    const auto& advance = buffers->advance;
    if (!buffers->encoder->reset({image, size, palette, advance.image_scale_quarters, first_block, last_block,
                                  buffers->paper, before_feed_dots, after_feed_dots,
                                  advance.printable_width_dots, buffers->centering, advance.left_margin_dots})) {
        status_.store(PrintStatus::Error, std::memory_order_release);
        cyw43_arch_lwip_end();
        return false;
    }
    buffers->length = 0;
    buffers->offset = 0;
    progress_.store(0, std::memory_order_release);
    total_.store(buffers->encoder->encoded_size(), std::memory_order_release);
    KP_BT_TRACE_DEBUG(TraceKind::JobStart, static_cast<uint32>(size),
                      static_cast<uint32>(total()));
    print_pending_.store(true, std::memory_order_release);
    status_.store(PrintStatus::Printing, std::memory_order_release);
    const bool send_ready = printer_flow_paused_ || request_can_send_now();
    cyw43_arch_lwip_end();
    return send_ready;
}

void BluetoothPrinter::packet_handler(const uint8 packet_type, const uint16 channel,
                                      uint8* packet, const uint16 size) {
    if (instance_ != nullptr) {
        instance_->handle_packet(packet_type, channel, packet, size);
    }
}

void BluetoothPrinter::sdp_handler(const uint8 packet_type, const uint16 channel,
                                   uint8* packet, const uint16 size) {
    if (instance_ != nullptr) {
        instance_->handle_sdp(packet_type, channel, packet, size);
    }
}

void BluetoothPrinter::handle_packet(const uint8 packet_type, const uint16 channel,
                                     uint8* packet, const uint16 size) {
    if (packet_type == RFCOMM_DATA_PACKET) {
        if (!connected() || channel != rfcomm_cid_ || size == 0 || packet == nullptr) {
            return;
        }
        if (packet[0] == 0x08) {
            trace(TraceKind::FlowControl, packet[0], size);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::FlowControl, packet[0], size);
        }
        if (packet[0] == 0x13) {
            printer_flow_paused_ = true;
        } else if (packet[0] == 0x11) {
            printer_flow_paused_ = false;
            if (print_pending_.load(std::memory_order_acquire) && rfcomm_cid_ != 0) {
                request_can_send_now();
            }
        } else if (packet[0] == 0x08 && print_pending_.load(std::memory_order_acquire)) {
            fail_print();
        }
        return;
    }
    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }
    const uint8 event = hci_event_packet_get_type(packet);
    if (event == HCI_EVENT_CONNECTION_COMPLETE || event == HCI_EVENT_DISCONNECTION_COMPLETE ||
        event == HCI_EVENT_AUTHENTICATION_COMPLETE || event == HCI_EVENT_ENCRYPTION_CHANGE ||
        event == HCI_EVENT_LINK_KEY_REQUEST || event == HCI_EVENT_LINK_KEY_NOTIFICATION ||
        event == HCI_EVENT_SIMPLE_PAIRING_COMPLETE) {
        const uint32 detail = event == HCI_EVENT_DISCONNECTION_COMPLETE ?
            hci_event_disconnection_complete_get_reason(packet) :
            (event == HCI_EVENT_LINK_KEY_REQUEST || event == HCI_EVENT_LINK_KEY_NOTIFICATION || size <= 2) ?
            0u : packet[2];
        const bool unexpected_disconnect = event == HCI_EVENT_DISCONNECTION_COMPLETE &&
            !disconnect_pending_ && (connected_.load(std::memory_order_acquire) ||
                                     connect_requested_.load(std::memory_order_acquire));
        const bool link_error = unexpected_disconnect ||
            ((event == HCI_EVENT_CONNECTION_COMPLETE || event == HCI_EVENT_AUTHENTICATION_COMPLETE ||
              event == HCI_EVENT_ENCRYPTION_CHANGE || event == HCI_EVENT_SIMPLE_PAIRING_COMPLETE) && detail != 0);
        if (link_error) {
            trace(TraceKind::HciLink, event, detail);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::HciLink, event, detail);
        }
    }
    if (event == BTSTACK_EVENT_STATE) {
        const uint8 state = btstack_event_state_get_state(packet);
        if (state != HCI_STATE_WORKING && connected_.load(std::memory_order_acquire)) {
            trace(TraceKind::HciState, state);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::HciState, state);
        }
        if (state == HCI_STATE_WORKING) {
            hci_ready_.store(true, std::memory_order_release);
        } else {
            const bool was_ready = hci_ready_.exchange(false, std::memory_order_acq_rel);
            if (was_ready) {
                const bool reconnecting = connection_state_.load(std::memory_order_acquire) ==
                                          ConnectionState::Connecting;
                if (print_pending_.load(std::memory_order_acquire)) {
                    fail_print();
                }
                connected_.store(false, std::memory_order_release);
                rfcomm_cid_ = 0;
                rfcomm_mtu_ = 0;
                hci_connection_handle_ = HCI_CON_HANDLE_INVALID;
                disconnect_pending_ = false;
                rfcomm_open_pending_ = false;
                query_pending_ = false;
                inquiry_pending_ = false;
                name_query_pending_ = false;
                connect_requested_.store(false, std::memory_order_release);
                if (reconnecting) {
                    connect_queued_ = true;
                    connect_started_ms_ = time_us_64() / 1000u;
                } else if (connection_state_.load(std::memory_order_acquire) == ConnectionState::Connected) {
                    connection_state_.store(ConnectionState::Disconnected, std::memory_order_release);
                }
            }
        }
        return;
    }
    if (event == HCI_EVENT_DISCONNECTION_COMPLETE && hci_connection_handle_ != HCI_CON_HANDLE_INVALID &&
        hci_event_disconnection_complete_get_connection_handle(packet) == hci_connection_handle_) {
        rfcomm_cid_ = 0;
        rfcomm_mtu_ = 0;
        hci_connection_handle_ = HCI_CON_HANDLE_INVALID;
        disconnect_pending_ = false;
        connected_.store(false, std::memory_order_release);
        if (connection_state_.load(std::memory_order_acquire) == ConnectionState::Connected) {
            connection_state_.store(ConnectionState::Disconnected, std::memory_order_release);
        }
        if (print_pending_.load(std::memory_order_acquire)) {
            fail_print();
        }
        return;
    }
    if (event == HCI_EVENT_PIN_CODE_REQUEST) {
        KP_BT_TRACE_DEBUG(TraceKind::PinRequest);
        bd_addr_t address;
        hci_event_pin_code_request_get_bd_addr(packet, address);
        gap_pin_code_response(address, pin_.data());
        return;
    }
    if (event == HCI_EVENT_USER_CONFIRMATION_REQUEST) {
        KP_BT_TRACE_DEBUG(TraceKind::SspConfirmation);
        bd_addr_t address;
        hci_event_user_confirmation_request_get_bd_addr(packet, address);
        gap_ssp_confirmation_response(address);
        return;
    }
    if (event == GAP_EVENT_INQUIRY_RESULT && inquiry_pending_) {
        if (!connect_requested_.load(std::memory_order_acquire)) {
            return;
        }
        if (inquiry_match_found_) {
            return;
        }
        bd_addr_t found_address;
        gap_event_inquiry_result_get_bd_addr(packet, found_address);
        const usize target_length = std::strlen(printer_name_.data());
        if (gap_event_inquiry_result_get_name_available(packet)) {
            const usize found_length = gap_event_inquiry_result_get_name_len(packet);
            if (found_length == target_length &&
                std::memcmp(gap_event_inquiry_result_get_name(packet), printer_name_.data(), target_length) == 0) {
                select_discovered_address(found_address);
                inquiry_match_found_ = true;
                KP_BT_TRACE_DEBUG(TraceKind::InquiryMatch, 1);
                gap_inquiry_stop();
                return;
            }
        }
        if (inquiry_count_ < inquiry_devices_.size()) {
            bool seen = false;
            for (usize index = 0; index < inquiry_count_; ++index) {
                if (std::memcmp(inquiry_devices_[index].address.data(), found_address, 6) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                InquiryDevice& found = inquiry_devices_[inquiry_count_++];
                std::memcpy(found.address.data(), found_address, found.address.size());
                found.page_scan_repetition_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
                found.clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
            }
        }
        return;
    }
    if (event == GAP_EVENT_INQUIRY_COMPLETE && inquiry_pending_) {
        inquiry_pending_ = false;
        KP_BT_TRACE_DEBUG(TraceKind::InquiryComplete, static_cast<uint32>(inquiry_count_),
                          inquiry_match_found_ ? 1u : 0u);
        if (!connect_requested_.load(std::memory_order_acquire)) {
            return;
        }
        if (inquiry_match_found_) {
            begin_sdp_query();
        } else {
            query_next_remote_name();
        }
        return;
    }
    if (event == HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE && name_query_pending_) {
        name_query_pending_ = false;
        if (!connect_requested_.load(std::memory_order_acquire)) {
            return;
        }
        const usize target_length = std::strlen(printer_name_.data());
        const uint8 name_status = hci_event_remote_name_request_complete_get_status(packet);
        if (hci_event_remote_name_request_complete_get_status(packet) == ERROR_CODE_SUCCESS &&
            size >= 9 + target_length) {
            const char* found_name = hci_event_remote_name_request_complete_get_remote_name(packet);
            const bool exact_length = size == 9 + target_length || found_name[target_length] == '\0';
            if (exact_length && std::memcmp(found_name, printer_name_.data(), target_length) == 0) {
                KP_BT_TRACE_DEBUG(TraceKind::NameComplete, name_status, 1);
                bd_addr_t found_address;
                hci_event_remote_name_request_complete_get_bd_addr(packet, found_address);
                select_discovered_address(found_address);
                begin_sdp_query();
                return;
            }
        }
        if (name_status != ERROR_CODE_SUCCESS) {
            trace(TraceKind::NameComplete, name_status, 0);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::NameComplete, name_status, 0);
        }
        query_next_remote_name();
        return;
    }
    if (event == RFCOMM_EVENT_CHANNEL_OPENED) {
        rfcomm_open_pending_ = false;
        const uint8 status = rfcomm_event_channel_opened_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            trace(TraceKind::RfcommOpen, status);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::RfcommOpen, status, rfcomm_event_channel_opened_get_max_frame_size(packet));
        }
        if (status != ERROR_CODE_SUCCESS) {
            if (connect_requested_.load(std::memory_order_acquire)) {
                if (status == L2CAP_CONNECTION_PIN_OR_LINK_KEY_MISSING && !link_key_retry_done_) {
                    link_key_retry_done_ = true;
                    gap_drop_link_key_for_bd_addr(address_.data());
                    if (rfcomm_create_channel(&BluetoothPrinter::packet_handler, address_.data(),
                                              rfcomm_server_channel_, nullptr) == ERROR_CODE_SUCCESS) {
                        rfcomm_open_pending_ = true;
                        return;
                    }
                }
                if (inquiry_attempted_) {
                    fail_connection();
                } else {
                    begin_inquiry();
                }
            }
            return;
        }
        rfcomm_cid_ = rfcomm_event_channel_opened_get_rfcomm_cid(packet);
        rfcomm_mtu_ = rfcomm_event_channel_opened_get_max_frame_size(packet);
        hci_connection_handle_ = rfcomm_event_channel_opened_get_con_handle(packet);
        if (!connect_requested_.load(std::memory_order_acquire)) {
            close_link();
            return;
        }
        connect_requested_.store(false, std::memory_order_release);
        forget_link_key_queued_ = false;
        printer_flow_paused_ = false;
        connected_.store(true, std::memory_order_release);
        connection_state_.store(ConnectionState::Connected, std::memory_order_release);
        status_.store(PrintStatus::Idle, std::memory_order_release);
        return;
    }
    if (event == RFCOMM_EVENT_CAN_SEND_NOW) {
        if (rfcomm_event_can_send_now_get_rfcomm_cid(packet) == rfcomm_cid_) {
            send_next_chunk();
        }
        return;
    }
    if (event == RFCOMM_EVENT_CHANNEL_CLOSED) {
        const uint16 closed_cid = rfcomm_event_channel_closed_get_rfcomm_cid(packet);
        if (closed_cid == rfcomm_cid_ && connected_.load(std::memory_order_acquire)) {
            trace(TraceKind::RfcommClosed, closed_cid);
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::RfcommClosed, closed_cid);
        }
        if (closed_cid != rfcomm_cid_) {
            return;
        }
        rfcomm_cid_ = 0;
        rfcomm_mtu_ = 0;
        if (hci_connection_handle_ == HCI_CON_HANDLE_INVALID) {
            disconnect_pending_ = false;
        }
        connected_.store(false, std::memory_order_release);
        if (connection_state_.load(std::memory_order_acquire) == ConnectionState::Connected) {
            connection_state_.store(ConnectionState::Disconnected, std::memory_order_release);
        }
        if (print_pending_.load(std::memory_order_acquire)) {
            fail_print();
        }
    }
}

void BluetoothPrinter::handle_sdp(const uint8 packet_type, const uint16 channel,
                                  uint8* packet, const uint16 size) {
    (void)packet_type;
    (void)channel;
    (void)size;
    if (!query_pending_) {
        return;
    }
    if (hci_event_packet_get_type(packet) == SDP_EVENT_QUERY_RFCOMM_SERVICE) {
        rfcomm_server_channel_ = sdp_event_query_rfcomm_service_get_rfcomm_channel(packet);
        KP_BT_TRACE_DEBUG(TraceKind::SdpService, rfcomm_server_channel_);
        return;
    }
    if (hci_event_packet_get_type(packet) != SDP_EVENT_QUERY_COMPLETE) {
        return;
    }
    query_pending_ = false;
    const uint8 status = sdp_event_query_complete_get_status(packet);
    if (status != ERROR_CODE_SUCCESS || rfcomm_server_channel_ == 0) {
        trace(TraceKind::SdpComplete, status, rfcomm_server_channel_);
    } else {
        KP_BT_TRACE_DEBUG(TraceKind::SdpComplete, status, rfcomm_server_channel_);
    }
    if (!connect_requested_.load(std::memory_order_acquire)) {
        return;
    }
    if (status != ERROR_CODE_SUCCESS || rfcomm_server_channel_ == 0) {
        if (inquiry_attempted_) {
            fail_connection();
        } else {
            begin_inquiry();
        }
        return;
    }
    if (rfcomm_create_channel(&BluetoothPrinter::packet_handler, address_.data(), rfcomm_server_channel_, nullptr) ==
        ERROR_CODE_SUCCESS) {
        rfcomm_open_pending_ = true;
    } else {
        if (inquiry_attempted_) {
            fail_connection();
        } else {
            begin_inquiry();
        }
    }
}

void BluetoothPrinter::begin_sdp_query() {
    if (query_pending_ || !connect_requested_.load(std::memory_order_acquire)) {
        return;
    }
    query_pending_ = true;
    rfcomm_server_channel_ = 0;
    const uint8 result = sdp_client_query_rfcomm_channel_and_name_for_uuid(
        &BluetoothPrinter::sdp_handler, address_.data(), BLUETOOTH_SERVICE_CLASS_SERIAL_PORT);
    if (result != ERROR_CODE_SUCCESS && result != SDP_QUERY_BUSY) {
        trace(TraceKind::SdpStart, result);
    } else {
        KP_BT_TRACE_DEBUG(TraceKind::SdpStart, result);
    }
    if (result != ERROR_CODE_SUCCESS) {
        query_pending_ = false;
        if (result == SDP_QUERY_BUSY) {
            return;
        }
        if (inquiry_attempted_) {
            fail_connection();
        } else {
            begin_inquiry();
        }
    }
}

void BluetoothPrinter::begin_inquiry() {
    if (!connect_requested_.load(std::memory_order_acquire)) {
        return;
    }
    if (inquiry_attempted_) {
        fail_connection();
        return;
    }
    inquiry_attempted_ = true;
    inquiry_pending_ = true;
    inquiry_match_found_ = false;
    inquiry_count_ = 0;
    name_query_index_ = 0;
    const int result = gap_inquiry_start(5);
    if (result != 0) {
        trace(TraceKind::InquiryStart, static_cast<uint32>(result));
    } else {
        KP_BT_TRACE_DEBUG(TraceKind::InquiryStart, static_cast<uint32>(result));
    }
    if (result != 0) {
        inquiry_pending_ = false;
        fail_connection();
    }
}

void BluetoothPrinter::select_discovered_address(const uint8* address) {
    std::memcpy(address_.data(), address, address_.size());
    address_known_ = true;
    if (forget_link_key_queued_) {
        gap_drop_link_key_for_bd_addr(address_.data());
        forget_link_key_queued_ = false;
    }
}

void BluetoothPrinter::query_next_remote_name() {
    while (name_query_index_ < inquiry_count_) {
        const InquiryDevice& device = inquiry_devices_[name_query_index_++];
        const int result = gap_remote_name_request(device.address.data(), device.page_scan_repetition_mode,
                                                   device.clock_offset | 0x8000);
        if (result != 0) {
            trace(TraceKind::NameRequest, static_cast<uint32>(name_query_index_), static_cast<uint32>(result));
        } else {
            KP_BT_TRACE_DEBUG(TraceKind::NameRequest, static_cast<uint32>(name_query_index_),
                              static_cast<uint32>(result));
        }
        if (result == 0) {
            name_query_pending_ = true;
            return;
        }
    }
    fail_connection();
}

void BluetoothPrinter::send_next_chunk() {
    auto* buffers = print_buffers_.get();
    if (buffers == nullptr || rfcomm_cid_ == 0 || !print_pending_.load(std::memory_order_acquire) ||
        printer_flow_paused_) {
        return;
    }
    if (buffers->offset == buffers->length) {
        buffers->offset = 0;
        const auto result = buffers->encoder->next_chunk(buffers->tx.data(), buffers->tx.size(), buffers->length);
        if (result == EncodeResult::Complete) {
            print_pending_.store(false, std::memory_order_release);
            status_.store(PrintStatus::Success, std::memory_order_release);
            KP_BT_TRACE_DEBUG(TraceKind::JobComplete, static_cast<uint32>(progress()));
            return;
        }
        if (result == EncodeResult::Error) {
            fail_print();
            return;
        }
    }
    const usize frame_limit = rfcomm_mtu_ == 0 ? 512 : rfcomm_mtu_;
    const usize chunk = std::min(frame_limit, buffers->length - buffers->offset);
    if (rfcomm_send(rfcomm_cid_, buffers->tx.data() + buffers->offset, static_cast<uint16>(chunk)) !=
        ERROR_CODE_SUCCESS) {
        fail_print_link();
        return;
    }
    buffers->offset += chunk;
    progress_.fetch_add(chunk, std::memory_order_acq_rel);
    request_can_send_now();
}

bool BluetoothPrinter::request_can_send_now() {
    if (rfcomm_cid_ != 0 && rfcomm_request_can_send_now_event(rfcomm_cid_) == ERROR_CODE_SUCCESS) {
        return true;
    }
    fail_print_link();
    return false;
}

void BluetoothPrinter::fail_connection() {
    trace(TraceKind::Failure, rfcomm_cid_, rfcomm_server_channel_);
    connect_requested_.store(false, std::memory_order_release);
    connect_queued_ = false;
    connected_.store(false, std::memory_order_release);
    connection_state_.store(ConnectionState::Error, std::memory_order_release);
}

void BluetoothPrinter::fail_print() {
    trace(TraceKind::Failure, rfcomm_cid_, rfcomm_server_channel_);
    print_pending_.store(false, std::memory_order_release);
    status_.store(PrintStatus::Error, std::memory_order_release);
}

void BluetoothPrinter::fail_print_link() {
    fail_print();
    connected_.store(false, std::memory_order_release);
    connection_state_.store(ConnectionState::Error, std::memory_order_release);
    close_link();
}

}  // namespace BtPrinter

#undef KP_BT_TRACE_DEBUG
#undef KP_BT_LOG_DEBUG
