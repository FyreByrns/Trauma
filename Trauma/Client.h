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
			float radius = PlayerRadius;
			float facing = 1.0f;
			Shape shape = Shape::Circle;
			olc::vf2d halfExtents = { 0.0f, 0.0f };
			uint8_t vertexCount = 0;
			std::array<olc::vf2d, MaxPolygonVertices> localVertices{};
			float angle = 0.0f;
		};

		struct WorldItem {
			Item item = Item::None;
			ObjectState body;
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
		const RagdollState& GetLocalRagdoll() const;
		std::array<olc::vf2d, PartCount> GetLocalPose(float alpha) const;
		bool GetRemotePose(uint8_t id, float alpha, std::array<olc::vf2d, PartCount>& pose, float& facing) const;
		const std::vector<ObjectView>& GetObjects() const;
		const std::array<WorldItem, MaxWorldItems>& GetWorldItems() const;
		void MoveItem(bool fromClothing, size_t from, bool toClothing, size_t to);
		void DropItem(bool fromClothing, size_t index, const olc::vf2d& position);
		bool GetPickupCandidate(size_t& index) const;
		void PickupItem(size_t index);
		bool GetPredictedBody(uint8_t id, float alpha, olc::vf2d& position, float& angle) const;

	private:
		struct SentInput {
			uint32_t sequence = 0;
			InputState input;
		};

		struct PredictedRagdoll {
			uint8_t id = 0;
			RagdollState state;
			std::array<olc::vf2d, PartCount> previous{};
		};

		struct SnapshotChunk {
			bool received = false;
			uint8_t id = 0;
			RagdollState ragdoll;
			std::array<ObjectState, TestBodyCount> objects;
			std::array<WorldItem, MaxWorldItems> items{};
		};

		struct PredictedBody {
			uint8_t id = 0;
			ObjectState state;
			olc::vf2d previousPosition;
			float previousAngle = 0.0f;
		};

		// Drops every piece of per-connection state.
		void ResetSession();
		void SendHello();
		void SendPacket(const Net::PacketWriter& packet) const;
		void PollPackets();
		void HandlePacket(Net::PacketReader& reader);
		void HandleWelcome(Net::PacketReader& reader);
		void HandleSnapshot(Net::PacketReader& reader);
		void ApplySnapshot(uint32_t serverTick, uint8_t count);
		void Reconcile(const RagdollState& authoritative, uint32_t lastInput);
		void StepPrediction(const InputState& input);
		void SmoothRemoteObjects(float fElapsedTime);

	private:
		Net::Socket socket;
		Net::Endpoint server;
		Status status = Status::Disconnected;
		uint8_t playerId = 0;
		RagdollState predicted;
		std::array<olc::vf2d, PartCount> previousLocal{};
		std::vector<PredictedRagdoll> predictedRagdolls;
		std::vector<PredictedBody> predictedBodies;
		std::array<SnapshotChunk, MaxPlayers + 1> snapshotChunks{};
		uint32_t assemblingTick = 0;
		uint8_t assemblingCount = 0;
		std::vector<ObjectView> players;
		std::array<WorldItem, MaxWorldItems> worldItems{};
		std::deque<SentInput> pending;
		uint32_t nextInputSequence = 1;
		float connectingTime = 0.0f;
		float helloCooldown = 0.0f;
		float snapshotTimer = 0.0f;
		uint32_t latestServerTick = 0;
	};
}
