#pragma once
#include "Simulation.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Trauma::Net {
	constexpr uint32_t ProtocolVersion = 22;
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
		Shelf = 6,
		Shoot = 7
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

	inline void WriteSlotAddress(PacketWriter& packet, const SlotAddress& address) {
		packet.Write(static_cast<uint8_t>(address.kind));
		packet.Write(address.index);
		packet.Write(address.depth);
		for (size_t i = 0; i < address.depth; ++i)
			packet.Write(address.path[i]);
	}

	inline bool ReadSlotAddress(PacketReader& reader, SlotAddress& address) {
		uint8_t kind = 0;
		uint8_t index = 0;
		uint8_t depth = 0;
		if (!reader.Read(kind) || !reader.Read(index) || !reader.Read(depth)
			|| kind > static_cast<uint8_t>(SlotKind::Feet) || depth > MaxContainerDepth)
			return false;
		if (kind == static_cast<uint8_t>(SlotKind::Hotbar)) {
			if (index >= HotbarSlots)
				return false;
		}
		else if (index != 0) {
			return false;
		}
		address = SlotAddress{};
		address.kind = static_cast<SlotKind>(kind);
		address.index = index;
		address.depth = depth;
		for (size_t i = 0; i < depth; ++i)
			if (!reader.Read(address.path[i]) || address.path[i] >= MaxItemContainerSlots)
				return false;
		return true;
	}

	inline void WriteItemStack(PacketWriter& packet, const ItemStack& stack) {
		packet.Write(stack.item);
		packet.Write(stack.quantity);
		const bool hasContents = stack.item != ItemNone && stack.container
			&& !IsItemContainerEmpty(stack);
		packet.Write(static_cast<uint8_t>(hasContents ? 1 : 0));
		if (!hasContents)
			return;
		for (size_t slot = 0; slot < ItemContainerCapacity(stack.item); ++slot)
			WriteItemStack(packet, stack.container->slots[slot]);
	}

	inline bool ReadItemStack(PacketReader& reader, ItemStack& stack, size_t depth = 0) {
		ItemId item = ItemNone;
		uint8_t quantity = 0;
		uint8_t hasContents = 0;
		if (!reader.Read(item) || !reader.Read(quantity) || !reader.Read(hasContents) || hasContents > 1)
			return false;
		if (item == ItemNone)
			return quantity == 0 && hasContents == 0;

		if (!IsValidItemId(item) || quantity > ItemMaxQuantity(item))
			return false;
		const size_t capacity = ItemContainerCapacity(item);
		if (capacity == 0 && hasContents != 0)
			return false;

		ItemStack parsed;
		parsed.item = item;
		parsed.quantity = quantity;
		if (capacity > 0) {
			if (hasContents && depth >= MaxContainerDepth)
				return false;
			parsed.container = std::make_unique<ItemContainer>();
			if (hasContents) {
				for (size_t slot = 0; slot < capacity; ++slot)
					if (!ReadItemStack(reader, parsed.container->slots[slot], depth + 1))
						return false;
			}
		}
		stack = std::move(parsed);
		return true;
	}

	inline bool ReadItemBody(PacketReader& reader, ItemStack& stack, ObjectState& body) {
		if (!ReadItemStack(reader, stack))
			return false;
		body = DeadItemBody();
		if (stack.item == ItemNone)
			return true;
		body = CreateItemBody(stack.item, { 0.0f, 0.0f });
		olc::vf2d position;
		olc::vf2d velocity;
		float angle = 0.0f;
		float angularVelocity = 0.0f;
		if (!reader.Read(position.x) || !reader.Read(position.y)
			|| !reader.Read(velocity.x) || !reader.Read(velocity.y)
			|| !reader.Read(angle) || !reader.Read(angularVelocity)
			|| !std::isfinite(position.x) || !std::isfinite(position.y)
			|| !std::isfinite(velocity.x) || !std::isfinite(velocity.y)
			|| !std::isfinite(angle) || !std::isfinite(angularVelocity))
			return false;
		body.position = position;
		body.velocity = velocity;
		body.angle = angle;
		body.angularVelocity = angularVelocity;
		return true;
	}

	inline void WriteInventoryMove(PacketWriter& packet, const SlotAddress& from,
		const SlotAddress& to) {
		packet.Write(static_cast<uint8_t>(InventoryAction::Move));
		WriteSlotAddress(packet, from);
		WriteSlotAddress(packet, to);
	}

	inline void WriteInventoryDrop(PacketWriter& packet, const SlotAddress& from,
		const olc::vf2d& position) {
		packet.Write(static_cast<uint8_t>(InventoryAction::Drop));
		WriteSlotAddress(packet, from);
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

	inline void WriteShoot(PacketWriter& packet, uint8_t hotbar, const olc::vf2d& target) {
		packet.Write(hotbar);
		packet.Write(target.x);
		packet.Write(target.y);
	}

	inline void WriteWorldItems(PacketWriter& packet, const std::array<ItemStack, MaxWorldItems>& stacks,
		const std::array<ObjectState, MaxWorldItems>& bodies) {
		packet.Write(static_cast<uint8_t>(MaxWorldItems));
		for (size_t i = 0; i < MaxWorldItems; ++i) {
			WriteItemStack(packet, stacks[i]);
			if (stacks[i].item == ItemNone)
				continue;
			packet.Write(bodies[i].position.x);
			packet.Write(bodies[i].position.y);
			packet.Write(bodies[i].velocity.x);
			packet.Write(bodies[i].velocity.y);
			packet.Write(bodies[i].angle);
			packet.Write(bodies[i].angularVelocity);
		}
	}

	inline void WriteWorldItem(PacketWriter& packet, const ItemStack& stack, const ObjectState& body) {
		WriteItemStack(packet, stack);
		if (stack.item == ItemNone)
			return;
		packet.Write(body.position.x);
		packet.Write(body.position.y);
		packet.Write(body.velocity.x);
		packet.Write(body.velocity.y);
		packet.Write(body.angle);
		packet.Write(body.angularVelocity);
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
		for (const ItemStack& stack : ragdoll.inventory.hotbar)
			WriteItemStack(packet, stack);
		for (const ItemStack& stack : ragdoll.inventory.clothing)
			WriteItemStack(packet, stack);
		packet.Write(ragdoll.inventory.shelfKits);
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
		ragdoll = CreateRagdoll({ 0.0f, 0.0f });
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

		for (size_t slot = 0; slot < ragdoll.inventory.hotbar.size(); ++slot) {
			ItemStack stack;
			if (!ReadItemStack(reader, stack))
				return false;
			if (stack.item != ItemNone
				&& !SlotAcceptsStack(ragdoll.inventory, HotbarAddress(slot), stack))
				return false;
			ragdoll.inventory.hotbar[slot] = std::move(stack);
		}
		for (size_t slot = 0; slot < ragdoll.inventory.clothing.size(); ++slot) {
			ItemStack stack;
			if (!ReadItemStack(reader, stack))
				return false;
			if (stack.item != ItemNone
				&& !SlotAcceptsStack(ragdoll.inventory, ClothingAddress(slot), stack))
				return false;
			ragdoll.inventory.clothing[slot] = std::move(stack);
		}
		if (!reader.Read(ragdoll.inventory.shelfKits) || ragdoll.inventory.shelfKits > MaxShelfKits
			|| (ragdoll.inventory.shelfKits == 0 && HasShelfTool(ragdoll.inventory)))
			return false;
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
