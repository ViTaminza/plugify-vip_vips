// vip_vips — constants shared by the plugin and its helpers.
//
// The plugin is pure s2sdk: every safety rule of PITFALLS.md that applies to a client query lives
// in guards.cpp / commands.cpp (see the header comment of plugin.cpp). There is no s2guard and no
// pisexcompat here.

#pragma once

#include <cstdint>

namespace vips {

// The GetLogTag() of the original Pisex module ("[VIP-VIPS]").
inline constexpr char kLogTag[] = "VIP-VIPS";

// vip_vips.ini inside the Plugify configs tree: addons/plugify/configs/vip/vip_vips.ini.
inline constexpr char kConfigFile[] = "vip/vip_vips.ini";

// The original's fallback "type" when vip_vips.ini is missing or unreadable.
inline constexpr int kDefaultType = 0;

// CS2 has 64 player slots.
inline constexpr int32_t kMaxSlots = 64;

} // namespace vips
