#include <gitlabapi.hpp>

#include <cpr/cpr.h>
#include <rapidjson/document.h>

#include <expected>
#include <format>

using gitlab::GitLab;
using gitlab::Group;
using gitlab::GroupID;
using gitlab::User;
using gitlab::UserID;

static std::expected<rapidjson::Document, Error> fetch(const Config& config, std::string url) noexcept {
	auto resp = cpr::Get(cpr::Url{url}, cpr::Bearer{config.gitlabapi.apikey});
	if (resp.status_code == 404)
		return std::unexpected(Error::NotFound);
	else if (resp.status_code == 401)
		return std::unexpected(Error::AuthenticationError);
	else if (resp.status_code >= 400)
		return std::unexpected(Error::GenericError);
	else if (resp.status_code >= 500)
		return std::unexpected(Error::ServerError);
	rapidjson::Document json;
	json.Parse(resp.text.c_str());
	if (json.HasParseError())
		return std::unexpected(Error::ResponseFormatError);
	return json;
}

GitLab::GitLab(const Config& config) noexcept : config(config) {}

Error GitLab::fetchUserByUsername(std::string username, User& user) const {
	/**  \todo should not hurt to apply url-encoding of the username **/
	auto fetched = fetch(config, std::format("{}/users?username={}", config.gitlabapi.baseUrl, username));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsArray() || json.Size() != 1)
		return Error::ResponseFormatError;
	auto& userJson = json[0];
	user.id = userJson["id"].Get<decltype(user.id)>();
	user.username = userJson["username"].GetString();
	user.name = userJson["name"].GetString();
	return Error::Ok;
}

Error GitLab::fetchUserByID(UserID id, User& user) const {
	auto fetched = fetch(config, std::format("{}/users/{}", config.gitlabapi.baseUrl, id));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsObject())
		return Error::ResponseFormatError;
	auto& userJson = json;
	user.id = userJson["id"].Get<decltype(user.id)>();
	user.username = userJson["username"].GetString();
	user.name = userJson["name"].GetString();
	return Error::Ok;
}

Error GitLab::fetchAuthorizedKeys(UserID id, std::vector<std::string>& keys) const {
	auto fetched = fetch(config, std::format("{}/users/{}/keys", config.gitlabapi.baseUrl, id));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsArray())
		return Error::ResponseFormatError;
	for (const auto& key : json.GetArray())
		/** \todo adhere to expires_at **/
		if (key["usage_type"] == "auth_and_signing")
			keys.emplace_back(key["key"].GetString());
	return Error::Ok;
}

Error GitLab::fetchGroups(User& user) const {
	auto fetched = fetch(config, std::format("{}/users/{}/memberships", config.gitlabapi.baseUrl, user.id));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsArray())
		return Error::ResponseFormatError;
	user.groups.clear();
	for (const auto& group : json.GetArray())
		user.groups.emplace_back(
				Group{.id = group["source_id"].Get<decltype(user.id)>(), .name = group["source_name"].GetString()}
		);
	return Error::Ok;
}

Error GitLab::fetchGroupByName(const std::string& groupname, Group& group) const {
	/**  \todo should not hurt to apply url-encoding of the groupname **/
	auto fetched = fetch(config, std::format("{}/groups?search={}", config.gitlabapi.baseUrl, groupname));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsArray())
		return Error::ResponseFormatError;
	for (size_t i = 0; i < json.Size(); ++i) {
		const auto& groupJson = json[i];
		if (groupname == groupJson["name"].GetString()) {
			group.id = groupJson["id"].Get<decltype(group.id)>();
			group.name = groupJson["name"].GetString();
			return Error::Ok;
		}
	}
	return Error::NotFound;
}

Error GitLab::fetchGroupByID(GroupID id, Group& group) const {
	auto fetched = fetch(config, std::format("{}/groups/{}", config.gitlabapi.baseUrl, id));
	if (!fetched.has_value())
		return fetched.error();
	auto& json = fetched.value();
	if (!json.IsObject())
		return Error::ResponseFormatError;
	auto& groupJson = json;
	group.id = groupJson["id"].Get<decltype(group.id)>();
	group.name = groupJson["name"].GetString();
	return Error::Ok;
}