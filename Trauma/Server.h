#pragma once
#include "Network.h"
#include "Protocol.h"
#include "Simulation.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <thread>
#include <vector>

namespace Trauma {
	class Server {
	public:
		Server() = default;
		~Server();
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool Start(uint16_t port);
		bool Run(uint16_t port);
		void Stop();
		uint16_t GetPort() const;

	private:
		struct QueuedInput {
			uint32_t sequence = 0;
			InputState input;
		};

		struct ClientSlot {
			bool bActive = false;
			Net::Endpoint endpoint;
			uint8_t id = 0;
			RagdollState player;
			std::deque<QueuedInput> inputs;
			InputState lastInput;
			int missingInputTicks = 0;
			uint32_t highestReceived = 0;
			std::chrono::steady_clock::time_point lastSeen;
		};

		bool Open(uint16_t port);
		void Loop();
		void PollPackets();
		void StepSimulation();
		InputState SelectInput(ClientSlot& client);
		void BroadcastSnapshot();
		void SendSnapshotTo(const std::vector<ClientSlot*>& recipients, const Net::PacketWriter& packet);
		void DropTimedOutClients();
		ClientSlot* FindClient(const Net::Endpoint& endpoint);
		ClientSlot* AllocateClient(const Net::Endpoint& endpoint);
		void HandleHello(const Net::Endpoint& endpoint, Net::PacketReader& reader);
		void HandleInput(const Net::Endpoint& endpoint, Net::PacketReader& reader);
		void HandleInventory(const Net::Endpoint& endpoint, Net::PacketReader& reader);
		bool HandleInventoryMove(ClientSlot& client, Net::PacketReader& reader);
		bool HandleInventoryDrop(ClientSlot& client, Net::PacketReader& reader);
		bool HandleInventoryPickup(ClientSlot& client, Net::PacketReader& reader);
		void SendWelcome(const ClientSlot& client);

	private:
		Net::Socket socket;
	PhysicsWorld physics;
		std::array<ClientSlot, MaxPlayers> clients;
		std::array<ObjectState, TestBodyCount> bodies;
		std::array<ObjectState, MaxWorldItems> items;
		std::array<Item, MaxWorldItems> itemKinds{};
		std::thread worker;
		std::atomic<bool> running = false;
		uint32_t tick = 0;
	};
}
