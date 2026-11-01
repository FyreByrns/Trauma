#pragma once
#include "Simulation.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Trauma::Net {
	constexpr uint32_t ProtocolVersion = 5;
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

	inline void WritePlayerState(PacketWriter& packet, uint8_t id, const ObjectState& player) {
		packet.Write(id);
		packet.Write(player.position.x);
		packet.Write(player.position.y);
		packet.Write(player.velocity.x);
		packet.Write(player.velocity.y);
		packet.Write(static_cast<uint8_t>(player.grounded ? 1 : 0));
		packet.Write(player.lastInput);
	}

	inline bool ReadPlayerState(PacketReader& reader, uint8_t& id, ObjectState& player) {
		uint8_t grounded = 0;
		if (!reader.Read(id)
			|| !reader.Read(player.position.x)
			|| !reader.Read(player.position.y)
			|| !reader.Read(player.velocity.x)
			|| !reader.Read(player.velocity.y)
			|| !reader.Read(grounded)
			|| !reader.Read(player.lastInput))
			return false;

		player.grounded = grounded != 0;
		return true;
	}

	inline void WriteInputEntry(PacketWriter& packet, uint32_t sequence, const InputState& input) {
		packet.Write(sequence);
		packet.Write(input.moveX);
		packet.Write(static_cast<uint8_t>(input.jump ? 1 : 0));
	}

	inline bool ReadInputEntry(PacketReader& reader, uint32_t& sequence, InputState& input) {
		uint8_t jump = 0;
		if (!reader.Read(sequence) || !reader.Read(input.moveX) || !reader.Read(jump))
			return false;

		input.jump = jump != 0;
		return true;
	}
}
