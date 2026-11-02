#include "log.hpp"

#include <plg/plugin.hpp>

namespace vips {

void LogError(std::string_view message) {
	if (plg::Log) {
		plg::Log(message, plg::Severity::Error, plg::source_location::current());
	}
}

} // namespace vips
