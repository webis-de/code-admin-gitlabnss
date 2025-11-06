#include <config.hpp>
#include <error.hpp>
#include <rpcclient.hpp>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <grp.h>
#include <nss.h>
#include <pwd.h>
#include <shadow.h>
#include <sys/stat.h>

#include <filesystem>
#include <span>
#include <spanstream>

namespace fs = std::filesystem;

static auto initLogger() {

#if DEBUG
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#endif
	// auto filesink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("/var/log/gitlabnss-client.log");
	// auto filesink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("/var/log/gitlabnss-client.log", 5 * 1024 * 1024, 3);
	std::vector<spdlog::sink_ptr> sinks{
#if DEBUG
			console_sink,
#endif
			// filesink
	};
	auto logger = std::make_shared<spdlog::logger>("", sinks.begin(), sinks.end());
	logger->set_level(spdlog::level::trace);
	logger->flush_on(spdlog::level::info);
	SPDLOG_LOGGER_DEBUG(logger, "Logger created");
	return logger;
}

static auto logger = initLogger();
static auto config = Config::fromFile(fs::current_path().root_path() / "etc" / "gitlabnss" / "gitlabnss.conf");

static auto getGroupId(const Group::Reader& group) {
	if (group.getLocal())
		return group.getId();
	return group.getId() + config.nss.gidOffset;
}

void populatePasswd(passwd& pwd, const User::Reader& user, std::span<char> buffer) {
	auto stream = std::ospanstream(buffer);
	// Username
	pwd.pw_name = buffer.data() + stream.tellp();
	stream << user.getUsername().cStr() << '\0';
	// Password
	const char Password[] = "*"; // user can't login with PW: https://www.man7.org/linux/man-pages/man5/shadow.5.html
	pwd.pw_passwd = buffer.data() + stream.tellp();
	stream << Password << '\0';
	// UID
	pwd.pw_uid = user.getId() + config.nss.uidOffset;
	// GID
	pwd.pw_gid = user.getGroups().size() > 0 ? getGroupId(user.getGroups()[0]) : 65534 /*nogroup*/;
	// Real Name
	pwd.pw_gecos = buffer.data() + stream.tellp();
	stream << user.getName().cStr() << '\0';
	// Shell
	pwd.pw_shell = buffer.data() + stream.tellp();
	stream << config.nss.shell << '\0';
	// Home directory
	pwd.pw_dir = buffer.data() + stream.tellp();
	std::string homedir = config.nss.homesRoot / user.getUsername().cStr();
	fs::create_directories(config.nss.homesRoot / user.getUsername().cStr());
	chown(homedir.c_str(), pwd.pw_uid, pwd.pw_gid);
	chmod(homedir.c_str(), config.nss.homePerms);
	stream << homedir << '\0';
}

extern "C" {
nss_status _nss_gitlab_getpwuid_r(uid_t uid, passwd* pwd, char* buf, size_t buflen, int* errnop) {
	SPDLOG_LOGGER_DEBUG(logger, "getpwuid_r({})", uid);
	if (uid < config.nss.uidOffset)
		return nss_status::NSS_STATUS_NOTFOUND;
	SPDLOG_LOGGER_DEBUG(logger, "Fetching User {}", uid - config.nss.uidOffset);
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return NSS_STATUS_UNAVAIL;

	auto request = daemon->getUserByIDRequest();
	request.setId(uid - config.nss.uidOffset);
	auto promise = request.send().wait(waitScope);

	auto user = promise.getUser();
	auto err = static_cast<Error>(promise.getErrcode());
	if (err == Error::Ok && std::string("active") == user.getState().cStr()) {
		populatePasswd(*pwd, user, {buf, buflen});
		SPDLOG_LOGGER_DEBUG(logger, "Found!");
		return nss_status::NSS_STATUS_SUCCESS;
	} else if (err == Error::Ok) {
		SPDLOG_LOGGER_DEBUG(logger, "User is not active (status: {})", user.getState().cStr());
		return nss_status::NSS_STATUS_NOTFOUND;
	} else if (err == Error::NotFound) {
		SPDLOG_LOGGER_DEBUG(logger, "Not Found");
		return nss_status::NSS_STATUS_NOTFOUND;
	} else {
		SPDLOG_LOGGER_ERROR(logger, "Other Error");
		SPDLOG_LOGGER_ERROR(logger, "Error {}", promise.getErrcode());
		return nss_status::NSS_STATUS_UNAVAIL;
	}
}

nss_status _nss_gitlab_getpwnam_r(const char* name, passwd* pwd, char* buf, size_t buflen, int* errnop) {
	SPDLOG_LOGGER_DEBUG(logger, "getpwnam_r({})", name);
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return NSS_STATUS_UNAVAIL;

	auto request = daemon->getUserByNameRequest();
	request.setName(name);
	auto promise = request.send().wait(waitScope);

	auto user = promise.getUser();
	auto err = static_cast<Error>(promise.getErrcode());
	if (err == Error::Ok && std::string("active") == user.getState().cStr()) {
		populatePasswd(*pwd, user, {buf, buflen});
		SPDLOG_LOGGER_DEBUG(logger, "Found!");
		return nss_status::NSS_STATUS_SUCCESS;
	} else if (err == Error::Ok) {
		SPDLOG_LOGGER_DEBUG(logger, "User is not active (status: {})", user.getState().cStr());
		return nss_status::NSS_STATUS_NOTFOUND;
	} else if (err == Error::NotFound) {
		SPDLOG_LOGGER_DEBUG(logger, "Not Found");
		return nss_status::NSS_STATUS_NOTFOUND;
	} else {
		SPDLOG_LOGGER_ERROR(logger, "Other Error");
		SPDLOG_LOGGER_ERROR(logger, "Error {}", promise.getErrcode());
		return nss_status::NSS_STATUS_UNAVAIL;
	}
}

/**********************************************************************************************************************/
/* GROUPS                                                                                                             */
/**********************************************************************************************************************/
void populateGroup(group& group, const Group::Reader& obj, std::span<char> buffer) {
	auto stream = std::ospanstream(buffer);
	// Username
	group.gr_name = buffer.data() + stream.tellp();
	stream << (obj.getLocal() ? "" : config.nss.groupPrefix) << obj.getName().cStr() << '\0';
	// Password
	const char Password[] = "*"; // user can't login with PW: https://www.man7.org/linux/man-pages/man5/shadow.5.html
	group.gr_passwd = buffer.data() + stream.tellp();
	stream << Password << '\0';
	// GID
	group.gr_gid = getGroupId(obj);
	// Members
	group.gr_mem = nullptr;
}

nss_status _nss_gitlab_getgrgid_r(gid_t gid, group* result_buf, char* buf, size_t buflen, group** result) {
	SPDLOG_LOGGER_DEBUG(logger, "getgrgid_r({})", gid);
	if (gid < config.nss.gidOffset)
		return nss_status::NSS_STATUS_NOTFOUND;
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return NSS_STATUS_UNAVAIL;

	auto request = daemon->getGroupByIDRequest();
	request.setId(gid - config.nss.gidOffset);
	auto promise = request.send().wait(waitScope);

	auto user = promise.getGroup();
	switch (static_cast<Error>(promise.getErrcode())) {
	case Error::Ok:
		populateGroup(*result_buf, user, {buf, buflen});
		SPDLOG_LOGGER_DEBUG(logger, "Found!");
		*result = result_buf;
		return nss_status::NSS_STATUS_SUCCESS;
	case Error::NotFound:
		SPDLOG_LOGGER_DEBUG(logger, "Not Found");
		*result = nullptr;
		return nss_status::NSS_STATUS_NOTFOUND;
	default:
		SPDLOG_LOGGER_ERROR(logger, "Other Error");
		SPDLOG_LOGGER_ERROR(logger, "Error {}", promise.getErrcode());
		*result = nullptr;
		return nss_status::NSS_STATUS_UNAVAIL;
	}
}

nss_status _nss_gitlab_getgrnam_r(const char* name, group* result_buf, char* buf, size_t buflen, group** result) {
	SPDLOG_LOGGER_DEBUG(logger, "getgrnam_r({})", name);
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return NSS_STATUS_UNAVAIL;

	auto request = daemon->getGroupByNameRequest();
	request.setName(name);
	auto promise = request.send().wait(waitScope);

	auto user = promise.getGroup();
	switch (static_cast<Error>(promise.getErrcode())) {
	case Error::Ok:
		populateGroup(*result_buf, user, {buf, buflen});
		SPDLOG_LOGGER_DEBUG(logger, "Found!");
		*result = result_buf;
		return nss_status::NSS_STATUS_SUCCESS;
	case Error::NotFound:
		SPDLOG_LOGGER_DEBUG(logger, "Not Found");
		*result = nullptr;
		return nss_status::NSS_STATUS_NOTFOUND;
	default:
		SPDLOG_LOGGER_ERROR(logger, "Other Error");
		SPDLOG_LOGGER_ERROR(logger, "Error {}", promise.getErrcode());
		*result = nullptr;
		return nss_status::NSS_STATUS_UNAVAIL;
	}
}

/**********************************************************************************************************************/
/* GROUPS                                                                                                             */
/**********************************************************************************************************************/
nss_status _nss_gitlab_initgroups_dyn(
		const char* username, gid_t group, long int* start, long int* size, gid_t** groups, long int limit, int* errnop
) {
	// Its not well documented how this should behave but we can have a look at sssd for reference:
	// https://github.com/SSSD/sssd/blob/0c0afb24706ec343563833ea0c654b298dcdcf59/src/sss_client/nss_group.c#L375-L404
	SPDLOG_LOGGER_DEBUG(logger, "initgroups_dyn({}, {}, {}, {}, {})", username, group, (intptr_t)start, *size, limit);
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return NSS_STATUS_UNAVAIL;

	auto request = daemon->getUserByNameRequest();
	request.setName(username);
	auto promise = request.send().wait(waitScope);

	auto user = promise.getUser();
	auto err = static_cast<Error>(promise.getErrcode());
	if (err == Error::Ok && std::string("active") == user.getState().cStr()) {
		if (limit < 0 || limit > user.getGroups().size())
			limit = user.getGroups().size();
		// Check if groups is large enough, otherwise extend it
		if (*start + limit > *size) {
			*groups = static_cast<gid_t*>(std::realloc(*groups, (*start + limit) * sizeof(gid_t)));
			if (*groups == nullptr) {
				*errnop = ENOMEM;
				return NSS_STATUS_TRYAGAIN;
			}
			*size = *start + limit;
		}
		// Populate groups
		for (size_t i = 0; i < limit; ++i) {
			(*groups)[*start] = getGroupId(user.getGroups()[i]);
			*start += 1;
		}
		SPDLOG_LOGGER_DEBUG(logger, "Found!");
		return nss_status::NSS_STATUS_SUCCESS;
	} else if (err == Error::Ok) {
		SPDLOG_LOGGER_DEBUG(logger, "User is not active (status: {})", user.getState().cStr());
		return nss_status::NSS_STATUS_NOTFOUND;
	} else if (err == Error::NotFound) {
		SPDLOG_LOGGER_DEBUG(logger, "Not Found");
		return nss_status::NSS_STATUS_NOTFOUND;
	} else {
		SPDLOG_LOGGER_ERROR(logger, "Other Error");
		SPDLOG_LOGGER_ERROR(logger, "Error {}", promise.getErrcode());
		return nss_status::NSS_STATUS_UNAVAIL;
	}
}
}