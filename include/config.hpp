#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <filesystem>
#include <map>
#include <string>

struct Config {
	// general settings
	static constexpr const char DefaultSocketPath[] = "/var/run/gitlabnss.sock";
	static constexpr uint16_t DefaultSocketPerms = 0666u;
	static constexpr const char DefaultSocketOwner[] = "root:root";
	// gitlabapi settings
	// (no defaults)
	// nss settings
	static constexpr uint16_t DefaultHomePerms = 0700u;
	static constexpr unsigned DefaultUIDOffset = 0;
	static constexpr unsigned DefaultGIDOffset = 0;
	static constexpr const char DefaultShell[] = "/usr/bin/bash";
	static constexpr const char DefaultGroupPrefix[] = "";

	struct {
		std::filesystem::path socketPath;
		uint16_t socketPerms;
		std::string socketOwner;
	} general;
	struct {
		std::string baseUrl;
		std::string apikey;
	} gitlabapi;
	struct NSS {
		std::filesystem::path homesRoot;
		uint16_t homePerms;
		unsigned uidOffset;
		unsigned gidOffset;
		std::string groupPrefix;
		std::string shell;
		std::map<std::string, std::string> groupMapping;
	} nss;

	static Config fromFile(const std::filesystem::path& file) noexcept;
};

#endif