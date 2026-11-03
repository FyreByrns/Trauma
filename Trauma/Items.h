#pragma once
#include "Physics.h"
#include <array>
#include <cstdint>
#include <vector>

namespace Trauma {

	enum class Item : uint8_t {
		None, Wrench, Flashlight, Medkit, OxygenTank, Suit, Helmet, Boots
	};

	enum class ClothingSlot : size_t {
		Head, Body, Feet, Count
	};

	constexpr size_t HotbarSlots = 10;
	constexpr size_t MaxWorldItems = 16;

	struct Inventory {
		std::array<Item, HotbarSlots> hotbar{};
		std::array<Item, static_cast<size_t>(ClothingSlot::Count)> clothing{};
		uint8_t selected = 0;
	};

	inline bool IsClothing(Item item) {
		return item == Item::Suit || item == Item::Helmet || item == Item::Boots;
	}

	inline const char* ItemName(Item item) {
		switch (item) {
			case Item::Wrench: return "wrench";
			case Item::Flashlight: return "flashlight";
			case Item::Medkit: return "medkit";
			case Item::OxygenTank: return "oxygen tank";
			case Item::Suit: return "diving suit";
			case Item::Helmet: return "helmet";
			case Item::Boots: return "boots";
			default: return "";
		}
	}

	inline const char* ClothingName(ClothingSlot slot) {
		switch (slot) {
			case ClothingSlot::Head: return "head";
			case ClothingSlot::Body: return "body";
			case ClothingSlot::Feet: return "feet";
			default: return "";
		}
	}

	inline olc::Pixel ItemColour(Item item) {
		switch (item) {
			case Item::Wrench: return olc::Pixel(150, 160, 170);
			case Item::Flashlight: return olc::Pixel(235, 205, 110);
			case Item::Medkit: return olc::Pixel(215, 90, 90);
			case Item::OxygenTank: return olc::Pixel(110, 190, 200);
			case Item::Suit: return olc::Pixel(70, 140, 120);
			case Item::Helmet: return olc::Pixel(120, 160, 200);
			case Item::Boots: return olc::Pixel(120, 100, 80);
			default: return olc::Pixel(60, 70, 80);
		}
	}

	inline Inventory CreateInventory() {
		Inventory inventory;
		inventory.hotbar[0] = Item::Wrench;
		inventory.hotbar[1] = Item::Flashlight;
		inventory.hotbar[2] = Item::Medkit;
		inventory.hotbar[3] = Item::OxygenTank;
		inventory.clothing[static_cast<size_t>(ClothingSlot::Head)] = Item::Helmet;
		inventory.clothing[static_cast<size_t>(ClothingSlot::Body)] = Item::Suit;
		inventory.clothing[static_cast<size_t>(ClothingSlot::Feet)] = Item::Boots;
		return inventory;
	}

	inline bool MoveInventoryItem(Inventory& inventory, bool fromClothing, size_t from,
		bool toClothing, size_t to) {
		if (from >= (fromClothing ? inventory.clothing.size() : inventory.hotbar.size())
			|| to >= (toClothing ? inventory.clothing.size() : inventory.hotbar.size()))
			return false;
		Item& source = fromClothing ? inventory.clothing[from] : inventory.hotbar[from];
		Item& target = toClothing ? inventory.clothing[to] : inventory.hotbar[to];
		if (source == Item::None)
			return false;
		const Item moving = source;
		const Item displaced = target;
		if (toClothing && !IsClothing(moving))
			return false;
		if (fromClothing && displaced != Item::None && !IsClothing(displaced))
			return false;
		source = displaced;
		target = moving;
		return true;
	}

	inline Item TakeInventoryItem(Inventory& inventory, bool clothing, size_t index) {
		if (clothing) {
			if (index >= inventory.clothing.size())
				return Item::None;
			const Item item = inventory.clothing[index];
			inventory.clothing[index] = Item::None;
			return item;
		}
		if (index >= inventory.hotbar.size())
			return Item::None;
		const Item item = inventory.hotbar[index];
		inventory.hotbar[index] = Item::None;
		return item;
	}

	inline ObjectState DeadItemBody() {
		ObjectState body;
		body.inverseMass = 0.0f;
		body.radius = 0.0f;
		body.position = { -100000.0f, -100000.0f };
		return body;
	}

	inline ObjectState CreateItemBody(Item item, const olc::vf2d& position) {
		switch (item) {
			case Item::Wrench: return CreateBox(position, { 9.0f, 2.5f }, 0.6f);
			case Item::Flashlight: return CreateBox(position, { 7.0f, 3.0f }, 0.6f);
			case Item::Medkit: return CreateBox(position, { 7.0f, 6.0f }, 0.5f);
			case Item::OxygenTank: return CreateCircle(position, 6.0f, 0.5f);
			case Item::Suit: return CreateBox(position, { 8.0f, 9.0f }, 0.6f);
			case Item::Helmet: return CreateBox(position, { 6.0f, 6.0f }, 0.5f);
			case Item::Boots: return CreateBox(position, { 6.0f, 4.0f }, 0.5f);
			default: return DeadItemBody();
		}
	}

}
