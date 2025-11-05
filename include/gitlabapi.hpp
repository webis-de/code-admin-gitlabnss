#ifndef GITLABAPI_HPP
#define GITLABAPI_HPP

#include "config.hpp"
#include "error.hpp"

#include <string>
#include <vector>

namespace gitlab {
	using UserID = unsigned;
	using GroupID = unsigned;

	struct Group {
		GroupID id;
		std::string name;
	};

	struct User {
		UserID id;
		std::string username;
		std::string name;

		std::vector<Group> groups;
	};

	class GitLab final {
	private:
		const Config& config;

	public:
		explicit GitLab(const Config& config) noexcept;

		Error fetchUserByUsername(std::string username, User& user) const;
		Error fetchUserByID(UserID id, User& user) const;

		Error fetchAuthorizedKeys(UserID id, std::vector<std::string>& keys) const;
		Error fetchGroups(User& user) const;

		Error fetchGroupByName(const std::string& groupname, Group& group) const;
		Error fetchGroupByID(GroupID id, Group& group) const;
	};
} // namespace gitlab

#endif