#pragma once
#include "olcPixelGameEngine3.h"
#include "Network.h"
#include "Protocol.h"
#include "Simulation.h"
#include <deque>
#include <string>
#include <vector>

namespace Trauma {
	class Client {
	public:
		enum class Status {
			Disconnected,
			Connecting,
			Connected
		};

		struct ObjectView {
			uint8_t id = 0;
			olc::vf2d position;
			olc::vf2d target;
			float facing = 1.0f;
		};

		Client() = default;
		~Client();
		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		bool Connect(const std::string& host, uint16_t port);
		void Disconnect();
		void StepInput(const InputState& input);
		void Poll(float fElapsedTime);

		Status GetStatus() const;
		uint8_t GetPlayerId() const;
		const ObjectState& GetLocalState() const;
		const std::vector<ObjectView>& GetObjects() const;

	private:
		struct SentInput {
			uint32_t sequence = 0;
			InputState input;
		};

		void SendHello();
		void SendPacket(const Net::PacketWriter& packet) const;
		void PollPackets();
		void HandlePacket(Net::PacketReader& reader);
		void HandleWelcome(Net::PacketReader& reader);
		void HandleSnapshot(Net::PacketReader& reader);
		void Reconcile(const ObjectState& authoritative, uint32_t lastInput);
		void SmoothRemoteObjects(float fElapsedTime);

	private:
		Net::Socket socket;
		Net::Endpoint server;
		Status status = Status::Disconnected;
		uint8_t playerId = 0;
		ObjectState predicted;
		std::vector<ObjectView> players;
		std::deque<SentInput> pending;
		uint32_t nextInputSequence = 1;
		float connectingTime = 0.0f;
		float helloCooldown = 0.0f;
		float snapshotTimer = 0.0f;
		uint32_t latestServerTick = 0;
	};
}
