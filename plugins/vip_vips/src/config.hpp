// vip_vips — where the config lives and how it is read.
//
// The port keeps the original file: the Plugify configs tree holds vip/vip_vips.ini (the Metamod
// server had addons/configs/vip/vip_vips.ini; pisexcompat mapped the same directory).
//
// The original loaded the file in Load() and refused to start without it. The port fails soft
// (PITFALLS.md A1): a missing or malformed file logs one error line and the plugin keeps the
// default type 0, exactly like the pre-rewrite port.

#pragma once

#include <string>
#include <string_view>

namespace vips {

// <Plugify configs dir>/<relative>; "" when the host does not provide the configs dir (tests).
std::string ConfigPath(std::string_view relative);

// Reads "type" from vip_vips.ini. Missing file, malformed file or missing key: kDefaultType with
// one error line in the log.
int LoadVipVipsType();

} // namespace vips
