#include <error.hpp>
#include <rpcclient.hpp>

#include <iostream>

int main(int argc, char* argv[]) {
	if (argc != 2)
		return -1;
	auto io = kj::setupAsyncIo();
	auto& waitScope = io.waitScope;
	auto daemon = initClient(io);

	if (!daemon)
		return -2;

	// Get the user ID from the username via RPC to the daemon
	auto userreq = daemon->getUserByNameRequest();
	userreq.setName(argv[1]);
	auto userresp = userreq.send().wait(waitScope);
	if (static_cast<Error>(userresp.getErrcode()) != Error::Ok)
		return userresp.getErrcode();

	if (std::string("active") != userresp.getUser().getState().cStr())
		return -3;

	// Get the ssh public keys from user by ID via RPC to the daemon
	auto keyreq = daemon->getSSHKeysRequest();
	keyreq.setId(userresp.getUser().getId());
	auto keyresp = keyreq.send().wait(waitScope);
	if (static_cast<Error>(keyresp.getErrcode()) != Error::Ok)
		return keyresp.getErrcode();

	// Print keys and success
	std::cout << keyresp.getKeys().cStr() << std::endl;
	return 0;
}