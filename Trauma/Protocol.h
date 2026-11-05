#pragma once
#include "Simulation.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Trauma::Net {
	constexpr uint32_t ProtocolVersion = 16;
	constexpr uint16_t DefaultPort = 42000;
	constexpr int SnapshotIntervalTicks = 3;
	constexpr float ConnectionTimeoutSeconds = 5.0f;
	constexpr uint8_t InputRedundancy = 4;
	constexpr uint8_t MaxInputEntries = 8;

	enum class MessageType : uint8_t {
		None = 0,
		Hello = 1,
		Welcome = 2,
		Input = 3,
		Snapshot = 4,
		Inventory = 5,
		Shelf = 6
	};

	enum class InventoryAction : uint8_t {
		Move = 0,
		Drop = 1,
		Pickup = 2,
		ShelfPickup = 3
	};

	enum class ShelfAction : uint8_t {
		Place = 0,
		Move = 1
	};

	class PacketWriter {
	public:
		explicit PacketWriter(MessageType type) {
			data.reserve(64);
			Write(type);
		}

		template <typename T>
		void Write(const T& value) {
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
			data.insert(data.end(), bytes, bytes + sizeof(T));
		}

		const std::vector<uint8_t>& Data() const {
			return data;
		}

		size_t Size() const {
			return data.size();
		}

	private:
		std::vector<uint8_t> data;
	};

	class PacketReader {
	public:
		PacketReader(const uint8_t* data, size_t size)
			: data(data), size(size) {}

		MessageType GetType() {
			MessageType type = MessageType::None;
			Read(type);
			return type;
		}

		template <typename T>
		bool Read(T& value) {
			if (offset + sizeof(T) > size)
				return false;

			std::memcpy(&value, data + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

	private:
		const uint8_t* data = nullptr;
		size_t size = 0;
		size_t offset = 0;
	};

	inline void WriteObjectState(PacketWriter& packet, uint8_t id, const ObjectState& player) {
		packet.Write(id);
		packet.Write(player.position.x);
		packet.Write(player.position.y);
		packet.Write(player.velocity.x);
		packet.Write(player.velocity.y);
		packet.Write(player.radius);
		packet.Write(player.inverseMass);
		packet.Write(static_cast<uint8_t>(player.shape));
		packet.Write(player.halfExtents.x);
		packet.Write(player.halfExtents.y);
		const int vertices = player.shape == Shape::Polygon
			? static_cast<int>(std::min<int>(player.vertexCount, MaxPolygonVertices)) : 0;
		packet.Write(static_cast<uint8_t>(vertices));
		for (int i = 0; i < vertices; ++i) {
			packet.Write(player.localVertices[i].x);
			packet.Write(player.localVertices[i].y);
		}
		packet.Write(player.angle);
		packet.Write(player.angularVelocity);
		packet.Write(static_cast<uint8_t>(player.grounded ? 1 : 0));
		packet.Write(player.lastInput);
	}

	inline bool ReadObjectState(PacketReader& reader, uint8_t& id, ObjectState& player) {
		uint8_t shape = 0;
		uint8_t grounded = 0;
		uint8_t vertices = 0;
		if (!reader.Read(id)
			|| !reader.Read(player.position.x)
			|| !reader.Read(player.position.y)
			|| !reader.Read(player.velocity.x)
			|| !reader.Read(player.velocity.y)
			|| !reader.Read(player.radius)
			|| !reader.Read(player.inverseMass)
			|| !reader.Read(shape)
			|| !reader.Read(player.halfExtents.x)
			|| !reader.Read(player.halfExtents.y)
			|| !reader.Read(vertices) || vertices > MaxPolygonVertices)
			return false;
		for (uint8_t i = 0; i < vertices; ++i)
			if (!reader.Read(player.localVertices[i].x) || !reader.Read(player.localVertices[i].y))
				return false;
		if (!reader.Read(player.angle)
			|| !reader.Read(player.angularVelocity)
			|| !reader.Read(grounded)
			|| !reader.Read(player.lastInput))
			return false;

		if (shape > static_cast<uint8_t>(Shape::Polygon)
			|| !std::isfinite(player.position.x) || !std::isfinite(player.position.y)
			|| !std::isfinite(player.velocity.x) || !std::isfinite(player.velocity.y)
			|| !std::isfinite(player.radius) || !std::isfinite(player.inverseMass)
			|| !std::isfinite(player.halfExtents.x) || !std::isfinite(player.halfExtents.y)
			|| player.halfExtents.x < 0.0f || player.halfExtents.y < 0.0f
			|| player.halfExtents.x > 4096.0f || player.halfExtents.y > 4096.0f
			|| !std::isfinite(player.angle) || !std::isfinite(player.angularVelocity))
			return false;

		player.shape = static_cast<Shape>(shape);
		player.vertexCount = 0;
		player.inverseInertia = 0.0f;
		if (player.shape == Shape::Polygon) {
			if (vertices < 3)
				return false;
			for (uint8_t i = 0; i < vertices; ++i) {
				const olc::vf2d& vertex = player.localVertices[i];
				if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)
					|| std::abs(vertex.x) > 4096.0f || std::abs(vertex.y) > 4096.0f)
					return false;
			}
			player.vertexCount = vertices;
			player.inverseInertia = PolygonInverseInertia(player.inverseMass, player.localVertices.data(), vertices);
		}
		else if (vertices != 0)
			return false;
		player.grounded = grounded != 0;
		return true;
	}

	inline void WriteInput(PacketWriter& packet, const InputState& input) {
		packet.Write(input.moveX);
		packet.Write(static_cast<uint8_t>(input.jump ? 1 : 0));
		packet.Write(static_cast<uint8_t>((input.reachLeft ? 1 : 0) | (input.reachRight ? 2 : 0)
			| (input.run ? 4 : 0) | (input.crouch ? 8 : 0) | (input.crawl ? 16 : 0)
			| (input.climb < 0.0f ? 32 : 0) | (input.climb > 0.0f ? 64 : 0) | (input.grab ? 128 : 0)));
		packet.Write(input.aim.x);
		packet.Write(input.aim.y);
		packet.Write(input.hotbar);
	}

	inline bool ReadInput(PacketReader& reader, InputState& input) {
		uint8_t jump = 0;
		uint8_t reach = 0;
		if (!reader.Read(input.moveX) || !reader.Read(jump) || !reader.Read(reach)
			|| !reader.Read(input.aim.x) || !reader.Read(input.aim.y)
			|| !reader.Read(input.hotbar) || input.hotbar >= HotbarSlots)
			return false;
		input.jump = jump != 0;
		input.reachLeft = (reach & 1) != 0;
		input.reachRight = (reach & 2) != 0;
		input.run = (reach & 4) != 0;
		input.crouch = (reach & 8) != 0;
		input.crawl = (reach & 16) != 0;
		input.climb = ((reach & 64) ? 1.0f : 0.0f) - ((reach & 32) ? 1.0f : 0.0f);
		input.grab = (reach & 128) != 0;
		return true;
	}

	inline void WriteInputEntry(PacketWriter& packet, uint32_t sequence, const InputState& input) {
		packet.Write(sequence);
		WriteInput(packet, input);
	}

	inline bool ReadInputEntry(PacketReader& reader, uint32_t& sequence, InputState& input) {
		return reader.Read(sequence) && ReadInput(reader, input);
	}

	inline void WriteInventoryMove(PacketWriter& packet, bool fromClothing, uint8_t from,
		bool toClothing, uint8_t to) {
		packet.Write(static_cast<uint8_t>(InventoryAction::Move));
		packet.Write(static_cast<uint8_t>(fromClothing ? 1 : 0));
		packet.Write(from);
		packet.Write(static_cast<uint8_t>(toClothing ? 1 : 0));
		packet.Write(to);
	}

	inline void WriteInventoryDrop(PacketWriter& packet, bool fromClothing, uint8_t from,
		const olc::vf2d& position) {
		packet.Write(static_cast<uint8_t>(InventoryAction::Drop));
		packet.Write(static_cast<uint8_t>(fromClothing ? 1 : 0));
		packet.Write(from);
		packet.Write(position.x);
		packet.Write(position.y);
	}

	inline void WriteInventoryPickup(PacketWriter& packet, uint8_t index) {
		packet.Write(static_cast<uint8_t>(InventoryAction::Pickup));
		packet.Write(index);
	}

	inline void WriteShelfPickup(PacketWriter& packet, uint8_t shelf, uint8_t item) {
		packet.Write(static_cast<uint8_t>(InventoryAction::ShelfPickup));
		packet.Write(shelf);
		packet.Write(item);
	}

	inline void WriteShelfPlace(PacketWriter& packet, const olc::vf2d& position) {
		packet.Write(static_cast<uint8_t>(ShelfAction::Place));
		packet.Write(position.x);
		packet.Write(position.y);
	}

	inline void WriteShelfMove(PacketWriter& packet, uint8_t shelf, const olc::vf2d& position) {
		packet.Write(static_cast<uint8_t>(ShelfAction::Move));
		packet.Write(shelf);
		packet.Write(position.x);
		packet.Write(position.y);
	}

	inline void WriteWorldItems(PacketWriter& packet, const std::array<Item, MaxWorldItems>& kinds,
		const std::array<ObjectState, MaxWorldItems>& bodies) {
		packet.Write(static_cast<uint8_t>(MaxWorldItems));
		for (size_t i = 0; i < MaxWorldItems; ++i) {
			packet.Write(static_cast<uint8_t>(kinds[i]));
			packet.Write(bodies[i].position.x);
			packet.Write(bodies[i].position.y);
			packet.Write(bodies[i].velocity.x);
			packet.Write(bodies[i].velocity.y);
			packet.Write(bodies[i].angle);
			packet.Write(bodies[i].angularVelocity);
		}
	}

	inline void WriteRagdoll(PacketWriter& packet, uint8_t id, const RagdollState& ragdoll) {
		packet.Write(id);
		packet.Write(ragdoll.parts[Torso].lastInput);
		packet.Write(ragdoll.facing);
		packet.Write(ragdoll.gaitPhase);
		packet.Write(ragdoll.crouchTurn);
		packet.Write(ragdoll.ladder);
		packet.Write(ragdoll.ladderDetachTime);
		WriteInput(packet, ragdoll.input);
		for (const HandGrip& grip : ragdoll.grips) {
			packet.Write(grip.target);
			packet.Write(grip.anchor.x);
			packet.Write(grip.anchor.y);
			packet.Write(grip.angleOffset);
		}
		for (const Item item : ragdoll.inventory.hotbar)
			packet.Write(static_cast<uint8_t>(item));
		for (const Item item : ragdoll.inventory.clothing)
			packet.Write(static_cast<uint8_t>(item));
		packet.Write(ragdoll.inventory.selected);
		for (const ObjectState& part : ragdoll.parts) {
			packet.Write(part.position.x);
			packet.Write(part.position.y);
			packet.Write(part.velocity.x);
			packet.Write(part.velocity.y);
			packet.Write(static_cast<uint8_t>(part.grounded ? 1 : 0));
		}
	}

	inline bool ReadRagdoll(PacketReader& reader, uint8_t& id, RagdollState& ragdoll) {
		ragdoll = CreateRagdoll({0.0f, 0.0f});
		if (!reader.Read(id) || !reader.Read(ragdoll.parts[Torso].lastInput)
			|| !reader.Read(ragdoll.facing) || !reader.Read(ragdoll.gaitPhase)
			|| !reader.Read(ragdoll.crouchTurn) || !std::isfinite(ragdoll.crouchTurn)
			|| ragdoll.crouchTurn < 0.0f
			|| !reader.Read(ragdoll.ladder) || ragdoll.ladder > TestLadders().size()
			|| !reader.Read(ragdoll.ladderDetachTime) || !ReadInput(reader, ragdoll.input))
			return false;
		for (HandGrip& grip : ragdoll.grips)
			if (!reader.Read(grip.target) || !reader.Read(grip.anchor.x) || !reader.Read(grip.anchor.y)
				|| !reader.Read(grip.angleOffset) || !std::isfinite(grip.angleOffset)
				|| grip.target > TestTerrain().size() + TestBodyCount + MaxWorldItems)
				return false;
		for (Item& item : ragdoll.inventory.hotbar) {
			uint8_t stored = 0;
			if (!reader.Read(stored) || stored > static_cast<uint8_t>(Item::Shelf))
				return false;
			item = static_cast<Item>(stored);
		}
		for (Item& item : ragdoll.inventory.clothing) {
			uint8_t stored = 0;
			if (!reader.Read(stored) || stored > static_cast<uint8_t>(Item::Boots)
				|| (stored != 0 && !IsClothing(static_cast<Item>(stored))))
				return false;
			item = static_cast<Item>(stored);
		}
		if (!reader.Read(ragdoll.inventory.selected) || ragdoll.inventory.selected >= HotbarSlots)
			return false;
		for (ObjectState& part : ragdoll.parts) {
			uint8_t grounded = 0;
			if (!reader.Read(part.position.x) || !reader.Read(part.position.y)
				|| !reader.Read(part.velocity.x) || !reader.Read(part.velocity.y) || !reader.Read(grounded))
				return false;
			part.grounded = grounded != 0;
		}
		return true;
	}
}
