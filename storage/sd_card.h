#pragma once

#include "config/display_config.h"

namespace storage {

// Requires the active-low card-detect GPIO to have been initialized.
bool mount_initialized_card();
bool load_display_config(config::DisplayConfig& output);

}  // namespace storage
