// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ken(github.com/krovma)

#pragma once

#include "types.h"
#include "gbp/protocol/packet.h"

namespace Gbp::Protocol {

// Encode a complete Game Boy Printer request, including two bytes that clock
// out the printer's device ID and status. Returns zero if output is too small.
usize write_packet(Command command, bool compressed, const uint8* payload,
                         uint16 length, uint8* output, usize capacity);

}  // namespace Gbp::Protocol
