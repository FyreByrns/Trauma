#include "Client.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

namespace {
	constexpr size_t MaxPredictionSnapshots = 96;
	constexpr float ShotTracerLifetime = 0.08f;
	constexpr float ReconcilePositionTolerance = 3.0f;
	constexpr float ReconcileAngleTolerance = 0.1f;
	constexpr float ReconcileSpeedTolerance = 8.0f;

	bool NeedsReconcile(const Trauma::ObjectState& incoming, b2BodyId body) {
		if (!b2Body_IsValid(body))
			return true;
		const b2Transform current = b2Body_GetTransform(body);
		const olc::vf2d difference = incoming.position - olc::vf2d{ current.p.x, current.p.y };
		const float angleDifference = std::abs(std::remainder(
			incoming.angle - b2Rot_GetAngle(current.q), Trauma::TwoPi));
		const bool moving = incoming.velocity.mag2() > ReconcileSpeedTolerance * ReconcileSpeedTolerance
			|| std::abs(incoming.angularVelocity) > 0.25f;
		return moving || difference.mag2() > ReconcilePositionTolerance * ReconcilePositionTolerance
			|| angleDifference > ReconcileAngleTolerance;
	}
}

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

		ResetSession();
		status = Status::Connecting;
		connectingTime = 0.0f;
		helloCooldown = 0.0f;

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
		ResetSession();
	}

	void Client::ResetSession() {
		playerId = 0;
		ResetWorld(physics);
		CreateTerrainBodies(physics);
		predicted = RagdollState{};
		previousLocal = {};
		predictedRagdolls.clear();
		predictedBodies.clear();
		players.clear();
		worldItems = {};
		shelves = {};
		shotTracers.clear();
		shotCooldown = 0.0f;
		pending.clear();
		snapshots.clear();
		assemblingTick = 0;
		assemblingCount = 0;
		nextInputSequence = 1;
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
		for (PredictedBody& body : predictedBodies) {
			body.previousPosition = body.state.position;
			body.previousAngle = body.state.angle;
		}

		StepPrediction(input);
		predicted.parts[Torso].lastInput = sent.sequence;
		CaptureSnapshot(sent.sequence);

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

		shotCooldown = std::max(0.0f, shotCooldown - fElapsedTime);
		for (auto tracer = shotTracers.begin(); tracer != shotTracers.end();) {
			tracer->life -= fElapsedTime;
			if (tracer->life <= 0.0f)
				tracer = shotTracers.erase(tracer);
			else
				++tracer;
		}

		SmoothRemoteObjects(fElapsedTime);
	}

	Client::Status Client::GetStatus() const {
		return status;
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

	const std::array<Client::ShelfView, MaxShelves>& Client::GetShelves() const {
		return shelves;
	}

	const std::vector<Client::ShotTracer>& Client::GetShotTracers() const {
		return shotTracers;
	}

	int Client::ShelfAt(const olc::vf2d& position) const {
		for (size_t i = 0; i < shelves.size(); ++i) {
			const ShelfView& shelf = shelves[i];
			if (!shelf.active)
				continue;
			const olc::vf2d local = position - shelf.position;
			if (local.x >= -ShelfHalfWidth && local.x <= ShelfHalfWidth
				&& local.y >= ShelfPickTop && local.y <= ShelfPickBottom)
				return static_cast<int>(i);
		}
		return -1;
	}

	bool Client::IsShelfDrop(const olc::vf2d& position) const {
		for (const ShelfView& shelf : shelves) {
			if (!shelf.active)
				continue;
			float boardTop = 0.0f;
			if (ShelfSlotAt(position - shelf.position, boardTop))
				return true;
		}
		return false;
	}

	void Client::PlaceShelf(const olc::vf2d& position) {
		if (status != Status::Connected || predicted.inventory.shelfKits == 0
			|| !HasShelfTool(predicted.inventory))
			return;
		Net::PacketWriter packet(Net::MessageType::Shelf);
		Net::WriteShelfPlace(packet, position);
		SendPacket(packet);
		ConsumeShelfKit(predicted.inventory);
	}

	void Client::MoveShelf(uint8_t index, const olc::vf2d& position) {
		if (status != Status::Connected || index >= MaxShelves || !shelves[index].active)
			return;
		Net::PacketWriter packet(Net::MessageType::Shelf);
		Net::WriteShelfMove(packet, index, position);
		SendPacket(packet);
		shelves[index].previousPosition = shelves[index].position;
		shelves[index].position = position;
	}

	void Client::FireGun(size_t hotbar, const olc::vf2d& target) {
		if (status != Status::Connected || shotCooldown > 0.0f)
			return;
		Inventory& inventory = predicted.inventory;
		if (hotbar >= inventory.hotbar.size() || !IsGun(inventory.hotbar[hotbar].item)
			|| !LoadedMagazine(inventory, hotbar))
			return;

		const olc::vf2d origin = predicted.parts[RightHand].position;
		const olc::vf2d delta = target - origin;
		const float distance = delta.mag();
		if (distance < 0.001f)
			return;

		Net::PacketWriter packet(Net::MessageType::Shoot);
		Net::WriteShoot(packet, static_cast<uint8_t>(hotbar), target);
		SendPacket(packet);
		if (!ConsumeGunRound(inventory, hotbar))
			return;

		const olc::vf2d direction = delta * (1.0f / distance);
		ShotTracer tracer;
		tracer.start = origin;
		tracer.end = origin + direction * std::min(distance, GunRange);
		tracer.life = ShotTracerLifetime;
		shotTracers.push_back(tracer);
		shotCooldown = static_cast<float>(GunCooldownTicks) * FixedDeltaTime;
	}

	void Client::MoveItem(const SlotAddress& from, const SlotAddress& to) {
		if (status != Status::Connected)
			return;
		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryMove(packet, from, to);
		SendPacket(packet);
		MoveInventoryItem(predicted.inventory, from, to);
	}

	void Client::DropItem(const SlotAddress& from, const olc::vf2d& position) {
		if (status != Status::Connected)
			return;
		const ItemStack* source = ResolveSlot(predicted.inventory, from);
		if (!source || !IsWorldItem(source->item))
			return;
		const ItemId item = source->item;

		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryDrop(packet, from, position);
		SendPacket(packet);

		ItemStack carried;
		if (!RemoveStack(predicted.inventory, from, carried))
			return;
		if (IsShelfDrop(position))
			return;

		for (WorldItem& slot : worldItems)
			if (slot.stack.item == ItemNone) {
				slot.stack = std::move(carried);
				slot.body = CreateItemBody(item, position);
				slot.body.velocity = { 0.0f, 40.0f };
				break;
			}
	}

	bool Client::GetPickupCandidate(PickupTarget& target) const {
		const uint8_t base = static_cast<uint8_t>(TestTerrain().size() + TestBodyCount);
		for (const HandGrip& grip : predicted.grips) {
			if (grip.target <= base)
				continue;
			const size_t candidate = static_cast<size_t>(grip.target) - base - 1;
			if (candidate >= MaxWorldItems)
				continue;
			target = {};
			target.index = static_cast<uint8_t>(candidate);
			return true;
		}
		for (size_t slot = 0; slot < worldItems.size(); ++slot)
			if (worldItems[slot].stack.item != ItemNone
				&& HandsOnItem(predicted, worldItems[slot].body)) {
				target = {};
				target.index = static_cast<uint8_t>(slot);
				return true;
			}
		for (size_t shelfIndex = 0; shelfIndex < shelves.size(); ++shelfIndex) {
			const ShelfView& shelf = shelves[shelfIndex];
			if (!shelf.active)
				continue;
			for (size_t itemIndex = 0; itemIndex < shelf.items.size(); ++itemIndex) {
				const ShelfItemView& item = shelf.items[itemIndex];
				if (item.stack.item == ItemNone)
					continue;
				ObjectState body = item.body;
				body.position += shelf.position;
				if (!HandsOnItem(predicted, body))
					continue;
				target = {};
				target.shelf = true;
				target.index = static_cast<uint8_t>(shelfIndex);
				target.item = static_cast<uint8_t>(itemIndex);
				return true;
			}
		}
		return false;
	}

	void Client::PickupItem(const PickupTarget& target) {
		if (status != Status::Connected)
			return;

		if (target.shelf) {
			if (target.index >= MaxShelves || target.item >= MaxShelfItems)
				return;
			ShelfView& shelf = shelves[target.index];
			if (!shelf.active || shelf.items[target.item].stack.item == ItemNone)
				return;
			const size_t slot = FindEmptyHotbarSlot(predicted.inventory,
				shelf.items[target.item].stack);
			if (slot >= predicted.inventory.hotbar.size())
				return;

			Net::PacketWriter packet(Net::MessageType::Inventory);
			Net::WriteShelfPickup(packet, target.index, target.item);
			SendPacket(packet);

			predicted.inventory.hotbar[slot] = shelf.items[target.item].stack;
			shelf.items[target.item] = ShelfItemView{};
			return;
		}

		if (target.index >= MaxWorldItems || worldItems[target.index].stack.item == ItemNone)
			return;
		const size_t slot = FindEmptyHotbarSlot(predicted.inventory, worldItems[target.index].stack);
		if (slot >= predicted.inventory.hotbar.size())
			return;

		Net::PacketWriter packet(Net::MessageType::Inventory);
		Net::WriteInventoryPickup(packet, target.index);
		SendPacket(packet);

		predicted.inventory.hotbar[slot] = worldItems[target.index].stack;

		const uint8_t targetId = static_cast<uint8_t>(TestTerrain().size() + TestBodyCount + target.index + 1);
		for (HandGrip& grip : predicted.grips)
			if (grip.target == targetId)
				grip = {};

		worldItems[target.index] = WorldItem{};
	}

	bool Client::GetPredictedBody(uint8_t id, float alpha, olc::vf2d& position, float& angle) const {
		const float blend = std::clamp(alpha, 0.0f, 1.0f);
		for (const PredictedBody& body : predictedBodies) {
			if (body.id != id)
				continue;
			position = body.previousPosition.lerp(body.state.position, blend);
			angle = body.previousAngle + std::remainder(body.state.angle - body.previousAngle, TwoPi) * blend;
			return true;
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
		std::array<uint8_t, 32768> buffer{};
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

		ResetSession();
		playerId = id;
		predicted = welcome;
		for (size_t i = 0; i < PartCount; ++i)
			previousLocal[i] = predicted.parts[i].position;
		status = Status::Connected;

		std::cout << "[client] connected as player " << static_cast<int>(playerId) << std::endl;
	}

	void Client::HandleSnapshot(Net::PacketReader& reader) {
		uint32_t serverTick = 0;
		uint8_t index = 0;
		uint8_t count = 0;
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
				ItemStack stack;
				ObjectState body;
				if (!Net::ReadItemBody(reader, stack, body))
					return;
				if (stack.item != ItemNone && !IsWorldItem(stack.item))
					return;
				chunk.items[i].stack = std::move(stack);
				chunk.items[i].body = body;
			}

			uint8_t shelfCount = 0;
			if (!reader.Read(shelfCount) || shelfCount != MaxShelves)
				return;
			for (size_t i = 0; i < MaxShelves; ++i) {
				uint8_t active = 0;
				if (!reader.Read(active) || active > 1)
					return;
				ShelfView view;
				view.active = active != 0;
				if (view.active) {
					if (!reader.Read(view.position.x) || !reader.Read(view.position.y)
						|| !std::isfinite(view.position.x) || !std::isfinite(view.position.y)
						|| std::abs(view.position.x) > 2000.0f
						|| view.position.y < -800.0f || view.position.y > 1000.0f)
						return;
					view.previousPosition = view.position;

					uint8_t itemCount = 0;
					if (!reader.Read(itemCount) || itemCount != MaxShelfItems)
						return;
					for (size_t j = 0; j < MaxShelfItems; ++j) {
						ItemStack stack;
						ObjectState body;
						if (!Net::ReadItemBody(reader, stack, body))
							return;
						if (stack.item != ItemNone && !IsWorldItem(stack.item))
							return;
						ShelfItemView& item = view.items[j];
						item.stack = std::move(stack);
						if (item.stack.item == ItemNone)
							continue;
						if (std::abs(body.position.x) > 500.0f || std::abs(body.position.y) > 500.0f)
							return;
						item.body = body;
						item.previousPosition = body.position;
						item.previousAngle = body.angle;
					}
				}
				chunk.shelves[i] = view;
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

		ApplySnapshot(serverTick, count);
	}

	void Client::ApplySnapshot(uint32_t serverTick, uint8_t count) {
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
			PredictedBody body{ id, incoming, incoming.position };
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
		for (size_t i = 0; i < MaxShelves; ++i) {
			ShelfView incoming = snapshotChunks[count - 1].shelves[i];
			if (shelves[i].active && incoming.active) {
				incoming.previousPosition = shelves[i].position;
				for (size_t j = 0; j < MaxShelfItems; ++j) {
					const bool hadItem = shelves[i].items[j].stack.item != ItemNone;
					incoming.items[j].previousPosition = hadItem
						? shelves[i].items[j].body.position : incoming.items[j].body.position;
					incoming.items[j].previousAngle = hadItem
						? shelves[i].items[j].body.angle : incoming.items[j].body.angle;
				}
			} else {
				incoming.previousPosition = incoming.position;
				for (size_t j = 0; j < MaxShelfItems; ++j) {
					incoming.items[j].previousPosition = incoming.items[j].body.position;
					incoming.items[j].previousAngle = incoming.items[j].body.angle;
				}
			}
			shelves[i] = incoming;
		}
		Reconcile(authoritative, authoritative.parts[Torso].lastInput);
		snapshotTimer = 0.0f;
	}

	void Client::Reconcile(const RagdollState& authoritative, uint32_t lastInput) {
		const bool rewound = RestoreSnapshot(lastInput);
		predicted = authoritative;
		for (size_t i = 0; i < PartCount; ++i)
			SetBodyTransform(EnsureBody(physics, predicted.parts[i], RagdollGroup(physics, &predicted)),
				predicted.parts[i]);
		for (PredictedBody& body : predictedBodies) {
			const b2BodyId id = EnsureBody(physics, body.state);
			if (NeedsReconcile(body.state, id))
				SetBodyTransform(id, body.state);
		}
		for (WorldItem& item : worldItems) {
			const b2BodyId id = EnsureBody(physics, item.body);
			if (NeedsReconcile(item.body, id))
				SetBodyTransform(id, item.body);
		}
		while (!pending.empty() && pending.front().sequence <= lastInput)
			pending.pop_front();
		if (!rewound)
			return;
		for (const SentInput& sent : pending) {
			StepPrediction(sent.input);
			CaptureSnapshot(sent.sequence);
		}
	}

	void Client::CaptureSnapshot(uint32_t sequence) {
		const int size = b2World_GetSnapshot(physics.world, nullptr, 0);
		if (size <= 0)
			return;
		PhysicsSnapshot snapshot;
		snapshot.sequence = sequence;
		snapshot.image.resize(static_cast<size_t>(size));
		if (b2World_GetSnapshot(physics.world, snapshot.image.data(), size) != size)
			return;
		for (PhysicsSnapshot& existing : snapshots)
			if (existing.sequence == sequence) {
				existing = std::move(snapshot);
				return;
			}
		snapshots.push_back(std::move(snapshot));
		while (snapshots.size() > MaxPredictionSnapshots)
			snapshots.pop_front();
	}

	bool Client::RestoreSnapshot(uint32_t sequence) {
		for (auto it = snapshots.begin(); it != snapshots.end(); ++it) {
			if (it->sequence != sequence)
				continue;
			if (!b2World_RestoreFromSnapshot(physics.world, it->image.data(),
				static_cast<int>(it->image.size()))) {
				ResetWorld(physics);
				CreateTerrainBodies(physics);
				snapshots.clear();
				return false;
			}
			// Restored worlds can contain grip welds the rig no longer tracks
			for (const PhysicsRig& rig : physics.rigs) {
				for (size_t arm = 0; arm < rig.grips.size(); ++arm) {
					const b2BodyId hand = rig.bodies[arm == 0 ? LeftHand : RightHand];
					if (!b2Body_IsValid(hand))
						continue;
					b2JointId joints[8];
					const int count = b2Body_GetJoints(hand, joints, 8);
					for (int joint = 0; joint < count; ++joint) {
						if (b2Joint_GetType(joints[joint]) != b2_weldJoint)
							continue;
						const b2JointId tracked = rig.grips[arm];
						if (b2Joint_IsValid(tracked) && joints[joint].index1 == tracked.index1
							&& joints[joint].world0 == tracked.world0
							&& joints[joint].generation == tracked.generation)
							continue;
						b2DestroyJoint(joints[joint]);
					}
				}
			}
			snapshots.erase(snapshots.begin(), it + 1);
			return true;
		}
		return false;
	}

	void Client::StepPrediction(const InputState& input) {
		ApplyRagdollInput(physics, predicted, input);

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
			ApplyRagdollInput(physics, other.state, remoteInput);
			ragdolls.push_back(&other.state);
		}
		if (!addedLocal)
			ragdolls.push_back(&predicted);
		for (PredictedBody& body : predictedBodies)
			bodies.push_back(&body.state);
		for (WorldItem& item : worldItems)
			bodies.push_back(&item.body);

		StepPhysics(physics, ragdolls, bodies, FixedDeltaTime);
	}

	void Client::SmoothRemoteObjects(float fElapsedTime) {
		const float blend = std::min(1.0f, fElapsedTime * 14.0f);
		for (ObjectView& object : players)
			object.position = object.position.lerp(object.target, blend);
	}
}
