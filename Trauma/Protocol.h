#pragma once
#include "Simulation.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Trauma::Net {
	constexpr uint32_t ProtocolVersion = 7;
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
		Snapshot = 4
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
		packet.Write(static_cast<uint8_t>(player.grounded ? 1 : 0));
		packet.Write(player.lastInput);
	}

	inline bool ReadObjectState(PacketReader& reader, uint8_t& id, ObjectState& player) {
		uint8_t grounded = 0;
		if (!reader.Read(id)
			|| !reader.Read(player.position.x)
			|| !reader.Read(player.position.y)
			|| !reader.Read(player.velocity.x)
			|| !reader.Read(player.velocity.y)
			|| !reader.Read(player.radius)
			|| !reader.Read(player.inverseMass)
			|| !reader.Read(grounded)
			|| !reader.Read(player.lastInput))
			return false;

		player.grounded = grounded != 0;
		return true;
	}

	inline void WriteInput(PacketWriter& packet, const InputState& input) {
		packet.Write(input.moveX);
		packet.Write(static_cast<uint8_t>(input.jump ? 1 : 0));
		packet.Write(static_cast<uint8_t>((input.reachLeft ? 1 : 0) | (input.reachRight ? 2 : 0)
			| (input.run ? 4 : 0) | (input.crouch ? 8 : 0) | (input.crawl ? 16 : 0)
			| (input.climb < 0.0f ? 32 : 0) | (input.climb > 0.0f ? 64 : 0)));
		packet.Write(input.aim.x);
		packet.Write(input.aim.y);
	}

	inline bool ReadInput(PacketReader& reader, InputState& input) {
		uint8_t jump = 0;
		uint8_t reach = 0;
		if (!reader.Read(input.moveX) || !reader.Read(jump) || !reader.Read(reach)
			|| !reader.Read(input.aim.x) || !reader.Read(input.aim.y))
			return false;
		input.jump = jump != 0;
		input.reachLeft = (reach & 1) != 0;
		input.reachRight = (reach & 2) != 0;
		input.run = (reach & 4) != 0;
		input.crouch = (reach & 8) != 0;
		input.crawl = (reach & 16) != 0;
		input.climb = ((reach & 64) ? 1.0f : 0.0f) - ((reach & 32) ? 1.0f : 0.0f);
		return true;
	}

	inline void WriteInputEntry(PacketWriter& packet, uint32_t sequence, const InputState& input) {
		packet.Write(sequence);
		WriteInput(packet, input);
	}

	inline bool ReadInputEntry(PacketReader& reader, uint32_t& sequence, InputState& input) {
		return reader.Read(sequence) && ReadInput(reader, input);
	}

	inline void WriteRagdoll(PacketWriter& packet, uint8_t id, const RagdollState& ragdoll) {
		packet.Write(id);
		packet.Write(ragdoll.parts[Torso].lastInput);
		packet.Write(ragdoll.facing);
		packet.Write(ragdoll.gaitPhase);
		packet.Write(ragdoll.ladder);
		packet.Write(ragdoll.ladderDetachTime);
		WriteInput(packet, ragdoll.input);
		for (const HandGrip& grip : ragdoll.grips) {
			packet.Write(grip.target);
			packet.Write(grip.anchor.x);
			packet.Write(grip.anchor.y);
		}
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
			|| !reader.Read(ragdoll.ladder) || ragdoll.ladder > TestLadders().size()
			|| !reader.Read(ragdoll.ladderDetachTime) || !ReadInput(reader, ragdoll.input))
			return false;
		for (HandGrip& grip : ragdoll.grips)
			if (!reader.Read(grip.target) || !reader.Read(grip.anchor.x) || !reader.Read(grip.anchor.y)
				|| grip.target > TestTerrain().size() + TestBodyCount)
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
