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
			ItemStack stack;
			ObjectState body;
		};

		struct ShelfItemView {
			ItemStack stack;
			ObjectState body;
			olc::vf2d previousPosition;
			float previousAngle = 0.0f;
		};

		struct ShelfView {
			bool active = false;
			olc::vf2d position;
			olc::vf2d previousPosition;
			std::array<ShelfItemView, MaxShelfItems> items{};
		};

		struct PickupTarget {
			bool shelf = false;
			uint8_t index = 0;
			uint8_t item = 0;
		};

		struct ShotTracer {
			olc::vf2d start;
			olc::vf2d end;
			float life = 0.0f;
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
		const std::array<ShelfView, MaxShelves>& GetShelves() const;
		const std::vector<ShotTracer>& GetShotTracers() const;
		int ShelfAt(const olc::vf2d& position) const;
		bool IsShelfDrop(const olc::vf2d& position) const;
		void PlaceShelf(const olc::vf2d& position);
		void MoveShelf(uint8_t index, const olc::vf2d& position);
		void FireGun(size_t hotbar, const olc::vf2d& target);
		void MoveItem(const SlotAddress& from, const SlotAddress& to);
		void DropItem(const SlotAddress& from, const olc::vf2d& position);
		bool GetPickupCandidate(PickupTarget& target) const;
		void PickupItem(const PickupTarget& target);
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
			std::array<ShelfView, MaxShelves> shelves{};
		};

		struct PredictedBody {
			uint8_t id = 0;
			ObjectState state;
			olc::vf2d previousPosition;
			float previousAngle = 0.0f;
		};

		struct PhysicsSnapshot {
			uint32_t sequence = 0;
			std::vector<uint8_t> image;
		};

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
		void CaptureSnapshot(uint32_t sequence);
		bool RestoreSnapshot(uint32_t sequence);
		void SmoothRemoteObjects(float fElapsedTime);

	private:
		Net::Socket socket;
		PhysicsWorld physics;
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
		std::array<ShelfView, MaxShelves> shelves{};
		std::vector<ShotTracer> shotTracers;
		std::deque<SentInput> pending;
		std::deque<PhysicsSnapshot> snapshots;
		uint32_t nextInputSequence = 1;
		float connectingTime = 0.0f;
		float helloCooldown = 0.0f;
		float snapshotTimer = 0.0f;
		uint32_t latestServerTick = 0;
		float shotCooldown = 0.0f;
	};
}
