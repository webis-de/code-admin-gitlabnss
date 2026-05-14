#ifndef RPCCLIENT_HPP
#define RPCCLIENT_HPP

#include "compat/format.hpp"

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <protocol/messages.capnp.h>

#include <filesystem>
#include <memory>

static std::shared_ptr<GitLabDaemon::Client> initClient(kj::AsyncIoContext& io) {
	try {
		auto& waitScope = io.waitScope;
		auto socketPath = std::filesystem::current_path().root_path() / "var" / "run" / "gitlabnss.sock";
		auto addrstr = std20::format("unix:{}", socketPath.string());
		kj::Network& network = io.provider->getNetwork();
		kj::Own<kj::NetworkAddress> addr = network.parseAddress(kj::StringPtr{addrstr.c_str()}).wait(waitScope);
		struct Anon {
			kj::Own<kj::AsyncIoStream> conn;
			capnp::TwoPartyClient client;
			GitLabDaemon::Client daemon = client.bootstrap().castAs<GitLabDaemon>();
			explicit Anon(kj::Own<kj::AsyncIoStream>&& conn)
					: conn(std::move(conn)), client(*(this->conn)), daemon(client.bootstrap().castAs<GitLabDaemon>()) {}
		};
		auto anon = std::make_shared<Anon>(std::move(addr->connect().wait(waitScope)));
		return std::shared_ptr<GitLabDaemon::Client>{anon, &anon->daemon};
	} catch (std::exception& e) {
		return nullptr;
	}
}

#endif