/**
 * @file gitlabnssd.cpp
 * @brief The gitlabnss daemon executable
 */

#include <config.hpp>
#include <gitlabapi.hpp>

#include <lrucache.hpp>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <capnp/ez-rpc.h>
#include <protocol/messages.capnp.h>

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
	auto basic_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("/var/log/gitlabnss.log");
	std::vector<spdlog::sink_ptr> sinks{
#if DEBUG
			console_sink,
#endif
			basic_sink
	};
	auto logger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());
	logger->set_level(spdlog::level::trace);
	logger->flush_on(spdlog::level::info);
	spdlog::set_default_logger(logger);
}

class GitLabDaemonImpl final : public GitLabDaemon::Server {
private:
	Config config;
	gitlab::GitLab gitlab;
	cache::lru_cache<std::string, std::any> cache{20}; // Cache for the most recent 20 calls.

	template <typename T>
	bool findInCache(const std::string& cacheId, T& value) {
		if (cache.exists(cacheId)) {
			spdlog::info("Found in cache");
			if (const T* val = std::any_cast<T>(&cache.get(cacheId))) {
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

public:
	GitLabDaemonImpl(Config config) : config(config), gitlab(this->config) {}

	virtual ::kj::Promise<void> getUserByID(GetUserByIDContext context) override {
		spdlog::info("getUserByID({})", context.getParams().getId());
		auto cacheId = std::format("getUserByID({})", context.getParams().getId());
		gitlab::User user;
		Error err = Error::Ok;
		if (findInCache(cacheId, user) ||
			((err = gitlab.fetchUserByID(context.getParams().getId(), user)) == Error::Ok &&
			 (err = gitlab.fetchGroups(user)) == Error::Ok)) {
			spdlog::debug("Found");
			cache.put(cacheId, user);
			auto output = context.getResults().initUser();
			output.setId(user.id);
			output.setName(user.name);
			output.setUsername(user.username);
			auto groups = output.initGroups(user.groups.size());
			for (auto i = 0; i < user.groups.size(); ++i) {
				groups[i].setId(user.groups[i].id);
				groups[i].setName(user.groups[i].name);
			}
		}
		context.getResults().setErrcode(static_cast<uint32_t>(err));
		return kj::READY_NOW;
	}
	virtual ::kj::Promise<void> getUserByName(GetUserByNameContext context) override {
		spdlog::info("getUserByName({})", context.getParams().getName().cStr());
		auto cacheId = std::format("getUserByName({})", context.getParams().getName().cStr());
		gitlab::User user;
		Error err = Error::Ok;
		if (findInCache(cacheId, user) ||
			((err = gitlab.fetchUserByUsername(context.getParams().getName().cStr(), user)) == Error::Ok &&
			 (err = gitlab.fetchGroups(user)) == Error::Ok)) {
			spdlog::debug("Found");
			cache.put(cacheId, user);
			auto output = context.getResults().initUser();
			output.setId(user.id);
			output.setName(user.name);
			output.setUsername(user.username);
			auto groups = output.initGroups(user.groups.size());
			for (auto i = 0; i < user.groups.size(); ++i) {
				groups[i].setId(user.groups[i].id);
				groups[i].setName(user.groups[i].name);
			}
		}
		context.getResults().setErrcode(static_cast<uint32_t>(err));
		return kj::READY_NOW;
	}

	virtual ::kj::Promise<void> getSSHKeys(GetSSHKeysContext context) {
		spdlog::info("getSSHKeys({})", context.getParams().getId());
		std::vector<std::string> keys;
		Error err;
		if ((err = gitlab.fetchAuthorizedKeys(context.getParams().getId(), keys)) == Error::Ok) {
			spdlog::debug("Found");
			// When std::ranges::to is finally implemented by GCC:
			// std::string joined = keys | std::views::join | std::ranges::to<std::string>();
			std::string joined;
			for (auto&& key : keys)
				joined += key;

			context.getResults().setKeys(joined);
		}
		context.getResults().setErrcode(static_cast<uint32_t>(err));
		return kj::READY_NOW;
	}

	virtual ::kj::Promise<void> getGroupByID(GetGroupByIDContext context) override {
		spdlog::info("getGroupByID({})", context.getParams().getId());
		auto cacheId = std::format("getGroupByID({})", context.getParams().getId());
		gitlab::Group group;
		Error err = Error::Ok;
		if (findInCache(cacheId, group) ||
			(err = gitlab.fetchGroupByID(context.getParams().getId(), group)) == Error::Ok) {
			spdlog::debug("Found");
			cache.put(cacheId, group);
			auto output = context.getResults().initGroup();
			output.setId(group.id);
			output.setName(group.name);
		}
		context.getResults().setErrcode(static_cast<uint32_t>(err));
		return kj::READY_NOW;
	}
	virtual ::kj::Promise<void> getGroupByName(GetGroupByNameContext context) override {
		spdlog::info("getGroupByName({})", context.getParams().getName().cStr());
		auto cacheId = std::format("getGroupByName({})", context.getParams().getName().cStr());
		gitlab::Group group;
		Error err = Error::Ok;
		if (findInCache(cacheId, group) ||
			(err = gitlab.fetchGroupByName(context.getParams().getName().cStr(), group)) == Error::Ok) {
			spdlog::debug("Found");
			cache.put(cacheId, group);
			auto output = context.getResults().initGroup();
			output.setId(group.id);
			output.setName(group.name);
		}
		context.getResults().setErrcode(static_cast<uint32_t>(err));
		return kj::READY_NOW;
	}
};

static auto [promise, fulfiller] = kj::newPromiseAndFulfiller<void>();
int main(int argc, char* argv[]) {
	// Daemonize
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

	spdlog::info("Instantiating SIGINT handler");
	std::signal(SIGINT, +[](int signal) { fulfiller->fulfill(); });

	// Run until SIGINT is signaled; accept connections and handle requests.
	spdlog::info("Listening...");
	promise.wait(waitScope);

	// A shame that EzRpcServer does not clean up after itself :(
	unlink(socketPath.string().c_str());
	spdlog::info("Good bye!");
	return 0;
}