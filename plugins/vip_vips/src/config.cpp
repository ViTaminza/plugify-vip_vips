#include "config.hpp"

#include "common.hpp"
#include "kv1.hpp"
#include "log.hpp"
#include "text.hpp"

#include <plg/plugin.hpp>

#include <optional>
#include <string>

namespace vips {

std::string ConfigPath(std::string_view relative) {
	if (!plg::GetConfigsDir) {
		return {};
	}
	std::string path(plg::GetConfigsDir());
	if (path.empty()) {
		return {};
	}
	if (path.back() != '/') {
		path.push_back('/');
	}
	path.append(relative);
	return path;
}

int LoadVipVipsType() {
	const std::string path = ConfigPath(kConfigFile);
	if (path.empty()) {
		LogError(Format("[Error] [%s] The Plugify configs directory is unavailable; using type %i", kLogTag,
						kDefaultType));
		return kDefaultType;
	}
	const std::optional<KvNode> config = LoadKv1File(path);
	if (!config) {
		LogError(Format("[Error] [%s] Failed to load %s (using type %i)", kLogTag, path.c_str(), kDefaultType));
		return kDefaultType;
	}
	return config->GetInt("type", kDefaultType);
}

} // namespace vips
