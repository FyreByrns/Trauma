#include "Server.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

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
		ClearShelves();
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
		ClearShelves();
		ResetWorld(physics);
		CreateTerrainBodies(physics);
		bodies = CreateTestBodies();
		for (size_t index = 0; index < items.size(); ++index) {
			items[index] = DeadItemBody();
			itemStacks[index] = ItemStack{};
		}

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
				RetryReliableMessages();
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
		std::array<uint8_t, Net::MaxDatagramSize> buffer{};
		while (true) {
			Net::Endpoint from;
			const int received = socket.ReceiveFrom(from, buffer.data(), buffer.size());
			if (received <= 0)
				return;

			Net::PacketReader reader(buffer.data(), static_cast<size_t>(received));
			const Net::MessageType type = reader.GetType();
			if (type == Net::MessageType::Fragment)
				HandleFragment(from, reader);
			else if (type == Net::MessageType::FragmentAck)
				HandleFragmentAck(from, reader);
			else
				HandleMessage(from, type, reader);
		}
	}

	void Server::HandleMessage(const Net::Endpoint& endpoint, Net::MessageType type, Net::PacketReader& reader) {
		switch (type) {
			case Net::MessageType::Hello:
				HandleHello(endpoint, reader);
				break;
			case Net::MessageType::Input:
				HandleInput(endpoint, reader);
				break;
			case Net::MessageType::Inventory:
				HandleInventory(endpoint, reader);
				break;
			case Net::MessageType::Shelf:
				HandleShelf(endpoint, reader);
				break;
			case Net::MessageType::Shoot:
				HandleShoot(endpoint, reader);
				break;
			default:
				break;
		}
	}

	InputState Server::SelectInput(ClientSlot& client) {
		InputState input = client.lastInput;
		input.jump = false;

		if (!client.inputs.empty()) {
			const QueuedInput queued = client.inputs.front();
			client.inputs.pop_front();
			client.player.parts[Torso].lastInput = queued.sequence;
			input = queued.input;
			client.lastInput = input;
			client.missingInputTicks = 0;
			return input;
		}

		if (client.missingInputTicks++ >= Net::InputRedundancy) {
			input.moveX = 0.0f;
			input.climb = 0.0f;
			input.run = false;
			input.crouch = false;
			input.crawl = false;
			input.reachLeft = false;
			input.reachRight = false;
		}
		return input;
	}

	void Server::StepSimulation() {
		std::vector<RagdollState*> ragdolls;
		std::vector<ObjectState*> activeBodies;
		ragdolls.reserve(MaxPlayers);
		activeBodies.reserve(TestBodyCount + MaxWorldItems);

		for (ClientSlot& client : clients) {
			if (!client.bActive)
				continue;

			ApplyRagdollInput(physics, client.player, SelectInput(client));
			ragdolls.push_back(&client.player);
		}
		for (ObjectState& body : bodies)
			activeBodies.push_back(&body);
		for (ObjectState& item : items)
			activeBodies.push_back(&item);

		StepPhysics(physics, ragdolls, activeBodies, FixedDeltaTime);
		StepShelves();
		++tick;
	}

	void Server::SendSnapshotTo(const std::vector<ClientSlot*>& recipients, const Net::PacketWriter& packet) {
		if (recipients.empty())
			return;
		if (packet.Size() <= Net::MaxDatagramSize) {
			for (const ClientSlot* recipient : recipients)
				socket.SendTo(recipient->endpoint, packet.Data().data(), packet.Size());
			return;
		}

		const uint32_t id = Net::AllocateMessageId(nextMessageId);
		const auto datagrams = Net::FragmentMessage(packet.Data(), id, false);
		for (const ClientSlot* recipient : recipients)
			SendDatagrams(recipient->endpoint, datagrams);
	}

	void Server::RetryReliableMessages() {
		const auto now = std::chrono::steady_clock::now();
		for (ClientSlot& client : clients) {
			if (!client.bActive)
				continue;

			for (auto message = client.reliable.begin(); message != client.reliable.end();) {
				if (now < message->nextRetry) {
					++message;
					continue;
				}
				if (message->attempts >= Net::MaxReliableAttempts) {
					message = client.reliable.erase(message);
					continue;
				}

				SendDatagrams(client.endpoint, message->datagrams);
				++message->attempts;
				message->nextRetry = now + std::chrono::milliseconds(Net::ReliableRetryMilliseconds);
				++message;
			}
		}
	}

	void Server::SendDatagrams(const Net::Endpoint& endpoint, const std::vector<std::vector<uint8_t>>& datagrams) {
		for (const auto& datagram : datagrams)
			socket.SendTo(endpoint, datagram.data(), datagram.size());
	}

	void Server::SendFragmentAck(const Net::Endpoint& endpoint, uint32_t id) {
		Net::PacketWriter packet(Net::MessageType::FragmentAck);
		packet.Write(id);
		socket.SendTo(endpoint, packet.Data().data(), packet.Size());
	}

	void Server::BroadcastSnapshot() {
		std::vector<ClientSlot*> recipients;
		for (ClientSlot& client : clients)
			if (client.bActive)
				recipients.push_back(&client);

		const uint8_t count = static_cast<uint8_t>(recipients.size() + 1);
		uint8_t index = 0;
		for (const ClientSlot* player : recipients) {
			Net::PacketWriter packet(Net::MessageType::Snapshot);
			packet.Write(tick);
			packet.Write(index++);
			packet.Write(count);
			Net::WriteRagdoll(packet, player->id, player->player);
			SendSnapshotTo(recipients, packet);
		}

		Net::PacketWriter packet(Net::MessageType::Snapshot);
		packet.Write(tick);
		packet.Write(index);
		packet.Write(count);
		for (size_t body = 0; body < bodies.size(); ++body)
			Net::WriteObjectState(packet, static_cast<uint8_t>(MaxPlayers + body + 1), bodies[body]);
		Net::WriteWorldItems(packet, itemStacks, items);
		packet.Write(static_cast<uint8_t>(MaxShelves));
		for (const Shelf& shelf : shelves) {
			packet.Write(static_cast<uint8_t>(shelf.active ? 1 : 0));
			if (!shelf.active)
				continue;
			packet.Write(shelf.position.x);
			packet.Write(shelf.position.y);
			packet.Write(static_cast<uint8_t>(MaxShelfItems));
			for (size_t i = 0; i < MaxShelfItems; ++i)
				Net::WriteWorldItem(packet, shelf.stacks[i], shelf.items[i]);
		}
		SendSnapshotTo(recipients, packet);
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
			client.player = CreateRagdoll({ -200.0f + static_cast<float>(index % 10) * 40.0f,
				-100.0f - static_cast<float>(index / 10) * 80.0f });

			std::cout << "[server] player " << static_cast<int>(client.id)
				<< " connected from " << endpoint.ToString() << std::endl;

			return &client;
		}

		std::cout << "[server] rejected client, server is full" << std::endl;
		return nullptr;
	}

	void Server::HandleFragment(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		Net::ReassembledMessage message;
		if (!client->fragments.Add(reader, message))
			return;
		if (message.reliable)
			SendFragmentAck(endpoint, message.id);
		if (message.data.empty())
			return;

		Net::PacketReader assembled(message.data.data(), message.data.size());
		const Net::MessageType type = assembled.GetType();
		if (type != Net::MessageType::Fragment && type != Net::MessageType::FragmentAck)
			HandleMessage(endpoint, type, assembled);
	}

	void Server::HandleFragmentAck(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		uint32_t id = 0;
		if (!reader.Read(id))
			return;

		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		for (auto message = client->reliable.begin(); message != client->reliable.end();) {
			if (message->id == id)
				message = client->reliable.erase(message);
			else
				++message;
		}
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
			if (!Net::ReadInputEntry(reader, sequence, input)
				|| !std::isfinite(input.moveX) || !std::isfinite(input.aim.x) || !std::isfinite(input.aim.y)
				|| std::abs(input.aim.x) > 2000.0f || std::abs(input.aim.y) > 2000.0f)
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

	void Server::HandleInventory(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		uint8_t action = 0;
		if (!reader.Read(action))
			return;
		client->lastSeen = std::chrono::steady_clock::now();

		switch (static_cast<Net::InventoryAction>(action)) {
			case Net::InventoryAction::Move:
				HandleInventoryMove(*client, reader);
				break;
			case Net::InventoryAction::Drop:
				HandleInventoryDrop(*client, reader);
				break;
			case Net::InventoryAction::Pickup:
				HandleInventoryPickup(*client, reader);
				break;
			case Net::InventoryAction::ShelfPickup:
				HandleShelfPickup(*client, reader);
				break;
			default:
				break;
		}
	}

	bool Server::HandleInventoryMove(ClientSlot& client, Net::PacketReader& reader) {
		SlotAddress from;
		SlotAddress to;
		if (!Net::ReadSlotAddress(reader, from) || !Net::ReadSlotAddress(reader, to))
			return false;
		return MoveInventoryItem(client.player.inventory, from, to);
	}

	bool Server::HandleInventoryPickup(ClientSlot& client, Net::PacketReader& reader) {
		uint8_t index = 0;
		if (!reader.Read(index) || index >= items.size())
			return false;
		if (itemStacks[index].item == ItemNone)
			return false;

		Inventory& inventory = client.player.inventory;
		SlotAddress slot;
		if (!FindEmptyCarrySlot(inventory, itemStacks[index], slot))
			return false;

		const uint8_t target = static_cast<uint8_t>(TestTerrain().size() + TestBodyCount + index + 1);
		bool holding = false;
		for (HandGrip& grip : client.player.grips)
			if (grip.target == target) {
				holding = true;
				grip = {};
			}
		if (!holding)
			holding = HandsOnItem(client.player, items[index]);
		if (!holding)
			return false;

		ItemStack* destination = ResolveSlot(inventory, slot);
		if (!destination)
			return false;
		*destination = std::move(itemStacks[index]);
		itemStacks[index] = ItemStack{};
		items[index] = DeadItemBody();
		return true;
	}

	bool Server::HandleShelfPickup(ClientSlot& client, Net::PacketReader& reader) {
		uint8_t shelfIndex = 0;
		uint8_t itemIndex = 0;
		if (!reader.Read(shelfIndex) || !reader.Read(itemIndex)
			|| shelfIndex >= MaxShelves || itemIndex >= MaxShelfItems)
			return false;

		Shelf& shelf = shelves[shelfIndex];
		if (!shelf.active || shelf.stacks[itemIndex].item == ItemNone)
			return false;

		ObjectState world = shelf.items[itemIndex];
		world.position += shelf.position;
		if (!HandsOnItem(client.player, world))
			return false;

		Inventory& inventory = client.player.inventory;
		SlotAddress slot;
		if (!FindEmptyCarrySlot(inventory, shelf.stacks[itemIndex], slot))
			return false;
		ItemStack* destination = ResolveSlot(inventory, slot);
		if (!destination)
			return false;

		*destination = std::move(shelf.stacks[itemIndex]);
		shelf.stacks[itemIndex] = ItemStack{};
		shelf.items[itemIndex] = DeadItemBody();
		return true;
	}

	void Server::HandleShelf(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		uint8_t action = 0;
		if (!reader.Read(action))
			return;
		client->lastSeen = std::chrono::steady_clock::now();

		const bool placing = action == static_cast<uint8_t>(Net::ShelfAction::Place);
		if (!placing && action != static_cast<uint8_t>(Net::ShelfAction::Move))
			return;
		if (!HasShelfTool(client->player.inventory))
			return;

		uint8_t index = 0;
		if (!placing && (!reader.Read(index) || index >= MaxShelves))
			return;

		olc::vf2d position;
		if (!reader.Read(position.x) || !reader.Read(position.y)
			|| !std::isfinite(position.x) || !std::isfinite(position.y)
			|| std::abs(position.x) > 2000.0f || position.y < -800.0f || position.y > 1000.0f)
			return;
		if ((position - DropHandOrigin(client->player, position)).mag() > ShelfPlaceRange)
			return;

		if (placing) {
			for (Shelf& shelf : shelves)
				if (!shelf.active) {
					CreateShelf(shelf, position);
					ConsumeShelfKit(client->player.inventory);
					return;
				}
			return;
		}

		Shelf& shelf = shelves[index];
		if (!shelf.active)
			return;
		if ((shelf.position - DropHandOrigin(client->player, shelf.position)).mag() > ShelfPlaceRange)
			return;
		shelf.position = position;
	}

	void Server::HandleShoot(const Net::Endpoint& endpoint, Net::PacketReader& reader) {
		ClientSlot* client = FindClient(endpoint);
		if (!client)
			return;

		uint8_t hotbar = 0;
		if (!reader.Read(hotbar))
			return;
		client->lastSeen = std::chrono::steady_clock::now();

		Inventory& inventory = client->player.inventory;
		if (hotbar >= HotbarSlots || !IsGun(inventory.hotbar[hotbar].item))
			return;
		if (!LoadedMagazine(inventory, hotbar))
			return;
		if (tick < client->nextShotTick)
			return;

		const GunBarrelRays barrels = ComputeGunBarrelRays(client->player, hotbar);
		if (barrels.count == 0)
			return;
		if (!ConsumeGunRound(inventory, hotbar))
			return;
		ApplyGunRecoil(client->player, hotbar);
		client->nextShotTick = tick + GunCooldownTicks;

		for (size_t barrel = 0; barrel < barrels.count; ++barrel) {
			const olc::vf2d origin = barrels.rays[barrel].origin;
			const olc::vf2d end = origin + barrels.rays[barrel].direction * GunRange;

			float wallFraction = 2.0f;
			for (const Terrain& terrain : TestTerrain()) {
				float fraction = 0.0f;
				if (SegmentRectFraction(origin, end, terrain, fraction))
					wallFraction = std::min(wallFraction, fraction);
			}

			size_t shelfHit = MaxShelves;
			float shelfFraction = 2.0f;
			for (size_t index = 0; index < shelves.size(); ++index) {
				if (!shelves[index].active)
					continue;
				float fraction = 0.0f;
				if (SegmentRectFraction(origin, end, ShelfBounds(shelves[index].position), fraction)
					&& fraction < shelfFraction) {
					shelfFraction = fraction;
					shelfHit = index;
				}
			}
			if (shelfHit >= shelves.size() || shelfFraction >= wallFraction)
				continue;

			bool occupied = false;
			for (const ItemStack& stack : shelves[shelfHit].stacks)
				if (stack.item != ItemNone) {
					occupied = true;
					break;
				}
			if (!occupied)
				DestroyShelf(shelves[shelfHit]);
		}
	}

	void Server::DestroyShelf(Shelf& shelf) {
		if (shelf.active)
			DestroyWorld(shelf.physics);
		shelf.active = false;
		shelf.position = {};
		shelf.items = {};
		shelf.stacks = {};
	}

	void Server::ClearShelves() {
		for (Shelf& shelf : shelves)
			DestroyShelf(shelf);
	}

	void Server::StepShelves() {
		for (Shelf& shelf : shelves) {
			if (!shelf.active || !b2World_IsValid(shelf.physics.world))
				continue;

			for (size_t i = 0; i < MaxShelfItems; ++i)
				if (shelf.stacks[i].item != ItemNone)
					EnsureBody(shelf.physics, shelf.items[i]);

			b2World_Step(shelf.physics.world, FixedDeltaTime, SolverSubSteps);

			for (size_t i = 0; i < MaxShelfItems; ++i) {
				if (shelf.stacks[i].item == ItemNone)
					continue;
				ObjectState& item = shelf.items[i];
				const b2BodyId body = EnsureBody(shelf.physics, item);
				SyncBody(item, body);
				if (!std::isfinite(item.position.x) || !std::isfinite(item.position.y)
					|| item.position.y > 20.0f || std::abs(item.position.x) > 80.0f) {
					ObjectState reset = CreateItemBody(shelf.stacks[i].item, { 0.0f, 0.0f });
					reset.position.y = -reset.radius - 1.0f;
					reset.velocity = { 0.0f, 40.0f };
					item = reset;
					SetBodyTransform(body, item);
				}
			}
			PruneBodies(shelf.physics);
		}
	}

	void Server::CreateShelf(Shelf& shelf, const olc::vf2d& position) {
		shelf.active = true;
		shelf.position = position;
		ResetWorld(shelf.physics);
		for (const ObjectState& board : ShelfBoards())
			shelf.physics.terrain.push_back(BuildBody(shelf.physics, board));
		for (size_t i = 0; i < MaxShelfItems; ++i) {
			shelf.items[i] = DeadItemBody();
			shelf.stacks[i] = ItemStack{};
		}
	}

	bool Server::HandleInventoryDrop(ClientSlot& client, Net::PacketReader& reader) {
		SlotAddress from;
		olc::vf2d position;
		if (!Net::ReadSlotAddress(reader, from) || !reader.Read(position.x) || !reader.Read(position.y))
			return false;
		if (!std::isfinite(position.x) || !std::isfinite(position.y)
			|| std::abs(position.x) > 2000.0f || position.y < -800.0f || position.y > 1000.0f)
			return false;

		const ItemStack* source = ResolveSlot(client.player.inventory, from);
		if (!source || !IsWorldItem(source->item))
			return false;
		const ItemId item = source->item;
		const ObjectState probe = CreateItemBody(item, position);
		if (probe.radius <= 0.0f)
			return false;

		// The drop has to stay on the thrower's side of anything between them and the cursor.
		std::array<ObjectState, TestBodyCount + MaxWorldItems> obstacles{};
		size_t obstacleCount = 0;
		for (const ObjectState& object : bodies)
			obstacles[obstacleCount++] = object;
		for (size_t other = 0; other < itemStacks.size(); ++other)
			if (itemStacks[other].item != ItemNone)
				obstacles[obstacleCount++] = items[other];
		const olc::vf2d hand = DropHandOrigin(client.player, position);
		const olc::vf2d drop = ClampDropPosition(hand, position, probe.radius,
			obstacles.data(), obstacleCount);

		for (Shelf& shelf : shelves) {
			if (!shelf.active)
				continue;
			olc::vf2d local;
			if (!ShelfLocalPlacement(drop, shelf.position, probe.radius, local))
				continue;
			if ((drop - hand).mag() > ShelfPlaceRange)
				return false;

			size_t shelfSlot = MaxShelfItems;
			for (size_t candidate = 0; candidate < MaxShelfItems; ++candidate)
				if (shelf.stacks[candidate].item == ItemNone) {
					shelfSlot = candidate;
					break;
				}
			if (shelfSlot >= MaxShelfItems)
				continue;

			ItemStack carried;
			if (!RemoveStack(client.player.inventory, from, carried))
				return false;
			ObjectState body = CreateItemBody(item, local);
			body.velocity = { 0.0f, 40.0f };
			shelf.items[shelfSlot] = body;
			shelf.stacks[shelfSlot] = std::move(carried);
			return true;
		}

		size_t slot = itemStacks.size();
		for (size_t candidate = 0; candidate < itemStacks.size(); ++candidate)
			if (itemStacks[candidate].item == ItemNone) {
				slot = candidate;
				break;
			}
		if (slot >= itemStacks.size())
			return false;

		ItemStack carried;
		if (!RemoveStack(client.player.inventory, from, carried))
			return false;
		ObjectState body = CreateItemBody(item, drop);
		body.velocity = { 0.0f, 40.0f };
		items[slot] = body;
		itemStacks[slot] = std::move(carried);
		return true;
	}

	void Server::SendWelcome(ClientSlot& client) {
		Net::PacketWriter packet(Net::MessageType::Welcome);
		packet.Write(Net::ProtocolVersion);
		Net::WriteRagdoll(packet, client.id, client.player);
		client.reliable.clear();
		if (packet.Size() <= Net::MaxDatagramSize) {
			socket.SendTo(client.endpoint, packet.Data().data(), packet.Size());
			return;
		}

		const uint32_t id = Net::AllocateMessageId(nextMessageId);
		const auto datagrams = Net::FragmentMessage(packet.Data(), id, true);
		if (datagrams.empty())
			return;

		PendingReliable message;
		message.id = id;
		message.datagrams = datagrams;
		message.nextRetry = std::chrono::steady_clock::now() + std::chrono::milliseconds(Net::ReliableRetryMilliseconds);
		message.attempts = 1;
		client.reliable.push_back(std::move(message));
		SendDatagrams(client.endpoint, client.reliable.back().datagrams);
	}
}
