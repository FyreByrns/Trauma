#pragma once
#include "Physics.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
	constexpr size_t MaxGunBarrels = 4;
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
		Ammo = 1u << 11,
		Large = 1u << 12,
		Belt = 1u << 13
	};

	using ItemTags = uint32_t;
	inline constexpr ItemTags AllItemTags =
		static_cast<ItemTags>(ItemTag::Clothing) | static_cast<ItemTags>(ItemTag::Head)
		| static_cast<ItemTags>(ItemTag::Body) | static_cast<ItemTags>(ItemTag::Feet)
		| static_cast<ItemTags>(ItemTag::Gun) | static_cast<ItemTags>(ItemTag::Magazine)
		| static_cast<ItemTags>(ItemTag::Container) | static_cast<ItemTags>(ItemTag::Backpack)
		| static_cast<ItemTags>(ItemTag::Shelf) | static_cast<ItemTags>(ItemTag::Tool)
		| static_cast<ItemTags>(ItemTag::Consumable) | static_cast<ItemTags>(ItemTag::Ammo)
		| static_cast<ItemTags>(ItemTag::Large) | static_cast<ItemTags>(ItemTag::Belt);

	inline constexpr ItemTags TagMask(ItemTag tag) {
		return static_cast<ItemTags>(tag);
	}

	enum class SlotKind : uint8_t {
		Hotbar = 0,
		Head = 1,
		Body = 2,
		Feet = 3,
		Back = 4,
		Belt = 5
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
			case SlotKind::Back: return "back";
			case SlotKind::Belt: return "belt";
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

	enum class HandControl : uint8_t {
		None,
		Left,
		Right,
		Any
	};

	struct ItemBodyDefinition {
		ItemBodyShape shape = ItemBodyShape::None;
		olc::vf2d halfExtents = { 0.0f, 0.0f };
		float radius = 0.0f;
		float density = 0.5f;
	};

	struct GunBarrelDefinition {
		olc::vf2d position = { 0.0f, 0.0f };
		float angle = 0.0f;
	};

	struct GunRecoilDefinition {
		float amount = 0.0f;
		olc::vf2d direction = { 0.0f, -1.0f };
		float lowReadyRecovery = 4.0f;
		float aimingRecovery = 18.0f;
	};

	struct GunPoseDefinition {
		olc::vf2d lowReadyHand = { 10.0f, 16.0f };
		olc::vf2d aimingHand = { 20.0f, 0.0f };
		std::array<GunBarrelDefinition, MaxGunBarrels> barrels{};
		uint8_t barrelCount = 0;
		GunRecoilDefinition recoil;
	};

	struct ItemDefinition {
		std::string name;
		ItemTags tags = 0;
		olc::Pixel colour = olc::Pixel(60, 70, 80);
		ItemBodyDefinition body;
		GunPoseDefinition gunPose;
		HandControl hand = HandControl::None;
		uint8_t handCount = 1;
		uint8_t maxStack = 1;
		uint8_t maxCharge = 0;
		uint8_t defaultCharge = 0;
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
		uint8_t charge = 0;
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
		ItemStack back;
		ItemStack belt;
		uint8_t selected = 0;
	};

	inline ItemStack::ItemStack(const ItemStack& other)
		: item(other.item), quantity(other.quantity), charge(other.charge) {
		if (other.container)
			container = std::make_unique<ItemContainer>(*other.container);
	}

	inline ItemStack& ItemStack::operator=(const ItemStack& other) {
		if (this == &other)
			return *this;
		item = other.item;
		quantity = other.quantity;
		charge = other.charge;
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
	uint8_t ItemMaxCharge(ItemId item);
	uint8_t ItemDefaultCharge(ItemId item);
	HandControl ItemHandControl(ItemId item);

	// Searches the hotbar, clothing, and every nested item container.
	bool FindInventoryItem(const Inventory& inventory,
		const std::function<bool(const ItemStack&)>& predicate, SlotAddress& found);
	bool FindInventoryItem(const Inventory& inventory, ItemId item, SlotAddress& found);

	inline bool IsItem(ItemId item) {
		return item != ItemNone;
	}

	inline uint8_t ItemHandCount(ItemId item) {
		const ItemDefinition& definition = ItemDefinitionFor(item);
		if (definition.hand == HandControl::None)
			return 0;
		return definition.handCount;
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

	inline SlotAddress BackAddress() {
		SlotAddress address;
		address.kind = SlotKind::Back;
		return address;
	}

	inline SlotAddress BeltAddress() {
		SlotAddress address;
		address.kind = SlotKind::Belt;
		return address;
	}

	inline ItemStack* RootStack(Inventory& inventory, const SlotAddress& address) {
		switch (address.kind) {
			case SlotKind::Hotbar:
				if (address.index >= inventory.hotbar.size())
					return nullptr;
				return &inventory.hotbar[address.index];
			case SlotKind::Back:
				return address.index == 0 ? &inventory.back : nullptr;
			case SlotKind::Belt:
				return address.index == 0 ? &inventory.belt : nullptr;
			default:
				break;
		}
		const size_t clothing = ClothingIndex(address.kind);
		if (clothing >= inventory.clothing.size())
			return nullptr;
		return &inventory.clothing[clothing];
	}

	inline const ItemStack* RootStack(const Inventory& inventory, const SlotAddress& address) {
		switch (address.kind) {
			case SlotKind::Hotbar:
				if (address.index >= inventory.hotbar.size())
					return nullptr;
				return &inventory.hotbar[address.index];
			case SlotKind::Back:
				return address.index == 0 ? &inventory.back : nullptr;
			case SlotKind::Belt:
				return address.index == 0 ? &inventory.belt : nullptr;
			default:
				break;
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
		stack.quantity = 1;
		stack.charge = ItemDefaultCharge(item);
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
		if (address.depth == 0 && address.kind != SlotKind::Back
			&& TagsOverlap(tags, TagMask(ItemTag::Large)))
			return false;
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
			&& source->charge == target->charge
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

	inline bool FindEmptyCarrySlot(const Inventory& inventory, const ItemStack& stack, SlotAddress& found) {
		if (inventory.belt.item == ItemNone) {
			const SlotAddress address = BeltAddress();
			if (SlotAcceptsStack(inventory, address, stack)) {
				found = address;
				return true;
			}
		}
		for (size_t slot = 0; slot < inventory.hotbar.size(); ++slot) {
			if (inventory.hotbar[slot].item != ItemNone)
				continue;
			const SlotAddress address = HotbarAddress(slot);
			if (!SlotAcceptsStack(inventory, address, stack))
				continue;
			found = address;
			return true;
		}
		if (inventory.back.item == ItemNone) {
			const SlotAddress address = BackAddress();
			if (SlotAcceptsStack(inventory, address, stack)) {
				found = address;
				return true;
			}
		}
		return false;
	}

	inline bool HasShelfTool(const Inventory& inventory) {
		for (const ItemStack& stack : inventory.hotbar)
			if (IsShelf(stack.item) && stack.quantity > 0)
				return true;
		return false;
	}

	inline void ConsumeShelfKit(Inventory& inventory) {
		for (ItemStack& stack : inventory.hotbar) {
			if (!IsShelf(stack.item) || stack.quantity == 0)
				continue;
			--stack.quantity;
			if (stack.quantity == 0)
				stack = ItemStack{};
			return;
		}
	}

	inline const ItemStack* GunMagazine(const Inventory& inventory, size_t gunSlot) {
		if (gunSlot >= inventory.hotbar.size() || !IsGun(inventory.hotbar[gunSlot].item))
			return nullptr;
		const ItemStack& gun = inventory.hotbar[gunSlot];
		if (!gun.container)
			return nullptr;
		for (const ItemStack& stack : gun.container->slots)
			if (IsMagazine(stack.item))
				return &stack;
		return nullptr;
	}

	inline const ItemStack* LoadedMagazine(const Inventory& inventory, size_t gunSlot) {
		const ItemStack* magazine = GunMagazine(inventory, gunSlot);
		return magazine && magazine->charge > 0 ? magazine : nullptr;
	}

	inline bool ConsumeGunRound(Inventory& inventory, size_t gunSlot) {
		if (gunSlot >= inventory.hotbar.size() || !IsGun(inventory.hotbar[gunSlot].item))
			return false;
		ItemStack& gun = inventory.hotbar[gunSlot];
		if (!gun.container)
			return false;
		for (ItemStack& magazine : gun.container->slots) {
			if (!IsMagazine(magazine.item) || magazine.charge == 0)
				continue;
			--magazine.charge;
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
		if (inventory.hotbar[4].item != ItemNone)
			inventory.hotbar[4].quantity = ItemMaxStack(inventory.hotbar[4].item);
		inventory.hotbar[5] = MakeStack(FindItemId("gun"));
		inventory.hotbar[6] = MakeStack(FindItemId("magazine"));
		inventory.hotbar[7] = MakeStack(FindItemId("magazine"));
		inventory.back = MakeStack(FindItemId("backpack"));
		inventory.belt = MakeStack(FindItemId("belt"));
		inventory.clothing[0] = MakeStack(FindItemId("helmet"));
		inventory.clothing[1] = MakeStack(FindItemId("diving suit"));
		inventory.clothing[2] = MakeStack(FindItemId("boots"));
		return inventory;
	}

}
