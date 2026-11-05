#pragma once
#include "Physics.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace Trauma {

	using ItemId = uint16_t;
	constexpr ItemId ItemNone = 0;

	constexpr size_t HotbarSlots = 10;
	constexpr size_t ClothingSlots = 3;
	constexpr size_t MaxWorldItems = 16;
	constexpr uint8_t MaxShelfKits = 4;
	constexpr uint8_t MagazineCapacity = 8;
	constexpr uint32_t GunCooldownTicks = 8;
	constexpr float GunRange = 900.0f;
	constexpr size_t MaxItemContainerSlots = 16;
	constexpr size_t MaxItemContainerSections = 3;
	constexpr size_t MaxContainerDepth = 4;

	enum class ItemTag : uint32_t {
		None = 0,
		Clothing = 1u << 0,
		Head = 1u << 1,
		Body = 1u << 2,
		Feet = 1u << 3,
		Gun = 1u << 4,
		Magazine = 1u << 5,
		Container = 1u << 6,
		Backpack = 1u << 7,
		Shelf = 1u << 8,
		Tool = 1u << 9,
		Consumable = 1u << 10,
		Ammo = 1u << 11
	};

	using ItemTags = uint32_t;
	inline constexpr ItemTags AllItemTags =
		static_cast<ItemTags>(ItemTag::Clothing) | static_cast<ItemTags>(ItemTag::Head)
		| static_cast<ItemTags>(ItemTag::Body) | static_cast<ItemTags>(ItemTag::Feet)
		| static_cast<ItemTags>(ItemTag::Gun) | static_cast<ItemTags>(ItemTag::Magazine)
		| static_cast<ItemTags>(ItemTag::Container) | static_cast<ItemTags>(ItemTag::Backpack)
		| static_cast<ItemTags>(ItemTag::Shelf) | static_cast<ItemTags>(ItemTag::Tool)
		| static_cast<ItemTags>(ItemTag::Consumable) | static_cast<ItemTags>(ItemTag::Ammo);

	inline constexpr ItemTags TagMask(ItemTag tag) {
		return static_cast<ItemTags>(tag);
	}

	enum class SlotKind : uint8_t {
		Hotbar = 0,
		Head = 1,
		Body = 2,
		Feet = 3
	};

	inline size_t ClothingIndex(SlotKind kind) {
		switch (kind) {
			case SlotKind::Head: return 0;
			case SlotKind::Body: return 1;
			case SlotKind::Feet: return 2;
			default: return ClothingSlots;
		}
	}

	inline SlotKind ClothingKind(size_t index) {
		switch (index) {
			case 0: return SlotKind::Head;
			case 1: return SlotKind::Body;
			case 2: return SlotKind::Feet;
			default: return SlotKind::Hotbar;
		}
	}

	inline const char* SlotKindName(SlotKind kind) {
		switch (kind) {
			case SlotKind::Head: return "head";
			case SlotKind::Body: return "body";
			case SlotKind::Feet: return "feet";
			default: return "hotbar";
		}
	}

	struct ItemContainerSection {
		std::string name;
		uint8_t x = 0;
		uint8_t y = 0;
		uint8_t width = 0;
		uint8_t height = 0;
		ItemTags accept = 0;
	};

	struct ItemContainerDefinition {
		std::string title;
		uint8_t width = 0;
		uint8_t height = 0;
		ItemTags accept = 0;
		ItemTags acceptFilled = 0;
		std::array<ItemContainerSection, MaxItemContainerSections> sections{};
		uint8_t sectionCount = 0;

		size_t Capacity() const {
			const size_t slots = static_cast<size_t>(width) * height;
			return slots > MaxItemContainerSlots ? MaxItemContainerSlots : slots;
		}
	};

	enum class ItemBodyShape : uint8_t {
		None,
		Box,
		Circle
	};

	struct ItemBodyDefinition {
		ItemBodyShape shape = ItemBodyShape::None;
		olc::vf2d halfExtents = { 0.0f, 0.0f };
		float radius = 0.0f;
		float density = 0.5f;
	};

	struct ItemDefinition {
		std::string name;
		ItemTags tags = 0;
		olc::Pixel colour = olc::Pixel(60, 70, 80);
		ItemBodyDefinition body;
		uint8_t maxStack = 1;
		uint8_t maxQuantity = 1;
		uint8_t defaultQuantity = 0;
		ItemContainerDefinition container;
	};

	struct SlotAddress {
		SlotKind kind = SlotKind::Hotbar;
		uint8_t index = 0;
		uint8_t depth = 0;
		std::array<uint8_t, MaxContainerDepth> path{};
	};

	struct ItemContainer;

	struct ItemStack {
		ItemId item = ItemNone;
		uint8_t quantity = 0;
		std::unique_ptr<ItemContainer> container;

		ItemStack() = default;
		ItemStack(const ItemStack& other);
		ItemStack& operator=(const ItemStack& other);
		ItemStack(ItemStack&& other) noexcept;
		ItemStack& operator=(ItemStack&& other) noexcept;
		~ItemStack();
	};

	struct ItemContainer {
		std::array<ItemStack, MaxItemContainerSlots> slots{};
	};

	struct Inventory {
		std::array<ItemStack, HotbarSlots> hotbar{};
		std::array<ItemStack, ClothingSlots> clothing{};
		uint8_t selected = 0;
		uint8_t shelfKits = MaxShelfKits;
	};

	inline ItemStack::ItemStack(const ItemStack& other)
		: item(other.item), quantity(other.quantity) {
		if (other.container)
			container = std::make_unique<ItemContainer>(*other.container);
	}

	inline ItemStack& ItemStack::operator=(const ItemStack& other) {
		if (this == &other)
			return *this;
		item = other.item;
		quantity = other.quantity;
		container.reset();
		if (other.container)
			container = std::make_unique<ItemContainer>(*other.container);
		return *this;
	}

	inline ItemStack::ItemStack(ItemStack&& other) noexcept = default;
	inline ItemStack& ItemStack::operator=(ItemStack&& other) noexcept = default;
	inline ItemStack::~ItemStack() = default;

	bool LoadItemRegistry(const std::string& path, std::string& error);
	const ItemDefinition& ItemDefinitionFor(ItemId item);
	const ItemContainerDefinition& ItemContainerInfo(ItemId item);
	ItemId FindItemId(std::string_view name);
	ItemTags RootSlotTags(SlotKind kind);
	ItemTags TagFromName(std::string_view name);
	const char* TagName(ItemTag tag);
	ItemTags ContainerSlotTags(const ItemContainerDefinition& definition, size_t slot);
	const char* ItemName(ItemId item);
	olc::Pixel ItemColour(ItemId item);
	ItemTags ItemTagsFor(ItemId item);
	bool IsValidItemId(ItemId item);
	bool HasItemTag(ItemId item, ItemTag tag);
	size_t ItemContainerCapacity(ItemId item);
	uint8_t ItemMaxStack(ItemId item);
	uint8_t ItemMaxQuantity(ItemId item);
	uint8_t ItemDefaultQuantity(ItemId item);

	inline bool IsItem(ItemId item) {
		return item != ItemNone;
	}

	inline bool IsGun(ItemId item) {
		return HasItemTag(item, ItemTag::Gun);
	}

	inline bool IsMagazine(ItemId item) {
		return HasItemTag(item, ItemTag::Magazine);
	}

	inline bool IsShelf(ItemId item) {
		return HasItemTag(item, ItemTag::Shelf);
	}

	inline bool IsWorldItem(ItemId item) {
		return item != ItemNone && !IsShelf(item);
	}

	inline bool IsStackEmpty(const ItemStack& stack) {
		return stack.item == ItemNone;
	}

	inline bool IsItemContainerEmpty(const ItemStack& stack) {
		if (!stack.container)
			return true;
		for (const ItemStack& slot : stack.container->slots)
			if (slot.item != ItemNone || slot.container)
				return false;
		return true;
	}

	inline bool TagsOverlap(ItemTags a, ItemTags b) {
		return (a & b) != 0;
	}

	inline bool SameSlot(const SlotAddress& a, const SlotAddress& b) {
		if (a.kind != b.kind || a.index != b.index || a.depth != b.depth)
			return false;
		for (size_t i = 0; i < a.depth; ++i)
			if (a.path[i] != b.path[i])
				return false;
		return true;
	}

	inline bool ChildSlot(const SlotAddress& parent, size_t slotIndex, SlotAddress& child) {
		if (parent.depth >= MaxContainerDepth || slotIndex >= MaxItemContainerSlots)
			return false;
		child = parent;
		child.path[child.depth++] = static_cast<uint8_t>(slotIndex);
		return true;
	}

	inline SlotAddress HotbarAddress(size_t index) {
		SlotAddress address;
		address.kind = SlotKind::Hotbar;
		address.index = static_cast<uint8_t>(index);
		return address;
	}

	inline SlotAddress ClothingAddress(size_t index) {
		SlotAddress address;
		address.kind = ClothingKind(index);
		address.index = 0;
		return address;
	}

	inline ItemStack* RootStack(Inventory& inventory, const SlotAddress& address) {
		if (address.kind == SlotKind::Hotbar) {
			if (address.index >= inventory.hotbar.size())
				return nullptr;
			return &inventory.hotbar[address.index];
		}
		const size_t clothing = ClothingIndex(address.kind);
		if (clothing >= inventory.clothing.size())
			return nullptr;
		return &inventory.clothing[clothing];
	}

	inline const ItemStack* RootStack(const Inventory& inventory, const SlotAddress& address) {
		if (address.kind == SlotKind::Hotbar) {
			if (address.index >= inventory.hotbar.size())
				return nullptr;
			return &inventory.hotbar[address.index];
		}
		const size_t clothing = ClothingIndex(address.kind);
		if (clothing >= inventory.clothing.size())
			return nullptr;
		return &inventory.clothing[clothing];
	}

	inline ItemStack* ResolveSlot(Inventory& inventory, const SlotAddress& address) {
		ItemStack* stack = RootStack(inventory, address);
		if (!stack)
			return nullptr;
		for (size_t i = 0; i < address.depth; ++i) {
			if (!stack->container)
				return nullptr;
			if (address.path[i] >= ItemContainerCapacity(stack->item))
				return nullptr;
			stack = &stack->container->slots[address.path[i]];
		}
		return stack;
	}

	inline const ItemStack* ResolveSlot(const Inventory& inventory, const SlotAddress& address) {
		const ItemStack* stack = RootStack(inventory, address);
		if (!stack)
			return nullptr;
		for (size_t i = 0; i < address.depth; ++i) {
			if (!stack->container)
				return nullptr;
			if (address.path[i] >= ItemContainerCapacity(stack->item))
				return nullptr;
			stack = &stack->container->slots[address.path[i]];
		}
		return stack;
	}

	inline bool IsDescendantAddress(const SlotAddress& child, const SlotAddress& ancestor) {
		if (child.kind != ancestor.kind || child.index != ancestor.index || child.depth <= ancestor.depth)
			return false;
		for (size_t i = 0; i < ancestor.depth; ++i)
			if (child.path[i] != ancestor.path[i])
				return false;
		return true;
	}

	inline ItemStack MakeStack(ItemId item) {
		ItemStack stack;
		stack.item = item;
		if (item == ItemNone)
			return stack;
		stack.quantity = ItemDefaultQuantity(item);
		if (ItemContainerCapacity(item) > 0)
			stack.container = std::make_unique<ItemContainer>();
		return stack;
	}

	inline void EnsureItemContainer(ItemStack& stack) {
		if (stack.item != ItemNone && ItemContainerCapacity(stack.item) > 0 && !stack.container)
			stack.container = std::make_unique<ItemContainer>();
	}

	inline const ItemContainer* ItemContainerOf(const ItemStack& stack) {
		return stack.container.get();
	}

	inline ItemContainer* ItemContainerOf(ItemStack& stack) {
		return stack.container.get();
	}

	inline size_t ContainerTreeDepth(const ItemStack& stack) {
		if (stack.item == ItemNone || !stack.container)
			return 0;
		size_t childDepth = 0;
		bool hasChild = false;
		for (const ItemStack& child : stack.container->slots) {
			if (child.item == ItemNone)
				continue;
			hasChild = true;
			childDepth = std::max(childDepth, ContainerTreeDepth(child));
		}
		return hasChild ? 1 + childDepth : 0;
	}

	inline ItemTags SlotAcceptedTags(const Inventory& inventory, const SlotAddress& address) {
		if (address.depth == 0)
			return RootSlotTags(address.kind);
		const SlotAddress parent = [&] {
			SlotAddress value = address;
			--value.depth;
			return value;
		}();
		const ItemStack* owner = ResolveSlot(inventory, parent);
		if (!owner)
			return 0;
		const ItemContainerDefinition& definition = ItemContainerInfo(owner->item);
		const size_t slot = address.path[address.depth - 1];
		return ContainerSlotTags(definition, slot);
	}

	inline bool SlotAcceptsStack(const Inventory& inventory, const SlotAddress& address,
		const ItemStack& stack) {
		if (stack.item == ItemNone)
			return false;
		const ItemTags tags = ItemTagsFor(stack.item);
		if (!TagsOverlap(tags, SlotAcceptedTags(inventory, address)))
			return false;
		if (address.depth > 0 && !IsItemContainerEmpty(stack)) {
			const SlotAddress parent = [&] {
				SlotAddress value = address;
				--value.depth;
				return value;
			}();
			const ItemStack* owner = ResolveSlot(inventory, parent);
			if (!owner)
				return false;
			const ItemContainerDefinition& definition = ItemContainerInfo(owner->item);
			if (!TagsOverlap(tags, definition.acceptFilled))
				return false;
		}
		return true;
	}

	inline bool StackFitsDepth(const SlotAddress& address, const ItemStack& stack) {
		return address.depth + ContainerTreeDepth(stack) <= MaxContainerDepth;
	}

	inline bool CanMoveStackTo(const Inventory& inventory, const SlotAddress& source,
		const SlotAddress& target, const ItemStack& stack) {
		if (SameSlot(source, target))
			return false;
		if (IsDescendantAddress(target, source) || IsDescendantAddress(source, target))
			return false;
		if (!StackFitsDepth(target, stack))
			return false;
		return SlotAcceptsStack(inventory, target, stack);
	}

	inline bool MoveInventoryItem(Inventory& inventory, const SlotAddress& from,
		const SlotAddress& to) {
		if (SameSlot(from, to) || IsDescendantAddress(to, from) || IsDescendantAddress(from, to))
			return false;
		ItemStack* source = ResolveSlot(inventory, from);
		ItemStack* target = ResolveSlot(inventory, to);
		if (!source || !target || source->item == ItemNone)
			return false;
		if (!CanMoveStackTo(inventory, from, to, *source))
			return false;

		if (target->item == source->item && ItemMaxStack(source->item) > 1
			&& IsItemContainerEmpty(*source) && IsItemContainerEmpty(*target)) {
			const uint8_t maxStack = ItemMaxStack(source->item);
			if (target->quantity < maxStack && source->quantity > 0) {
				const uint8_t moved = std::min<uint8_t>(
					static_cast<uint8_t>(maxStack - target->quantity), source->quantity);
				target->quantity = static_cast<uint8_t>(target->quantity + moved);
				source->quantity = static_cast<uint8_t>(source->quantity - moved);
				if (source->quantity == 0)
					*source = ItemStack{};
				return true;
			}
		}

		if (target->item != ItemNone) {
			if (!CanMoveStackTo(inventory, to, from, *target))
				return false;
			std::swap(*source, *target);
			return true;
		}

		*target = std::move(*source);
		*source = ItemStack{};
		return true;
	}

	inline bool RemoveStack(Inventory& inventory, const SlotAddress& address, ItemStack& out) {
		ItemStack* stack = ResolveSlot(inventory, address);
		if (!stack || stack->item == ItemNone)
			return false;
		out = std::move(*stack);
		*stack = ItemStack{};
		return true;
	}

	inline size_t FindEmptyHotbarSlot(const Inventory& inventory, const ItemStack& stack) {
		for (size_t slot = 0; slot < inventory.hotbar.size(); ++slot)
			if (inventory.hotbar[slot].item == ItemNone
				&& SlotAcceptsStack(inventory, HotbarAddress(slot), stack))
				return slot;
		return inventory.hotbar.size();
	}

	inline bool HasShelfTool(const Inventory& inventory) {
		for (const ItemStack& stack : inventory.hotbar)
			if (IsShelf(stack.item))
				return true;
		return false;
	}

	inline void ConsumeShelfKit(Inventory& inventory) {
		if (inventory.shelfKits > 0)
			--inventory.shelfKits;
		if (inventory.shelfKits == 0)
			for (ItemStack& stack : inventory.hotbar)
				if (IsShelf(stack.item))
					stack = ItemStack{};
	}

	inline const ItemStack* LoadedMagazine(const Inventory& inventory, size_t gunSlot) {
		if (gunSlot >= inventory.hotbar.size() || !IsGun(inventory.hotbar[gunSlot].item))
			return nullptr;
		const ItemStack& gun = inventory.hotbar[gunSlot];
		if (!gun.container)
			return nullptr;
		for (const ItemStack& stack : gun.container->slots)
			if (IsMagazine(stack.item) && stack.quantity > 0)
				return &stack;
		return nullptr;
	}

	inline bool ConsumeGunRound(Inventory& inventory, size_t gunSlot) {
		if (gunSlot >= inventory.hotbar.size() || !IsGun(inventory.hotbar[gunSlot].item))
			return false;
		ItemStack& gun = inventory.hotbar[gunSlot];
		if (!gun.container)
			return false;
		for (ItemStack& magazine : gun.container->slots) {
			if (!IsMagazine(magazine.item) || magazine.quantity == 0)
				continue;
			--magazine.quantity;
			if (magazine.quantity == 0)
				magazine = ItemStack{};
			return true;
		}
		return false;
	}

	inline ObjectState DeadItemBody() {
		ObjectState body;
		body.inverseMass = 0.0f;
		body.radius = 0.0f;
		body.position = { -100000.0f, -100000.0f };
		return body;
	}

	inline ObjectState CreateItemBody(ItemId item, const olc::vf2d& position) {
		if (item == ItemNone)
			return DeadItemBody();
		const ItemBodyDefinition& body = ItemDefinitionFor(item).body;
		if (body.shape == ItemBodyShape::Box)
			return CreateBox(position, body.halfExtents, body.density);
		if (body.shape == ItemBodyShape::Circle)
			return CreateCircle(position, body.radius, body.density);
		return DeadItemBody();
	}

	inline Inventory CreateInventory() {
		Inventory inventory;
		inventory.hotbar[0] = MakeStack(FindItemId("wrench"));
		inventory.hotbar[1] = MakeStack(FindItemId("flashlight"));
		inventory.hotbar[2] = MakeStack(FindItemId("medkit"));
		inventory.hotbar[3] = MakeStack(FindItemId("oxygen tank"));
		inventory.hotbar[4] = MakeStack(FindItemId("shelf"));
		inventory.shelfKits = MaxShelfKits;
		inventory.hotbar[5] = MakeStack(FindItemId("gun"));
		inventory.hotbar[6] = MakeStack(FindItemId("magazine"));
		inventory.hotbar[7] = MakeStack(FindItemId("magazine"));
		inventory.hotbar[8] = MakeStack(FindItemId("backpack"));
		inventory.clothing[0] = MakeStack(FindItemId("helmet"));
		inventory.clothing[1] = MakeStack(FindItemId("diving suit"));
		inventory.clothing[2] = MakeStack(FindItemId("boots"));
		return inventory;
	}

}
