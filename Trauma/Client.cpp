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
		predicted = RagdollState{};
		connectingTime = 0.0f;
		helloCooldown = 0.0f;
		pending.clear();
		predictedBodies.clear();
		predictedRagdolls.clear();
		players.clear();
		worldItems = {};
		assemblingTick = 0;
		assemblingCount = 0;
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
		predictedBodies.clear();
		predictedRagdolls.clear();
		players.clear();
		worldItems = {};
		assemblingTick = 0;
		assemblingCount = 0;
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

		for (size_t i = 0; i < PartCount; ++i)
			previousLocal[i] = predicted.parts[i].position;
		for (PredictedRagdoll& ragdoll : predictedRagdolls)
			for (size_t i = 0; i < PartCount; ++i)
				ragdoll.previous[i] = ragdoll.state.parts[i].position;
		for (PredictedBody& body : predictedBodies)
		{
			body.previousPosition = body.state.position;
			body.previousAngle = body.state.angle;
		}
		StepPrediction(input);
		predicted.parts[Torso].lastInput = sent.sequence;

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

	const RagdollState& Client::GetLocalRagdoll() const {
		return predicted;
	}

	std::array<olc::vf2d, PartCount> Client::GetLocalPose(float alpha) const {
		std::array<olc::vf2d, PartCount> pose;
		for (size_t i = 0; i < PartCount; ++i)
			pose[i] = previousLocal[i].lerp(predicted.parts[i].position, std::clamp(alpha, 0.0f, 1.0f));
		return pose;
	}

	bool Client::GetRemotePose(uint8_t id, float alpha, std::array<olc::vf2d, PartCount>& pose, float& facing) const {
		for (const PredictedRagdoll& ragdoll : predictedRagdolls) {
			if (ragdoll.id != id)
				continue;
			for (size_t i = 0; i < PartCount; ++i)
				pose[i] = ragdoll.previous[i].lerp(ragdoll.state.parts[i].position, std::clamp(alpha, 0.0f, 1.0f));
			facing = ragdoll.state.facing;
			return true;
		}
		return false;
	}

	const std::vector<Client::ObjectView>& Client::GetObjects() const {
		return players;
	}

	const std::array<Client::WorldItem, MaxWorldItems>& Client::GetWorldItems() const {
		return worldItems;
	}

	void Client::MoveItem(bool fromClothing, size_t from, bool toClothing, size_t to) {
		if (status != Status::Connected)
			return;
		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryMove(packet, fromClothing, static_cast<uint8_t>(from), toClothing,
			static_cast<uint8_t>(to));
		SendPacket(packet);
		MoveInventoryItem(predicted.inventory, fromClothing, from, toClothing, to);
	}

	void Client::DropItem(bool fromClothing, size_t index, const olc::vf2d& position) {
		if (status != Status::Connected)
			return;
		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryDrop(packet, fromClothing, static_cast<uint8_t>(index), position);
		SendPacket(packet);
		TakeInventoryItem(predicted.inventory, fromClothing, index);
	}

	bool Client::GetGrippedItem(size_t& index) const {
		const uint8_t base = static_cast<uint8_t>(TestTerrain().size() + TestBodyCount);
		for (const HandGrip& grip : predicted.grips) {
			if (grip.target <= base)
				continue;
			const size_t candidate = static_cast<size_t>(grip.target) - base - 1;
			if (candidate >= MaxWorldItems)
				continue;
			index = candidate;
			return true;
		}
		return false;
	}

	void Client::PickupItem(size_t index) {
		if (status != Status::Connected || index >= MaxWorldItems)
			return;
		if (worldItems[index].item == Item::None)
			return;
		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryPickup(packet, static_cast<uint8_t>(index));
		SendPacket(packet);
		for (Item& slot : predicted.inventory.hotbar)
			if (slot == Item::None) {
				slot = worldItems[index].item;
				break;
			}
		const uint8_t target = static_cast<uint8_t>(TestTerrain().size() + TestBodyCount + index + 1);
		for (HandGrip& grip : predicted.grips)
			if (grip.target == target)
				grip = {};
		worldItems[index] = WorldItem{};
	}

	bool Client::GetPredictedBody(uint8_t id, float alpha, olc::vf2d& position, float& angle) const {
		const float blend = std::clamp(alpha, 0.0f, 1.0f);
		for (const PredictedBody& body : predictedBodies) {
			if (body.id == id) {
				position = body.previousPosition.lerp(body.state.position, blend);
				angle = body.previousAngle + std::remainder(body.state.angle - body.previousAngle, 6.2831853f) * blend;
				return true;
			}
		}
		return false;
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
		RagdollState welcome;
		if (!Net::ReadRagdoll(reader, id, welcome))
			return;

		if (status == Status::Connected && playerId == id)
			return;

		playerId = id;
		predicted = welcome;
		for (size_t i = 0; i < PartCount; ++i)
			previousLocal[i] = predicted.parts[i].position;
		pending.clear();
		predictedBodies.clear();
		predictedRagdolls.clear();
		players.clear();
		worldItems = {};
		assemblingTick = 0;
		assemblingCount = 0;
		nextInputSequence = 1;
		snapshotTimer = 0.0f;
		latestServerTick = 0;
		status = Status::Connected;

		std::cout << "[client] connected as player " << static_cast<int>(playerId) << std::endl;
	}

	void Client::HandleSnapshot(Net::PacketReader& reader) {
		uint32_t serverTick = 0;
		uint8_t index = 0, count = 0;
		if (!reader.Read(serverTick) || !reader.Read(index) || !reader.Read(count)
			|| count < 2 || count > MaxPlayers + 1 || index >= count
			|| serverTick <= latestServerTick || serverTick < assemblingTick)
			return;

		SnapshotChunk chunk;
		if (index + 1 == count) {
			for (size_t i = 0; i < TestBodyCount; ++i) {
				uint8_t id = 0;
				if (!Net::ReadObjectState(reader, id, chunk.objects[i]) || id != MaxPlayers + i + 1)
					return;
			}
			uint8_t itemCount = 0;
			if (!reader.Read(itemCount) || itemCount != MaxWorldItems)
				return;
			for (size_t i = 0; i < MaxWorldItems; ++i) {
				uint8_t kind = 0;
				olc::vf2d position;
				olc::vf2d velocity;
				float angle = 0.0f;
				float spin = 0.0f;
				if (!reader.Read(kind) || !reader.Read(position.x) || !reader.Read(position.y)
					|| !reader.Read(velocity.x) || !reader.Read(velocity.y)
					|| !reader.Read(angle) || !reader.Read(spin)
					|| kind > static_cast<uint8_t>(Item::Boots)
					|| !std::isfinite(position.x) || !std::isfinite(position.y)
					|| !std::isfinite(velocity.x) || !std::isfinite(velocity.y)
					|| !std::isfinite(angle) || !std::isfinite(spin))
					return;
				chunk.items[i].item = static_cast<Item>(kind);
				chunk.items[i].body = CreateItemBody(static_cast<Item>(kind), position);
				chunk.items[i].body.velocity = velocity;
				chunk.items[i].body.angle = angle;
				chunk.items[i].body.angularVelocity = spin;
			}
		}
		else if (!Net::ReadRagdoll(reader, chunk.id, chunk.ragdoll) || chunk.id == 0 || chunk.id > MaxPlayers)
			return;

		if (serverTick > assemblingTick) {
			snapshotChunks = {};
			assemblingTick = serverTick;
			assemblingCount = count;
		}
		if (count != assemblingCount)
			return;
		chunk.received = true;
		snapshotChunks[index] = chunk;
		for (size_t i = 0; i < count; ++i)
			if (!snapshotChunks[i].received)
				return;

		bool sawLocal = false;
		RagdollState authoritative;
		std::vector<PredictedRagdoll> receivedRagdolls;
		std::vector<PredictedBody> receivedBodies;
		std::vector<ObjectView> received;
		uint8_t previousId = 0;
		for (size_t i = 0; i + 1 < count; ++i) {
			const SnapshotChunk& part = snapshotChunks[i];
			if (part.id <= previousId)
				return;
			previousId = part.id;
			if (part.id == playerId) {
				authoritative = part.ragdoll;
				sawLocal = true;
				continue;
			}
			PredictedRagdoll other;
			other.id = part.id;
			other.state = part.ragdoll;
			for (size_t j = 0; j < PartCount; ++j)
				other.previous[j] = other.state.parts[j].position;
			for (const PredictedRagdoll& existing : predictedRagdolls)
				if (existing.id == other.id)
					other.previous = existing.previous;
			receivedRagdolls.push_back(other);
			ObjectView view;
			view.id = part.id;
			view.position = view.target = part.ragdoll.parts[Torso].position;
			received.push_back(view);
		}
		if (!sawLocal) {
			Disconnect();
			return;
		}
		for (size_t i = 0; i < TestBodyCount; ++i) {
			const uint8_t id = static_cast<uint8_t>(MaxPlayers + i + 1);
			const ObjectState& incoming = snapshotChunks[count - 1].objects[i];
			PredictedBody body{id, incoming, incoming.position};
			body.previousAngle = incoming.angle;
			for (const PredictedBody& existing : predictedBodies)
				if (existing.id == id) {
					body.previousPosition = existing.previousPosition;
					body.previousAngle = existing.previousAngle;
				}
			receivedBodies.push_back(body);
			ObjectView view;
			view.id = id;
			view.position = view.target = incoming.position;
			view.radius = incoming.radius;
			view.shape = incoming.shape;
			view.halfExtents = incoming.halfExtents;
			view.vertexCount = incoming.vertexCount;
			view.localVertices = incoming.localVertices;
			view.angle = incoming.angle;
			received.push_back(view);
		}

		latestServerTick = serverTick;
		predictedRagdolls = std::move(receivedRagdolls);
		predictedBodies = std::move(receivedBodies);
		players = std::move(received);
		worldItems = snapshotChunks[count - 1].items;
		Reconcile(authoritative, authoritative.parts[Torso].lastInput);
		snapshotTimer = 0.0f;
	}

	void Client::Reconcile(const RagdollState& authoritative, uint32_t lastInput) {
		predicted = authoritative;
		while (!pending.empty() && pending.front().sequence <= lastInput)
			pending.pop_front();
		for (const SentInput& sent : pending)
			StepPrediction(sent.input);
	}

	void Client::StepPrediction(const InputState& input) {
		ApplyRagdollInput(predicted, input);
		std::vector<RagdollState*> ragdolls;
		std::vector<ObjectState*> bodies;
		bool addedLocal = false;
		for (PredictedRagdoll& other : predictedRagdolls) {
			if (!addedLocal && other.id > playerId) {
				ragdolls.push_back(&predicted);
				addedLocal = true;
			}
			InputState remoteInput = other.state.input;
			remoteInput.jump = false;
			ApplyRagdollInput(other.state, remoteInput);
			ragdolls.push_back(&other.state);
		}
		if (!addedLocal)
			ragdolls.push_back(&predicted);
		for (PredictedBody& body : predictedBodies)
			bodies.push_back(&body.state);
		for (WorldItem& item : worldItems)
			bodies.push_back(&item.body);
		StepPhysics(ragdolls, bodies, FixedDeltaTime);
	}

	void Client::SmoothRemoteObjects(float fElapsedTime) {
		const float blend = std::min(1.0f, fElapsedTime * 14.0f);
		for (ObjectView& object : players)
			object.position = object.position.lerp(object.target, blend);
	}
}
