#include <config.hpp>

#include <toml++/toml.hpp>

#include <iostream>
#include <string>

using namespace std::string_literals;

static std::optional<std::string> tryReadSecret(const std::filesystem::path& path) {
	std::ifstream file(path);
	if (std::string line; std::getline(file, line))
		return line;
	return std::nullopt;
}

std::string Config::NSS::resolveGroupName(const std::string& name) const {
	auto it = groupMapping.find(name);
	if (it != groupMapping.end())
		return it->second;
	return groupPrefix + name;
}

Config Config::fromFile(const std::filesystem::path& file) noexcept {
	auto config = toml::parse_file(file.string());
	if (!config) {
		std::cout << "Not found or invalid TOML: " << config.error() << std::endl;
		// No config found
		return Config{}; /** \todo do something sensible **/
	} else {
		auto table = config.table();
		return Config{
				.general =
						{.socketPath = std::filesystem::path{table["general"]["socket_path"].value_or(
								 Config::DefaultSocketPath
						 )},
						 .socketPerms = table["general"]["socket_permissions"].value_or(Config::DefaultSocketPerms),
						 .socketOwner = table["general"]["socket_owner"].value_or(Config::DefaultSocketOwner)},
				.gitlabapi =
						{.baseUrl = table["gitlabapi"]["base_url"].value_or(""s),
						 .apikey = table["gitlabapi"]["secret"]
										   .value<std::string>()
										   .transform([file](const std::filesystem::path& path) {
											   return file.parent_path() / path;
										   })
										   .and_then(tryReadSecret)
										   .value_or(""s)},
				.nss = {.homesRoot = std::filesystem::path{table["nss"]["homes_root"].value_or("/homes/"s)},
						.homePerms = table["nss"]["homes_permissions"].value_or(Config::DefaultHomePerms),
						.uidOffset = table["nss"]["uid_offset"].value_or(Config::DefaultUIDOffset),
						.gidOffset = table["nss"]["gid_offset"].value_or(Config::DefaultGIDOffset),
						.groupPrefix = table["nss"]["group_prefix"].value_or(""),
						.shell = table["nss"]["shell"].value_or(Config::DefaultShell)}
		};
	}
}