#define OLC_PGE3_APPLICATION
#include "Game.h"
#include "Items.h"
#include "Network.h"
#include "Server.h"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace {
	bool ParsePort(const std::string& text, uint16_t& port) {
		unsigned value = 0;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
		if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 || value > 65535)
			return false;

		port = static_cast<uint16_t>(value);
		return true;
	}
}

int main(int argc, char** argv) {
	namespace fs = std::filesystem;
	std::vector<fs::path> itemCandidates;
	std::error_code pathError;
	const fs::path workingDirectory = fs::current_path(pathError);
	if (!pathError)
		itemCandidates.push_back(workingDirectory / "data" / "items.json");
	if (!pathError)
		itemCandidates.push_back(workingDirectory / "Trauma" / "data" / "items.json");
	if (argc > 0 && argv[0] && argv[0][0] != '\0') {
		const fs::path executable = fs::absolute(fs::path(argv[0]), pathError).parent_path();
		if (!pathError) {
			itemCandidates.push_back(executable / "data" / "items.json");
			itemCandidates.push_back(executable / ".." / ".." / "Trauma" / "data" / "items.json");
			itemCandidates.push_back(executable / ".." / ".." / "data" / "items.json");
		}
	}
	fs::path itemPath;
	for (const fs::path& candidate : itemCandidates) {
		std::error_code existsError;
		if (fs::exists(candidate, existsError) && !existsError) {
			itemPath = candidate;
			break;
		}
	}
	if (itemPath.empty()) {
		std::cout << "failed to find data/items.json" << std::endl;
		return 1;
	}
	std::string itemError;
	if (!Trauma::LoadItemRegistry(itemPath.string(), itemError)) {
		std::cout << itemError << std::endl;
		return 1;
	}

	Trauma::Net::Network network;
	if (!network.IsReady()) {
		std::cout << "failed to initialise winsock" << std::endl;
		return 1;
	}

	enum class Mode {
		Local,
		Server,
		Client
	};

	Mode mode = Mode::Local;
	std::string host = "127.0.0.1";
	uint16_t port = Trauma::Net::DefaultPort;
	bool valid = true;

	for (int index = 1; index < argc && valid; ++index) {
		const std::string argument = argv[index];
		if (argument == "--server") {
			mode = Mode::Server;
		}
		else if (argument == "--connect") {
			if (index + 1 >= argc) {
				valid = false;
				break;
			}

			mode = Mode::Client;
			host = argv[++index];
		}
		else if (argument == "--port") {
			if (index + 1 >= argc || !ParsePort(argv[++index], port)) {
				valid = false;
				break;
			}
		}
		else {
			valid = false;
		}
	}

	if (!valid) {
		std::cout << "improper usage or malformed arguments:" << std::endl;
		std::cout << "usage: Trauma [--server | --connect HOST] [--port PORT]" << std::endl;
		return 1;
	}

	if (mode == Mode::Server) {
		Trauma::Server server;
		return server.Run(port) ? 0 : 1;
	}

	Trauma::Server localServer;
	if (mode == Mode::Local) {
		if (!localServer.Start(0))
			return 1;

		port = localServer.GetPort();
	}

	Trauma::Game game(host, port);
	if (!game.Construct({ 1600, 900 }, { 1, 1 }))
		return 1;

	game.Start();
	return 0;
}
