#include "Server.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>

namespace Trauma {
	Server::~Server() {
		Stop();
	}

	bool Server::Start(uint16_t port) {
		if (!Open(port))
			return false;

		running = true;
		worker = std::thread(&Server::Loop, this);
		return true;
	}

	bool Server::Run(uint16_t port) {
		if (!Open(port))
			return false;

		running = true;
		Loop();
		return true;
	}

	void Server::Stop() {
		running = false;
		if (worker.joinable())
			worker.join();
		socket.Close();
	}

	uint16_t Server::GetPort() const {
		return socket.GetPort();
	}

	bool Server::Open(uint16_t port) {
		if (!socket.Open(port)) {
			std::cout << "[server] failed to bind UDP port " << port << std::endl;
			return false;
		}

		for (ClientSlot& client : clients)
			client = ClientSlot{};

		tick = 0;
		std::cout << "[server] listening on port " << socket.GetPort() << std::endl;
		return true;
	}

	void Server::Loop() {
		using Clock = std::chrono::steady_clock;

		auto previous = Clock::now();
		double accumulator = 0.0;

		while (running) {
			const auto now = Clock::now();
			accumulator += std::chrono::duration<double>(now - previous).count();
			previous = now;
			accumulator = std::min(accumulator, 0.25);

			int steps = 0;
			while (accumulator >= FixedDeltaTime && steps < 8 && running) {
				PollPackets();
				StepSimulation();
				DropTimedOutClients();

				if (tick % Net::SnapshotIntervalTicks == 0)
					BroadcastSnapshot();

				accumulator -= FixedDeltaTime;
				++steps;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void Server::PollPackets() {
		std::array<uint8_t, 2048> buffer{};
		while (true) {
			Net::Endpoint from;
			const int received = socket.ReceiveFrom(from, buffer.data(), buffer.size());
			if (received <= 0)
				return;

			Net::PacketReader reader(buffer.data(), static_cast<size_t>(received));
			switch (reader.GetType()) {
				case Net::MessageType::Hello:
					HandleHello(from, reader);
					break;
				case Net::MessageType::Input:
					HandleInput(from, reader);
					break;
				default:
					break;
			}
		}
	}

	void Server::StepSimulation() {
		for (ClientSlot& client : clients) {
			if (!client.bActive)
				continue;

			while (!client.inputs.empty()) {
				const QueuedInput queued = client.inputs.front();
				client.inputs.pop_front();
				client.player.lastInput = queued.sequence;
				StepPhysics(client.player, queued.input, FixedDeltaTime);
			}
		}

		++tick;
	}

	void Server::BroadcastSnapshot() {
		Net::PacketWriter packet(Net::MessageType::Snapshot);
		packet.Write(tick);

		uint8_t count = 0;
		for (const ClientSlot& client : clients)
			if (client.bActive)
				++count;
		packet.Write(count);

		for (const ClientSlot& client : clients) {
			if (client.bActive)
				Net::WritePlayerState(packet, client.id, client.player);
		}

		for (const ClientSlot& client : clients) {
			if (client.bActive)
				socket.SendTo(client.endpoint, packet.Data().data(), packet.Size());
		}
	}

	void Server::DropTimedOutClients() {
		const auto now = std::chrono::steady_clock::now();
		const auto timeout = std::chrono::duration<double>(Net::ConnectionTimeoutSeconds);

		for (ClientSlot& client : clients) {
			if (!client.bActive || now - client.lastSeen < timeout)
				continue;

			std::cout << "[server] player " << static_cast<int>(client.id) << " timed out" << std::endl;
			client = ClientSlot{};
		}
	}

	Server::ClientSlot* Server::FindClient(const Net::Endpoint& endpoint) {
		for (ClientSlot& client : clients)
			if (client.bActive && client.endpoint == endpoint)
				return &client;

		return nullptr;
	}

	Server::ClientSlot* Server::AllocateClient(const Net::Endpoint& endpoint) {
		for (size_t index = 0; index < clients.size(); ++index) {
			if (clients[index].bActive)
				continue;

			ClientSlot& client = clients[index];
			client = ClientSlot{};
			client.bActive = true;
			client.endpoint = endpoint;
			client.id = static_cast<uint8_t>(index + 1);
			client.lastSeen = std::chrono::steady_clock::now();
			client.player.position = { 0, 0 };
			client.player.grounded = true;

			std::cout << "[server] player " << static_cast<int>(client.id)
				<< " connected from " << endpoint.ToString() << std::endl;

			return &client;
		}

		std::cout << "[server] rejected client, server is full" << std::endl;
		return nullptr;
	}

	void Server::HandleHello(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		uint32_t version = 0;
		if (!reader.Read(version) || version != Net::ProtocolVersion)
			return;

		ClientSlot* client = FindClient(endpoint);
		if (!client)
			client = AllocateClient(endpoint);
		if (!client)
			return;

		client->lastSeen = std::chrono::steady_clock::now();
		SendWelcome(*client);
	}

	void Server::HandleInput(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		uint8_t count = 0;
		if (!reader.Read(count) || count == 0 || count > Net::MaxInputEntries)
			return;

		std::array<QueuedInput, Net::MaxInputEntries> entries{};
		for (uint8_t index = 0; index < count; ++index) {
			uint32_t sequence = 0;
			InputState input;
			if (!Net::ReadInputEntry(reader, sequence, input))
				return;

			entries[index] = { sequence, input };
		}

		client->lastSeen = std::chrono::steady_clock::now();
		for (uint8_t index = 0; index < count; ++index) {
			const QueuedInput& entry = entries[index];
			if (entry.sequence <= client->highestReceived)
				continue;

			client->highestReceived = entry.sequence;
			client->inputs.push_back(entry);
		}

		while (client->inputs.size() > 32)
			client->inputs.pop_front();
	}

	void Server::SendWelcome(const ClientSlot& client) {
		Net::PacketWriter packet(Net::MessageType::Welcome);
		packet.Write(Net::ProtocolVersion);
		Net::WritePlayerState(packet, client.id, client.player);
		socket.SendTo(client.endpoint, packet.Data().data(), packet.Size());
	}
}
