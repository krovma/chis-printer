// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#include "gbp/host/print_job.h"

#include <algorithm>
#include <cstdio>

#include "types.h"

namespace Gbp::Host {
namespace {

constexpr usize DATA_PACKET_BYTES = 640;
constexpr usize PRINTER_BUFFER_BYTES = 9 * DATA_PACKET_BYTES;
constexpr uint8 BUSY_MASK = Protocol::STATUS_BUSY | Protocol::STATUS_UNPROCESSED;

}  // namespace

PrintResult PrintJob::send(PacketTransport& transport, const Protocol::Command command,
                           const uint8* payload, const uint16 length, uint8& status) {
    if (!transport.send(command, payload, length, status)) {
        printf("ERROR: GBP link cmd=0x%02X len=%u: no device ID\n",
               static_cast<unsigned>(command), static_cast<unsigned>(length));
        return PrintResult::NoPrinter;
    }
    if ((status & Protocol::STATUS_ERROR_MASK) != 0) {
        printf("ERROR: GBP status cmd=0x%02X len=%u status=0x%02X "
               "lowbat=%u other=%u paper=%u packet=%u checksum=%u\n",
               static_cast<unsigned>(command), static_cast<unsigned>(length),
               static_cast<unsigned>(status), static_cast<unsigned>((status >> 7) & 1u),
               static_cast<unsigned>((status >> 6) & 1u), static_cast<unsigned>((status >> 5) & 1u),
               static_cast<unsigned>((status >> 4) & 1u), static_cast<unsigned>(status & 1u));
        return PrintResult::PrinterError;
    }
    return PrintResult::Success;
}

PrintResult PrintJob::wait_ready(PacketTransport& transport, const char* phase, const uint8 mask,
                                 const uint32 attempts) {
    uint8 last_status = 0;
    for (uint32 attempt = 0; attempt < attempts; ++attempt) {
        uint8 status = 0;
        const PrintResult result = send(transport, Protocol::Command::Inquiry, nullptr, 0, status);
        if (result != PrintResult::Success) {
            printf("ERROR: GBP wait: %s, poll=%lu result=%s\n", phase,
                   static_cast<unsigned long>(attempt + 1), print_result_name(result));
            return result;
        }
        if ((status & mask) == 0) {
            if (attempt != 0) {
                printf("GBP ready: %s, polls=%lu status=0x%02X\n", phase,
                       static_cast<unsigned long>(attempt + 1), static_cast<unsigned>(status));
            }
            return PrintResult::Success;
        }
        if (attempt == 0 || status != last_status || (attempt + 1) % 50 == 0) {
            printf("GBP wait: %s, poll=%lu status=0x%02X mask=0x%02X\n", phase,
                   static_cast<unsigned long>(attempt + 1), static_cast<unsigned>(status),
                   static_cast<unsigned>(mask));
        }
        last_status = status;
        transport.pause_ms(100);
    }
    printf("ERROR: GBP timeout: %s, polls=%lu status=0x%02X mask=0x%02X\n", phase,
           static_cast<unsigned long>(attempts), static_cast<unsigned>(last_status),
           static_cast<unsigned>(mask));
    return PrintResult::Timeout;
}

PrintResult PrintJob::run(PacketTransport& transport, const uint8* data, const usize size,
                          const Gbr::Format::PrintControl& print_control, std::atomic<usize>& bytes_sent) {
    bytes_sent.store(0, std::memory_order_release);
    if (data == nullptr || size == 0 || size % 320 != 0 || size > 640 * 192) {
        printf("ERROR: GBP invalid image data=%p bytes=%lu\n", static_cast<const void*>(data),
               static_cast<unsigned long>(size));
        return PrintResult::InvalidImage;
    }

    printf("GBP print begin bytes=%lu sheets=0x%02X margins=0x%02X palette=0x%02X exposure=0x%02X\n",
           static_cast<unsigned long>(size), static_cast<unsigned>(print_control[0]),
           static_cast<unsigned>(print_control[1]), static_cast<unsigned>(print_control[2]),
           static_cast<unsigned>(print_control[3]));
    usize offset = 0;
    while (offset < size) {
        const usize band_start = offset;
        const usize band_size = std::min(size - offset, PRINTER_BUFFER_BYTES);
        const usize band_end = offset + band_size;
        printf("GBP band offset=%lu bytes=%lu\n", static_cast<unsigned long>(band_start),
               static_cast<unsigned long>(band_size));
        uint8 status = 0;
        PrintResult result = send(transport, Protocol::Command::Init, nullptr, 0, status);
        if (result != PrintResult::Success) {
            return result;
        }
        printf("GBP INIT status=0x%02X\n", static_cast<unsigned>(status));
        result = wait_ready(transport, "after INIT", BUSY_MASK | Protocol::STATUS_FULL, 50);
        if (result != PrintResult::Success) {
            return result;
        }

        while (offset < band_end) {
            const auto packet_size = static_cast<uint16>(std::min(band_end - offset, DATA_PACKET_BYTES));
            result = send(transport, Protocol::Command::Data, data + offset, packet_size, status);
            if (result != PrintResult::Success) {
                printf("ERROR: GBP DATA offset=%lu len=%u result=%s\n",
                       static_cast<unsigned long>(offset), static_cast<unsigned>(packet_size),
                       print_result_name(result));
                return result;
            }
            offset += packet_size;
            bytes_sent.store(offset, std::memory_order_release);
            result = wait_ready(transport, "after DATA", BUSY_MASK, 50);
            if (result != PrintResult::Success) {
                return result;
            }
        }

        result = send(transport, Protocol::Command::Data, nullptr, 0, status);
        if (result != PrintResult::Success) {
            return result;
        }
        printf("GBP DATA end offset=%lu status=0x%02X\n", static_cast<unsigned long>(offset),
               static_cast<unsigned>(status));
        result = wait_ready(transport, "after DATA end", BUSY_MASK, 50);
        if (result != PrintResult::Success) {
            return result;
        }

        // Keep only the outer margins when one image spans several printer bands.
        Gbr::Format::PrintControl control = print_control;
        if (band_end != band_size) {
            control[1] &= 0x0F;
        }
        if (offset != size) {
            control[1] &= 0xF0;
        }
        printf("GBP PRINT offset=%lu control=%02X %02X %02X %02X\n",
               static_cast<unsigned long>(offset), static_cast<unsigned>(control[0]),
               static_cast<unsigned>(control[1]), static_cast<unsigned>(control[2]),
               static_cast<unsigned>(control[3]));
        result = send(transport, Protocol::Command::Print, control.data(),
                      static_cast<uint16>(control.size()), status);
        if (result != PrintResult::Success) {
            return result;
        }
        printf("GBP PRINT accepted status=0x%02X\n", static_cast<unsigned>(status));
        result = wait_ready(transport, "after PRINT", BUSY_MASK, 1200);
        if (result != PrintResult::Success) {
            return result;
        }
        printf("GBP band complete offset=%lu\n", static_cast<unsigned long>(offset));
    }
    printf("GBP print complete bytes=%lu\n", static_cast<unsigned long>(size));
    return PrintResult::Success;
}

}  // namespace Gbp::Host
