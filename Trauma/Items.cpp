#include "Items.h"
#include "Json.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace Trauma {
	namespace {
		struct NamedTag {
			const char* name;
			ItemTag tag;
		};

		constexpr NamedTag NamedTags[] = {
			{ "clothing", ItemTag::Clothing },
			{ "head", ItemTag::Head },
			{ "body", ItemTag::Body },
			{ "feet", ItemTag::Feet },
			{ "gun", ItemTag::Gun },
			{ "magazine", ItemTag::Magazine },
			{ "container", ItemTag::Container },
			{ "backpack", ItemTag::Backpack },
			{ "shelf", ItemTag::Shelf },
			{ "tool", ItemTag::Tool },
			{ "consumable", ItemTag::Consumable },
			{ "ammo", ItemTag::Ammo }
		};

		struct ItemCatalog {
			std::vector<ItemDefinition> items;
			std::unordered_map<std::string, ItemId> ids;
			std::array<ItemTags, 4> rootTags{};
			bool loaded = false;
		};

		ItemCatalog& Catalog() {
			static ItemCatalog catalog;
			return catalog;
		}

		const ItemDefinition& EmptyDefinition() {
			static const ItemDefinition empty;
			return empty;
		}

		bool Fail(std::string& error, const std::string& message) {
			error = message;
			return false;
		}

		bool ReadInt(const JsonValue* value, int& result) {
			if (!value || !value->IsNumber())
				return false;
			const double number = value->GetNumber();
			if (number < static_cast<double>(std::numeric_limits<int>::min())
				|| number > static_cast<double>(std::numeric_limits<int>::max())
				|| number != std::floor(number))
				return false;
			result = static_cast<int>(number);
			return true;
		}

		bool ReadUInt8(const JsonValue* value, uint8_t& result) {
			int number = 0;
			if (!ReadInt(value, number) || number < 0 || number > 255)
				return false;
			result = static_cast<uint8_t>(number);
			return true;
		}

		bool ReadFloat(const JsonValue* value, float& result) {
			if (!value || !value->IsNumber())
				return false;
			const double number = value->GetNumber();
			if (!std::isfinite(number) || number < -4096.0 || number > 4096.0)
				return false;
			result = static_cast<float>(number);
			return true;
		}

		ItemTags TagForName(std::string_view name) {
			for (const NamedTag& entry : NamedTags)
				if (name == entry.name)
					return TagMask(entry.tag);
			if (name == "any")
				return AllItemTags;
			return 0;
		}

		bool ParseTags(const JsonValue* value, ItemTags& result, std::string& error) {
			result = 0;
			if (!value)
				return true;
			if (!value->IsArray())
				return Fail(error, "expected an array of tag names");
			for (const JsonValue& item : value->Items()) {
				if (!item.IsString())
					return Fail(error, "tag names must be strings");
				const std::string name = item.GetString();
				const ItemTags parsed = TagForName(name);
				if (parsed == 0)
					return Fail(error, "unknown item tag '" + name + "'");
				result |= parsed;
			}
			return true;
		}

		bool ParseColour(const JsonValue* value, olc::Pixel& colour) {
			if (!value)
				return true;
			if (!value->IsArray() || value->Items().size() != 3)
				return false;
			int channels[3] = {};
			for (size_t i = 0; i < 3; ++i) {
				if (!ReadInt(&value->Items()[i], channels[i]) || channels[i] < 0 || channels[i] > 255)
					return false;
			}
			colour = olc::Pixel(static_cast<uint8_t>(channels[0]), static_cast<uint8_t>(channels[1]),
				static_cast<uint8_t>(channels[2]));
			return true;
		}

		bool ParseBody(const JsonValue* value, ItemBodyDefinition& body, std::string& error) {
			if (!value)
				return true;
			if (!value->IsObject())
				return Fail(error, "body must be an object");
			const JsonValue* shape = value->Find("shape");
			if (!shape || !shape->IsString())
				return Fail(error, "body.shape must be a string");
			const std::string shapeName = shape->GetString();
			if (shapeName == "box") {
				body.shape = ItemBodyShape::Box;
				const JsonValue* extents = value->Find("halfExtents");
				if (!extents || !extents->IsArray() || extents->Items().size() != 2)
					return Fail(error, "box body requires halfExtents [x, y]");
				if (!ReadFloat(&extents->Items()[0], body.halfExtents.x)
					|| !ReadFloat(&extents->Items()[1], body.halfExtents.y))
					return Fail(error, "invalid body.halfExtents");
			}
			else if (shapeName == "circle") {
				body.shape = ItemBodyShape::Circle;
				if (!ReadFloat(value->Find("radius"), body.radius))
					return Fail(error, "circle body requires radius");
			}
			else {
				return Fail(error, "body.shape must be 'box' or 'circle'");
			}
			if (const JsonValue* density = value->Find("density"); density) {
				if (!ReadFloat(density, body.density))
					return Fail(error, "invalid body.density");
			}
			if (body.density <= 0.0f)
				return Fail(error, "body.density must be positive");
			return true;
		}

		bool ParseContainer(const JsonValue* value, ItemContainerDefinition& container,
			std::string& error) {
			if (!value)
				return true;
			if (!value->IsObject())
				return Fail(error, "container must be an object");
			if (!ReadUInt8(value->Find("width"), container.width)
				|| !ReadUInt8(value->Find("height"), container.height))
				return Fail(error, "container requires integer width and height");
			if (container.width == 0 || container.height == 0)
				return Fail(error, "container width and height must be positive");
			if (static_cast<size_t>(container.width) * container.height > MaxItemContainerSlots)
				return Fail(error, "container is larger than the supported slot limit");
			if (!ParseTags(value->Find("accept"), container.accept, error))
				return false;
			if (!ParseTags(value->Find("acceptFilled"), container.acceptFilled, error))
				return false;
			if (const JsonValue* title = value->Find("title"); title) {
				if (!title->IsString())
					return Fail(error, "container.title must be a string");
				container.title = title->GetString();
			}
			if (container.title.empty())
				container.title = "Inventory";

			const JsonValue* sections = value->Find("sections");
			if (!sections)
				return true;
			if (!sections->IsArray())
				return Fail(error, "container.sections must be an array");
			if (sections->Items().size() > MaxItemContainerSections)
				return Fail(error, "too many container sections");
			for (const JsonValue& sectionValue : sections->Items()) {
				if (!sectionValue.IsObject())
					return Fail(error, "container section must be an object");
				ItemContainerSection section;
				if (const JsonValue* name = sectionValue.Find("name"); name) {
					if (!name->IsString())
						return Fail(error, "section.name must be a string");
					section.name = name->GetString();
				}
				if (!ReadUInt8(sectionValue.Find("x"), section.x)
					|| !ReadUInt8(sectionValue.Find("y"), section.y)
					|| !ReadUInt8(sectionValue.Find("width"), section.width)
					|| !ReadUInt8(sectionValue.Find("height"), section.height))
					return Fail(error, "section requires integer x, y, width, and height");
				if (section.width == 0 || section.height == 0)
					return Fail(error, "section width and height must be positive");
				if (section.x + section.width > container.width
					|| section.y + section.height > container.height)
					return Fail(error, "section lies outside the container");
				section.accept = container.accept;
				if (const JsonValue* accept = sectionValue.Find("accept"); accept) {
					if (!ParseTags(accept, section.accept, error))
						return false;
				}
				container.sections[container.sectionCount++] = std::move(section);
			}
			return true;
		}

		bool ParseItem(const JsonValue& value, ItemDefinition& item, std::string& error) {
			if (!value.IsObject())
				return Fail(error, "each item must be an object");
			const JsonValue* name = value.Find("name");
			if (!name || !name->IsString() || name->GetString().empty())
				return Fail(error, "item requires a non-empty name");
			item.name = name->GetString();
			if (!ParseTags(value.Find("tags"), item.tags, error))
				return false;
			if (const JsonValue* colour = value.Find("colour"); colour) {
				if (!ParseColour(colour, item.colour))
					return Fail(error, "item '" + item.name + "' has an invalid colour");
			}
			if (!ParseBody(value.Find("body"), item.body, error))
				return false;
			if (const JsonValue* maxStack = value.Find("maxStack"); maxStack) {
				if (!ReadUInt8(maxStack, item.maxStack) || item.maxStack == 0)
					return Fail(error, "item '" + item.name + "' has an invalid maxStack");
			}
			item.maxQuantity = item.maxStack;
			if (const JsonValue* maxQuantity = value.Find("maxQuantity"); maxQuantity) {
				if (!ReadUInt8(maxQuantity, item.maxQuantity) || item.maxQuantity == 0)
					return Fail(error, "item '" + item.name + "' has an invalid maxQuantity");
			}
			item.defaultQuantity = item.maxStack > 1 ? 1 : (item.maxQuantity > 1 ? item.maxQuantity : 0);
			if (const JsonValue* defaultQuantity = value.Find("defaultQuantity"); defaultQuantity) {
				if (!ReadUInt8(defaultQuantity, item.defaultQuantity))
					return Fail(error, "item '" + item.name + "' has an invalid defaultQuantity");
			}
			if (item.defaultQuantity > item.maxQuantity)
				return Fail(error, "item '" + item.name + "' defaultQuantity exceeds maxQuantity");
			if (!ParseContainer(value.Find("container"), item.container, error))
				return false;
			if (item.container.Capacity() > 0)
				item.tags |= TagMask(ItemTag::Container);
			return true;
		}

		bool ParseRootTags(const JsonValue& root, ItemCatalog& catalog, std::string& error) {
			catalog.rootTags = {
				AllItemTags,
				TagMask(ItemTag::Head),
				TagMask(ItemTag::Body),
				TagMask(ItemTag::Feet)
			};
			const JsonValue* slotTags = root.Find("slotTags");
			if (!slotTags)
				return true;
			if (!slotTags->IsObject())
				return Fail(error, "slotTags must be an object");
			const std::pair<const char*, size_t> keys[] = {
				{ "hotbar", 0 }, { "head", 1 }, { "body", 2 }, { "feet", 3 }
			};
			for (const auto& key : keys) {
				if (const JsonValue* tags = slotTags->Find(key.first); tags) {
					if (!ParseTags(tags, catalog.rootTags[key.second], error))
						return false;
				}
			}
			return true;
		}
	}

	bool LoadItemRegistry(const std::string& path, std::string& error) {
		error.clear();
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return Fail(error, "failed to open item data file '" + path + "'");
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		JsonValue root = JsonValue::Parse(text, error);
		if (!error.empty()) {
			error = "item data parse error: " + error;
			return false;
		}
		if (!root.IsObject())
			return Fail(error, "item data root must be an object");
		const JsonValue* items = root.Find("items");
		if (!items || !items->IsArray() || items->Items().empty())
			return Fail(error, "item data requires a non-empty items array");

		ItemCatalog catalog;
		catalog.items.emplace_back();
		for (const JsonValue& value : items->Items()) {
			ItemDefinition item;
			if (!ParseItem(value, item, error))
				return false;
			if (catalog.ids.find(item.name) != catalog.ids.end())
				return Fail(error, "duplicate item name '" + item.name + "'");
			if (catalog.items.size() > std::numeric_limits<ItemId>::max())
				return Fail(error, "too many items for the item id palette");
			const ItemId id = static_cast<ItemId>(catalog.items.size());
			catalog.ids.emplace(item.name, id);
			catalog.items.push_back(std::move(item));
		}
		if (!ParseRootTags(root, catalog, error))
			return false;
		catalog.loaded = true;
		Catalog() = std::move(catalog);
		return true;
	}

	const ItemDefinition& ItemDefinitionFor(ItemId item) {
		const ItemCatalog& catalog = Catalog();
		if (item == ItemNone || static_cast<size_t>(item) >= catalog.items.size())
			return EmptyDefinition();
		return catalog.items[item];
	}

	const ItemContainerDefinition& ItemContainerInfo(ItemId item) {
		return ItemDefinitionFor(item).container;
	}

	ItemId FindItemId(std::string_view name) {
		const ItemCatalog& catalog = Catalog();
		const auto found = catalog.ids.find(std::string(name));
		return found == catalog.ids.end() ? ItemNone : found->second;
	}

	ItemTags RootSlotTags(SlotKind kind) {
		const ItemCatalog& catalog = Catalog();
		switch (kind) {
			case SlotKind::Head: return catalog.rootTags[1];
			case SlotKind::Body: return catalog.rootTags[2];
			case SlotKind::Feet: return catalog.rootTags[3];
			default: return catalog.rootTags[0];
		}
	}

	ItemTags TagFromName(std::string_view name) {
		return TagForName(name);
	}

	const char* TagName(ItemTag tag) {
		for (const NamedTag& entry : NamedTags)
			if (entry.tag == tag)
				return entry.name;
		return "";
	}

	ItemTags ContainerSlotTags(const ItemContainerDefinition& definition, size_t slot) {
		if (definition.width == 0 || slot >= definition.Capacity())
			return 0;
		const size_t x = slot % definition.width;
		const size_t y = slot / definition.width;
		for (size_t i = 0; i < definition.sectionCount; ++i) {
			const ItemContainerSection& section = definition.sections[i];
			if (x >= section.x && x < static_cast<size_t>(section.x) + section.width
				&& y >= section.y && y < static_cast<size_t>(section.y) + section.height)
				return section.accept;
		}
		return definition.accept;
	}

	const char* ItemName(ItemId item) {
		return ItemDefinitionFor(item).name.c_str();
	}

	olc::Pixel ItemColour(ItemId item) {
		return ItemDefinitionFor(item).colour;
	}

	ItemTags ItemTagsFor(ItemId item) {
		return ItemDefinitionFor(item).tags;
	}

	bool IsValidItemId(ItemId item) {
		const ItemCatalog& catalog = Catalog();
		return !catalog.items.empty() && item != ItemNone && static_cast<size_t>(item) < catalog.items.size();
	}

	bool HasItemTag(ItemId item, ItemTag tag) {
		return TagsOverlap(ItemTagsFor(item), TagMask(tag));
	}

	size_t ItemContainerCapacity(ItemId item) {
		return ItemContainerInfo(item).Capacity();
	}

	uint8_t ItemMaxStack(ItemId item) {
		return ItemDefinitionFor(item).maxStack;
	}

	uint8_t ItemMaxQuantity(ItemId item) {
		return ItemDefinitionFor(item).maxQuantity;
	}

	uint8_t ItemDefaultQuantity(ItemId item) {
		return ItemDefinitionFor(item).defaultQuantity;
	}

}
