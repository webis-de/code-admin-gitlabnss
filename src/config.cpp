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

static std::map<std::string, std::string> tomap(const toml::table* table) {
	if (table == nullptr) { /** \todo notify user of error? **/
		return {};
	}
	std::map<std::string, std::string> ret;
	for (const auto& [key, value] : *table)
		value.value<std::string>().and_then([&](const auto& val) {
			ret[std::string{key.str()}] = val;
			return std::optional{val};
		});
	return ret;
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
						.createHomedirs = table["nss"]["create_homedirs"].value_or(false),
						.homePerms = table["nss"]["homes_permissions"].value_or(Config::DefaultHomePerms),
						.uidOffset = table["nss"]["uid_offset"].value_or(Config::DefaultUIDOffset),
						.gidOffset = table["nss"]["gid_offset"].value_or(Config::DefaultGIDOffset),
						.groupPrefix = table["nss"]["group_prefix"].value_or(Config::DefaultGroupPrefix),
						.shell = table["nss"]["shell"].value_or(Config::DefaultShell),
						.primaryGroup = table["nss"]["primary_group"].value<std::string>(),
						.userCachesize = table["nss"]["user_cachesize"].value_or(Config::DefaultUserCachesize),
						.groupCachesize = table["nss"]["group_cachesize"].value_or(Config::DefaultGroupCachesize),
						.groupMapping = tomap(table["nss"]["group_mapping"].as_table())}
		};
	}
}