#include "Client.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

namespace Trauma {
	Client::~Client() {
		Disconnect();
	}

	bool Client::Connect(const std::string& host, uint16_t port) {
		Disconnect();

		if (!Net::ResolveEndpoint(host, port, server)) {
			std::cout << "[client] failed to resolve " << host << std::endl;
			return false;
		}

		if (!socket.Open(0)) {
			std::cout << "[client] failed to open UDP socket" << std::endl;
			return false;
		}

		status = Status::Connecting;
		playerId = 0;
		predicted = ObjectState{};
		connectingTime = 0.0f;
		helloCooldown = 0.0f;
		pending.clear();
		players.clear();
		nextInputSequence = 1;

		std::cout << "[client] connecting to " << host << ":" << port << std::endl;
		SendHello();
		helloCooldown = 0.5f;
		return true;
	}

	void Client::Disconnect() {
		if (status != Status::Disconnected)
			std::cout << "[client] disconnected" << std::endl;

		status = Status::Disconnected;
		socket.Close();
		pending.clear();
		players.clear();
		playerId = 0;
		snapshotTimer = 0.0f;
		latestServerTick = 0;
	}

	void Client::StepInput(const InputState& input) {
		if (status != Status::Connected)
			return;

		SentInput sent;
		sent.sequence = nextInputSequence++;
		sent.input = input;
		pending.push_back(sent);

		const uint8_t resendCount = static_cast<uint8_t>(std::min<size_t>(pending.size(), Net::InputRedundancy));
		Net::PacketWriter packet(Net::MessageType::Input);
		packet.Write(resendCount);

		const size_t first = pending.size() - resendCount;
		for (size_t index = first; index < pending.size(); ++index)
			Net::WriteInputEntry(packet, pending[index].sequence, pending[index].input);

		SendPacket(packet);

		StepPhysics(predicted, input, FixedDeltaTime);
		predicted.lastInput = sent.sequence;

		while (pending.size() > 240)
			pending.pop_front();
	}

	void Client::Poll(float fElapsedTime) {
		if (status == Status::Connecting) {
			connectingTime += fElapsedTime;
			helloCooldown -= fElapsedTime;

			if (helloCooldown <= 0.0f) {
				SendHello();
				helloCooldown = 0.5f;
			}

			PollPackets();

			if (status == Status::Connecting && connectingTime > Net::ConnectionTimeoutSeconds) {
				std::cout << "[client] connection timed out" << std::endl;
				Disconnect();
			}
			return;
		}

		if (status != Status::Connected)
			return;

		snapshotTimer += fElapsedTime;
		PollPackets();

		if (status != Status::Connected)
			return;

		if (snapshotTimer > Net::ConnectionTimeoutSeconds) {
			std::cout << "[client] server timed out" << std::endl;
			Disconnect();
			return;
		}

		SmoothRemoteObjects(fElapsedTime);
	}

	Client::Status Client::GetStatus() const {
		return status;
	}

	uint8_t Client::GetPlayerId() const {
		return playerId;
	}

	const ObjectState& Client::GetLocalState() const {
		return predicted;
	}

	const std::vector<Client::ObjectView>& Client::GetObjects() const {
		return players;
	}

	void Client::SendHello() {
		Net::PacketWriter packet(Net::MessageType::Hello);
		packet.Write(Net::ProtocolVersion);
		SendPacket(packet);
	}

	void Client::SendPacket(const Net::PacketWriter& packet) const {
		socket.SendTo(server, packet.Data().data(), packet.Size());
	}

	void Client::PollPackets() {
		std::array<uint8_t, 2048> buffer{};
		while (true) {
			Net::Endpoint from;
			const int received = socket.ReceiveFrom(from, buffer.data(), buffer.size());
			if (received <= 0)
				return;

			if (!(from == server))
				continue;

			Net::PacketReader reader(buffer.data(), static_cast<size_t>(received));
			HandlePacket(reader);
		}
	}

	void Client::HandlePacket(Net::PacketReader& reader) {
		switch (reader.GetType()) {
			case Net::MessageType::Welcome:
				HandleWelcome(reader);
				break;
			case Net::MessageType::Snapshot:
				HandleSnapshot(reader);
				break;
			default:
				break;
		}
	}

	void Client::HandleWelcome(Net::PacketReader& reader) {
		uint32_t version = 0;
		if (!reader.Read(version))
			return;

		if (version != Net::ProtocolVersion) {
			std::cout << "[client] protocol version mismatch" << std::endl;
			Disconnect();
			return;
		}

		uint8_t id = 0;
		ObjectState welcome;
		if (!Net::ReadPlayerState(reader, id, welcome))
			return;

		if (status == Status::Connected && playerId == id)
			return;

		playerId = id;
		predicted = welcome;
		pending.clear();
		players.clear();
		nextInputSequence = 1;
		snapshotTimer = 0.0f;
		latestServerTick = 0;
		status = Status::Connected;

		std::cout << "[client] connected as player " << static_cast<int>(playerId) << std::endl;
	}

	void Client::HandleSnapshot(Net::PacketReader& reader) {
		uint32_t serverTick = 0;
		uint8_t count = 0;
		if (!reader.Read(serverTick) || !reader.Read(count) || count > MaxPlayers)
			return;

		if (serverTick <= latestServerTick)
			return;

		latestServerTick = serverTick;
		bool sawLocalPlayer = false;
		std::vector<ObjectView> received;
		received.reserve(count);

		for (uint8_t index = 0; index < count; ++index) {
			uint8_t id = 0;
			ObjectState incoming;
			if (!Net::ReadPlayerState(reader, id, incoming))
				return;

			if (id == playerId) {
				Reconcile(incoming, incoming.lastInput);
				sawLocalPlayer = true;
				continue;
			}

			ObjectView view;
			view.id = id;
			view.position = incoming.position;
			view.target = incoming.position;

			for (const ObjectView& existing : players) {
				if (existing.id != id)
					continue;

				view.position = existing.position;
				view.facing = existing.facing;
				break;
			}

			if (std::abs(incoming.velocity.x) > 1.0f)
				view.facing = incoming.velocity.x > 0.0f ? 1.0f : -1.0f;

			received.push_back(view);
		}

		if (!sawLocalPlayer) {
			Disconnect();
			return;
		}

		players = std::move(received);
		snapshotTimer = 0.0f;
	}

	void Client::Reconcile(const ObjectState& authoritative, uint32_t lastInput) {
		predicted = authoritative;

		while (!pending.empty() && pending.front().sequence <= lastInput)
			pending.pop_front();

		for (const SentInput& sent : pending)
			StepPhysics(predicted, sent.input, FixedDeltaTime);
	}

	void Client::SmoothRemoteObjects(float fElapsedTime) {
		const float blend = std::min(1.0f, fElapsedTime * 14.0f);
		for (ObjectView& object : players)
			object.position = object.position.lerp(object.target, blend);
	}
}
