/**
 * @file gitlabnssd.cpp
 * @brief The gitlabnss daemon executable
 */

#include <config.hpp>
#include <gitlabapi.hpp>

#include <stlcache/stlcache.hpp>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <capnp/ez-rpc.h>
#include <protocol/messages.capnp.h>

#include <grp.h>

#include <any>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static void initLogger() {
#if DEBUG
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#endif
	// auto filesink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("/var/log/gitlabnss.log");
	auto filesink =
			std::make_shared<spdlog::sinks::rotating_file_sink_mt>("/var/log/gitlabnss.log", 5 * 1024 * 1024, 3);
	std::vector<spdlog::sink_ptr> sinks{
#if DEBUG
			console_sink,
#endif
			filesink
	};
	auto logger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());
	logger->set_level(spdlog::level::trace);
	logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(logger);
}

// Aging set to 30*60 seconds (aka .5 h); that is, an entry that was used n times will be deleted after n/2 hours
template <typename K, typename V>
using Cache = stlcache::cache<K, V, stlcache::policy_lfuaging<60 * 60>>;

class GitLabDaemonImpl final : public GitLabDaemon::Server {
private:
	Config config;
	gitlab::GitLab gitlab;

	Cache<std::string, gitlab::User> usercache{100};   // Cache for the most recent 100 user calls.
	Cache<std::string, gitlab::Group> groupcache{100}; // Cache for the most recent 100 group calls.
	std::map<gitlab::GroupID, gid_t> groupMap;

	template <typename V>
	Cache<std::string, V>& getcache();

	template <typename T>
	bool findInCache(const std::string& cacheId, T& value) {
		auto& cache = getcache<T>();
		if (cache.check(cacheId)) {
			spdlog::info("Found in cache");
			if (const T* val = &cache.fetch(cacheId)) {
				value = *val;
				return true;
			} else {
				spdlog::error("Type error (this should not happen)");
			}
		} else {
			spdlog::info("Cachemiss");
		}
		return false;
	}

	std::map<gitlab::GroupID, gid_t> resolveGroupMap() {
		std::map<gitlab::GroupID, gid_t> ret;
		spdlog::info("Resolving Group Map");
		spdlog::info("\tgitlab (id) -> host (id)");
		gitlab::Group group;
		for (const auto& [gitlabgrp, hostgrp] : config.nss.groupMapping) {
			if (Error err; (err = gitlab.fetchGroupByName(gitlabgrp, group)) == Error::Ok) {
				if (::group* grp = getgrnam(hostgrp.c_str())) {
					spdlog::info("\t{} ({}) -> {} ({})", gitlabgrp, group.id, hostgrp, grp->gr_gid);
					ret[group.id] = grp->gr_gid;
				} else {
					spdlog::error(
							"\tFailed to resolve group {} on host with errno {}; I will ignore it", hostgrp, errno
					);
				}
			} else {
				spdlog::error(
						"\tFailed to resolve GitLab group {} with error {}; I will ignore it", gitlabgrp,
						static_cast<unsigned>(err)
				);
			}
		}
		return ret;
	}

	void populateUserDTO(User::Builder& dto, gitlab::User user) const;

public:
	GitLabDaemonImpl(Config config) : config(config), gitlab(this->config), groupMap(resolveGroupMap()) {}

	virtual ::kj::Promise<void> getUserByID(GetUserByIDContext context) override;
	virtual ::kj::Promise<void> getUserByName(GetUserByNameContext context) override;
	virtual ::kj::Promise<void> getSSHKeys(GetSSHKeysContext context) override;
	virtual ::kj::Promise<void> getGroupByID(GetGroupByIDContext context) override;
	virtual ::kj::Promise<void> getGroupByName(GetGroupByNameContext context) override;
};

template <>
constexpr Cache<std::string, gitlab::User>& GitLabDaemonImpl::getcache<gitlab::User>() {
	return usercache;
}
template <>
constexpr Cache<std::string, gitlab::Group>& GitLabDaemonImpl::getcache<gitlab::Group>() {
	return groupcache;
}

void GitLabDaemonImpl::populateUserDTO(User::Builder& dto, gitlab::User user) const {
	dto.setId(user.id);
	dto.setName(user.name);
	dto.setUsername(user.username);
	dto.setState(user.state);
	// Move primary group of the user to the front
	auto it = std::find_if(std::begin(user.groups), std::end(user.groups), [this](const auto& group) {
		return group.name == config.nss.primaryGroup;
	});
	if (it != std::end(user.groups))
		std::swap(*it, user.groups[0]);
	//
	auto groups = dto.initGroups(user.groups.size());
	for (auto i = 0; i < user.groups.size(); ++i) {
		if (decltype(groupMap)::const_iterator it; (it = groupMap.find(user.groups[i].id)) != groupMap.end()) {
			// Group mapped to host group
			groups[i].setId(it->second);
			groups[i].setName("");
			groups[i].setLocal(true);
		} else {
			// GitLab group
			groups[i].setId(user.groups[i].id);
			groups[i].setName(user.groups[i].name);
			groups[i].setLocal(false);
		}
	}
}

::kj::Promise<void> GitLabDaemonImpl::getUserByID(GetUserByIDContext context) {
	auto& cache = getcache<gitlab::User>();
	spdlog::info("getUserByID({})", context.getParams().getId());
	auto cacheId = std::format("getUserByID({})", context.getParams().getId());
	gitlab::User user;
	Error err = Error::Ok;
	if (findInCache(cacheId, user) || ((err = gitlab.fetchUserByID(context.getParams().getId(), user)) == Error::Ok &&
									   (err = gitlab.fetchGroups(user)) == Error::Ok)) {
		spdlog::debug("Found");
		cache.insert_or_assign(cacheId, user);
		cache.insert_or_assign(std::format("getUserByName({})", user.name), user);
		auto output = context.getResults().initUser();
		populateUserDTO(output, user);
	}
	context.getResults().setErrcode(static_cast<uint32_t>(err));
	return kj::READY_NOW;
}
::kj::Promise<void> GitLabDaemonImpl::getUserByName(GetUserByNameContext context) {
	auto& cache = getcache<gitlab::User>();
	spdlog::info("getUserByName({})", context.getParams().getName().cStr());
	auto cacheId = std::format("getUserByName({})", context.getParams().getName().cStr());
	gitlab::User user;
	Error err = Error::Ok;
	if (findInCache(cacheId, user) ||
		((err = gitlab.fetchUserByUsername(context.getParams().getName().cStr(), user)) == Error::Ok &&
		 (err = gitlab.fetchGroups(user)) == Error::Ok)) {
		spdlog::debug("Found");
		cache.insert_or_assign(cacheId, user);
		cache.insert_or_assign(std::format("getUserByID({})", user.id), user);
		auto output = context.getResults().initUser();
		populateUserDTO(output, user);
	}
	context.getResults().setErrcode(static_cast<uint32_t>(err));
	return kj::READY_NOW;
}

::kj::Promise<void> GitLabDaemonImpl::getSSHKeys(GetSSHKeysContext context) {
	spdlog::info("getSSHKeys({})", context.getParams().getId());
	std::vector<std::string> keys;
	Error err;
	if ((err = gitlab.fetchAuthorizedKeys(context.getParams().getId(), keys)) == Error::Ok) {
		spdlog::debug("Found");
		// When std::ranges::to is finally implemented by GCC:
		// std::string joined = keys | std::views::join | std::ranges::to<std::string>();
		std::string joined;
		for (auto&& key : keys)
			joined += key + "\n";

		context.getResults().setKeys(joined);
	}
	context.getResults().setErrcode(static_cast<uint32_t>(err));
	return kj::READY_NOW;
}

::kj::Promise<void> GitLabDaemonImpl::getGroupByID(GetGroupByIDContext context) {
	auto& cache = getcache<gitlab::Group>();
	spdlog::info("getGroupByID({})", context.getParams().getId());
	auto cacheId = std::format("getGroupByID({})", context.getParams().getId());
	gitlab::Group group;
	Error err = Error::Ok;
	if (findInCache(cacheId, group) || (err = gitlab.fetchGroupByID(context.getParams().getId(), group)) == Error::Ok) {
		spdlog::debug("Found");
		cache.insert_or_assign(std::format("getGroupByName({})", group.name), group);
		cache.insert_or_assign(cacheId, group);
		auto output = context.getResults().initGroup();
		output.setId(group.id);
		output.setName(group.name);
	}
	context.getResults().setErrcode(static_cast<uint32_t>(err));
	return kj::READY_NOW;
}
::kj::Promise<void> GitLabDaemonImpl::getGroupByName(GetGroupByNameContext context) {
	auto& cache = getcache<gitlab::Group>();
	spdlog::info("getGroupByName({})", context.getParams().getName().cStr());
	auto cacheId = std::format("getGroupByName({})", context.getParams().getName().cStr());
	gitlab::Group group;
	Error err = Error::Ok;
	if (findInCache(cacheId, group) ||
		(err = gitlab.fetchGroupByName(context.getParams().getName().cStr(), group)) == Error::Ok) {
		spdlog::debug("Found");
		cache.insert_or_assign(cacheId, group);
		cache.insert_or_assign(std::format("getGroupByID({})", group.id), group);
		auto output = context.getResults().initGroup();
		output.setId(group.id);
		output.setName(group.name);
	}
	context.getResults().setErrcode(static_cast<uint32_t>(err));
	return kj::READY_NOW;
}

static auto [promise, fulfiller] = kj::newPromiseAndFulfiller<void>();
int main(int argc, char* argv[]) {
	bool daemonize = true;
	if (argc == 2 && argv[1] == std::string_view{"--foreground"}) {
		daemonize = false;
	} else if (argc != 1) {
		return -1; // Invalid CLI Args
	}
	if (daemonize)
		daemon(0, 0);
	{
		std::ofstream fstream((std::filesystem::absolute("run") / "gitlabnssd.pid").c_str());
		fstream << getpid() << std::endl;
	}

	// Init
	auto configPath = fs::current_path().root_path() / "etc" / "gitlabnss" / "gitlabnss.conf";
	initLogger();
	spdlog::info("Starting the GitLab NSS daemon...");
	spdlog::info("Reading config from {}", configPath.string());
	auto config = Config::fromFile(configPath);
	auto socketPath = config.general.socketPath;
	spdlog::info("Success! Will use {} to communicate with GitLab", config.gitlabapi.baseUrl);
	spdlog::info("Binding socket to {}", socketPath.string());
	capnp::Capability::Client heap{kj::heap<GitLabDaemonImpl>(config)};
	auto addr = std::format("unix:{}", socketPath.string());
	kj::StringPtr bind = addr.c_str();
	capnp::EzRpcServer server{heap, bind};
	auto& waitScope = server.getWaitScope();

	waitScope.poll(); /* Poll once to create the socket file. */
	spdlog::info("Setting socket permissions for {} to 0o{:o}", socketPath.c_str(), config.general.socketPerms);
	if (chmod(socketPath.c_str(), static_cast<mode_t>(config.general.socketPerms)) != 0)
		spdlog::warn("Failed to change permissions with errno {}", errno);

	spdlog::info("Instantiating SIGINT and SIGTERM handlers");
	std::signal(SIGINT, +[](int signal) { fulfiller->fulfill(); });
	std::signal(SIGTERM, +[](int signal) { fulfiller->fulfill(); });

	// Run until SIGINT is signaled; accept connections and handle requests.
	spdlog::info("Listening...");
	promise.wait(waitScope);

	// A shame that EzRpcServer does not clean up after itself :(
	unlink(socketPath.string().c_str());
	spdlog::info("Good bye!");
	return 0;
}
