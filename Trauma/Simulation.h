#pragma once
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Geometry2D.h"
#include "IK.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Trauma {
	constexpr int MaxPlayers = 20;
	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float Gravity = 1600.0f;
	constexpr float MoveSpeed = 220.0f;
	constexpr float JumpSpeed = 520.0f;
	constexpr float PlayerRadius = 10.0f;
	constexpr size_t TestBodyCount = 6;

	constexpr float ContactFriction = 3.0f;
	constexpr float RestitutionThreshold = 70.0f;
	constexpr float Restitution = 0.15f;
	constexpr float ContactSlop = 0.05f;
	constexpr float ContactCorrection = 0.5f;
	constexpr float ContactMargin = ContactSlop;
	constexpr float AngularDamping = 1.5f;
	constexpr float GroundedSpinDamping = 8.0f;
	constexpr float AxisSnap = 0.03f;
	constexpr float MaxAngularSpeed = 25.0f;
	constexpr float RestSpeed = 4.0f;
	constexpr float RestDamping = 0.5f;
	constexpr float FootingSlipLimit = 2.0f;
	constexpr float SleepSpeed = 3.0f;
	constexpr float SleepDelay = 0.6f;
	constexpr float WakeSpeed = 6.0f;
	constexpr float GripRotationStiffness = 14.0f;
	constexpr float GripRotationDamping = 0.5f;
	constexpr float MaxGripSpin = 25.0f;

	struct InputState {
		float moveX = 0.0f;
		bool jump = false;
		bool reachLeft = false;
		bool reachRight = false;
		olc::vf2d aim = { 1.0f, 0.0f };
		bool run = false;
		bool crouch = false;
		bool crawl = false;
		float climb = 0.0f;
		uint8_t hotbar = 0;
	};

	enum class MovementState {
		Walking,
		Running,
		Crouching,
		Crawling
	};

	inline MovementState GetMovementState(const InputState& input) {
		if (input.crawl)
			return MovementState::Crawling;
		if (input.crouch)
			return MovementState::Crouching;
		return input.run ? MovementState::Running : MovementState::Walking;
	}

	inline float MovementSpeed(MovementState state) {
		switch (state) {
			case MovementState::Running: return MoveSpeed;
			case MovementState::Crouching: return 75.0f;
			case MovementState::Crawling: return 45.0f;
			case MovementState::Walking: return 135.0f;
		}
		return 0.0f;
	}

	enum class Shape : uint8_t {
		Circle = 0,
		Polygon = 1
	};

	constexpr int MaxPolygonVertices = 8;

	struct Polygon {
		std::array<olc::vf2d, MaxPolygonVertices> vertices{};
		int count = 0;
	};

	inline olc::vf2d Rotate(const olc::vf2d& v, float angle) {
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		return { v.x * c - v.y * s, v.x * s + v.y * c };
	}

	inline float PolygonSignedArea(const olc::vf2d* vertices, int count) {
		float area = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = vertices[i];
			const olc::vf2d& b = vertices[(i + 1) % count];
			area += a.x * b.y - b.x * a.y;
		}
		return area * 0.5f;
	}

	inline float PolygonInverseInertia(float inverseMass, const olc::vf2d* vertices, int count) {
		if (inverseMass <= 0.0f || count < 3)
			return 0.0f;
		float numerator = 0.0f;
		float denominator = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = vertices[i];
			const olc::vf2d& b = vertices[(i + 1) % count];
			const float cross = a.x * b.y - b.x * a.y;
			numerator += cross * (a.dot(a) + a.dot(b) + b.dot(b));
			denominator += cross;
		}
		if (std::abs(numerator) < 0.0001f || std::abs(denominator) < 0.0001f)
			return 0.0f;
		return 6.0f * inverseMass * denominator / numerator;
	}

	struct ObjectState {
		olc::vf2d position;
		olc::vf2d velocity;
		float radius = PlayerRadius;
		float inverseMass = 1.0f;
		bool grounded = false;
		bool supported = false;
		uint32_t lastInput = 0;
		Shape shape = Shape::Circle;
		olc::vf2d halfExtents = { 0.0f, 0.0f };
		uint8_t vertexCount = 0;
		std::array<olc::vf2d, MaxPolygonVertices> localVertices{};
		float angle = 0.0f;
		float angularVelocity = 0.0f;
		float inverseInertia = 0.0f;
		float restTime = 0.0f;
		bool sleeping = false;
	};

	inline ObjectState CreatePolygon(const olc::vf2d& position, std::vector<olc::vf2d> outline,
		float inverseMass, float angle = 0.0f) {
		ObjectState body;
		body.shape = Shape::Polygon;
		body.position = position;
		body.inverseMass = inverseMass;
		body.angle = angle;

		const int count = static_cast<int>(std::min<size_t>(outline.size(), MaxPolygonVertices));
		if (count < 3) {
			body.shape = Shape::Circle;
			return body;
		}
		outline.resize(static_cast<size_t>(count));
		if (PolygonSignedArea(outline.data(), count) < 0.0f)
			std::reverse(outline.begin(), outline.end());

		olc::vf2d centroid{ 0.0f, 0.0f };
		float area = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = outline[i];
			const olc::vf2d& b = outline[(i + 1) % count];
			const float cross = a.x * b.y - b.x * a.y;
			area += cross;
			centroid += (a + b) * cross;
		}
		if (std::abs(area) > 0.0001f)
			centroid /= 3.0f * area;

		body.vertexCount = static_cast<uint8_t>(count);
		float extentX = 0.0f, extentY = 0.0f, radius = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d vertex = outline[i] - centroid;
			body.localVertices[i] = vertex;
			extentX = std::max(extentX, std::abs(vertex.x));
			extentY = std::max(extentY, std::abs(vertex.y));
			radius = std::max(radius, vertex.mag());
		}
		body.halfExtents = { extentX, extentY };
		body.radius = radius;
		body.inverseInertia = PolygonInverseInertia(inverseMass, body.localVertices.data(), count);
		return body;
	}

	inline ObjectState CreateBox(const olc::vf2d& position, const olc::vf2d& halfExtents,
		float inverseMass, float angle = 0.0f) {
		return CreatePolygon(position, {
			{ -halfExtents.x, -halfExtents.y }, { halfExtents.x, -halfExtents.y },
			{ halfExtents.x, halfExtents.y }, { -halfExtents.x, halfExtents.y } }, inverseMass, angle);
	}

	enum RagdollPart : size_t {
		Torso, Head, Hips, LeftElbow, LeftHand, RightElbow, RightHand,
		LeftKnee, LeftFoot, RightKnee, RightFoot, PartCount
	};

	struct HandGrip {
		uint8_t target = 0;
		olc::vf2d anchor;
		float angleOffset = 0.0f;
	};

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
			case Item::OxygenTank: {
				ObjectState body;
				body.position = position;
				body.radius = 6.0f;
				body.inverseMass = 0.5f;
				return body;
			}
			case Item::Suit: return CreateBox(position, { 8.0f, 9.0f }, 0.6f);
			case Item::Helmet: return CreateBox(position, { 6.0f, 6.0f }, 0.5f);
			case Item::Boots: return CreateBox(position, { 6.0f, 4.0f }, 0.5f);
			default: return DeadItemBody();
		}
	}

	struct RagdollState {
		std::array<ObjectState, PartCount> parts;
		std::array<HandGrip, 2> grips{};
		InputState input;
		float facing = 1.0f;
		float gaitPhase = 0.0f;
		uint8_t ladder = 0;
		float ladderDetachTime = 0.0f;
		olc::vf2d footingAnchor;
		bool footingAnchored = false;
		Inventory inventory;
	};

	struct RagdollJoint {
		RagdollPart first;
		RagdollPart second;
		float length;
	};

	inline const std::array<olc::vf2d, PartCount>& RagdollOffsets() {
		static const std::array<olc::vf2d, PartCount> offsets = {{
			{0.0f, 0.0f}, {0.0f, -14.0f}, {0.0f, 12.0f},
			{-3.0f, 10.0f}, {-4.0f, 22.0f}, {3.0f, 10.0f}, {4.0f, 22.0f},
			{-2.0f, 24.0f}, {-3.0f, 37.0f}, {2.0f, 24.0f}, {3.0f, 37.0f}
		}};
		return offsets;
	}

	inline const std::array<RagdollJoint, 10>& RagdollJoints() {
		static const std::array<RagdollJoint, 10> joints = [] {
			std::array<RagdollJoint, 10> result = {{
				{Torso, Head}, {Torso, Hips}, {Torso, LeftElbow}, {LeftElbow, LeftHand},
				{Torso, RightElbow}, {RightElbow, RightHand}, {Hips, LeftKnee},
				{LeftKnee, LeftFoot}, {Hips, RightKnee}, {RightKnee, RightFoot}
			}};
			for (auto& joint : result)
				joint.length = (RagdollOffsets()[joint.second] - RagdollOffsets()[joint.first]).mag();
			return result;
		}();
		return joints;
	}

	inline RagdollState CreateRagdoll(const olc::vf2d& position) {
		RagdollState ragdoll;
		for (size_t i = 0; i < PartCount; ++i) {
			ragdoll.parts[i].position = position + RagdollOffsets()[i];
			ragdoll.parts[i].radius = i == Head ? 7.0f : i == Torso || i == Hips ? 6.0f : 4.0f;
			ragdoll.parts[i].inverseMass = i == Torso || i == Hips ? 0.5f : 1.0f;
		}
		ragdoll.inventory = CreateInventory();
		return ragdoll;
	}

	using Terrain = olc::utils::geom2d::rect<float>;

	inline const std::array<Terrain, 5>& TestTerrain() {
		static const std::array<Terrain, 5> terrain = {{
			{{-500.0f, 150.0f}, {1000.0f, 50.0f}},
			{{-500.0f, -350.0f}, {40.0f, 500.0f}},
			{{460.0f, -350.0f}, {40.0f, 500.0f}},
			{{-360.0f, 50.0f}, {140.0f, 20.0f}},
			{{200.0f, 30.0f}, {150.0f, 20.0f}}
		}};
		return terrain;
	}

	constexpr float LadderRungSpacing = 12.0f;

	inline const std::array<Terrain, 1>& TestLadders() {
		static const std::array<Terrain, 1> ladders = {{{{-218.0f, -30.0f}, {16.0f, 180.0f}}}};
		return ladders;
	}

	inline std::array<ObjectState, TestBodyCount> CreateTestBodies() {
		std::array<ObjectState, TestBodyCount> bodies{};
		bodies[0] = CreateBox({ 30.0f, 100.0f }, { 22.0f, 22.0f }, 0.5f, 0.05f);
		bodies[1] = CreateBox({ 30.0f, 40.0f }, { 22.0f, 22.0f }, 0.5f, -0.04f);
		bodies[2] = CreateBox({ 125.0f, 60.0f }, { 30.0f, 14.0f }, 0.4f, 0.7f);
		bodies[3].position = { 90.0f, -40.0f };
		bodies[3].radius = 18.0f;
		bodies[3].inverseMass = 0.5f;
		bodies[4].position = { 90.0f, -110.0f };
		bodies[4].radius = 12.0f;
		bodies[4].inverseMass = 0.75f;
		std::vector<olc::vf2d> hexagon;
		for (int i = 0; i < 6; ++i) {
			const float turn = static_cast<float>(i) * 1.0471976f;
			hexagon.push_back({ std::cos(turn) * 20.0f, std::sin(turn) * 20.0f });
		}
		bodies[5] = CreatePolygon({ -285.0f, -10.0f }, hexagon, 0.6f, 0.35f);
		return bodies;
	}

	inline void ApplyPlayerInput(ObjectState& object, const InputState& input) {
		object.velocity.x = std::clamp(input.moveX, -1.0f, 1.0f) * MoveSpeed;
		if (input.jump && object.grounded)
			object.velocity.y = -JumpSpeed;
	}

	struct Contact {
		olc::vf2d point;
		olc::vf2d normal;
		float depth = 0.0f;
	};

	constexpr int MaxContacts = 4;

	inline Polygon WorldVertices(const ObjectState& body) {
		Polygon result;
		if (body.shape != Shape::Polygon)
			return result;
		result.count = body.vertexCount;
		for (int i = 0; i < result.count; ++i)
			result.vertices[i] = body.position + Rotate(body.localVertices[i], body.angle);
		return result;
	}

	inline void ProjectVertices(const Polygon& polygon, const olc::vf2d& axis,
		float& minimum, float& maximum) {
		minimum = maximum = polygon.vertices[0].dot(axis);
		for (int i = 1; i < polygon.count; ++i) {
			const float value = polygon.vertices[i].dot(axis);
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}
	}

	inline bool ContainsPoint(const Polygon& polygon, const olc::vf2d& point, float margin = 0.0f) {
		for (int i = 0; i < polygon.count; ++i) {
			const olc::vf2d& a = polygon.vertices[i];
			const olc::vf2d& b = polygon.vertices[(i + 1) % polygon.count];
			const olc::vf2d edge = b - a;
			const float length = edge.mag();
			if (length < 0.0001f)
				continue;
			const float distance = (edge.x * (point.y - a.y) - edge.y * (point.x - a.x)) / length;
			if (distance < -margin)
				return false;
		}
		return true;
	}

	inline ObjectState StaticTerrain(const Terrain& terrain) {
		const olc::vf2d half = terrain.size * 0.5f;
		return CreatePolygon(terrain.middle(), {
			{ -half.x, -half.y }, { half.x, -half.y },
			{ half.x, half.y }, { -half.x, half.y } }, 0.0f);
	}

	inline olc::vf2d SnapContactNormal(const olc::vf2d& normal) {
		if (std::abs(normal.x) < AxisSnap)
			return { 0.0f, normal.y > 0.0f ? 1.0f : -1.0f };
		if (std::abs(normal.y) < AxisSnap)
			return { normal.x > 0.0f ? 1.0f : -1.0f, 0.0f };
		return normal;
	}

	inline void Wake(ObjectState& body) {
		body.sleeping = false;
		body.restTime = 0.0f;
	}

	inline void UpdateSleep(ObjectState& body, float step) {
		if (body.inverseMass <= 0.0f)
			return;
		const bool resting = body.grounded && body.velocity.mag() < SleepSpeed
			&& std::abs(body.angularVelocity) < SleepSpeed;
		body.restTime = resting ? body.restTime + step : 0.0f;
		if (body.restTime < SleepDelay)
			return;
		body.sleeping = true;
		body.velocity = { 0.0f, 0.0f };
		body.angularVelocity = 0.0f;
	}

	inline bool PolygonPenetration(const ObjectState& first, const ObjectState& second,
		olc::vf2d& normal, float& depth) {
		const Polygon firstVertices = WorldVertices(first);
		const Polygon secondVertices = WorldVertices(second);
		if (firstVertices.count < 3 || secondVertices.count < 3)
			return false;

		float shallowest = std::numeric_limits<float>::max();
		olc::vf2d axis = { 1.0f, 0.0f };
		bool separated = false;

		const auto testOutline = [&](const Polygon& polygon, float bias) {
			for (int i = 0; i < polygon.count; ++i) {
				const olc::vf2d edge = polygon.vertices[(i + 1) % polygon.count] - polygon.vertices[i];
				const float length = edge.mag();
				if (length < 0.0001f)
					continue;
				const olc::vf2d candidate = edge.perp() / length;
				float firstMinimum = 0.0f, firstMaximum = 0.0f, secondMinimum = 0.0f, secondMaximum = 0.0f;
				ProjectVertices(firstVertices, candidate, firstMinimum, firstMaximum);
				ProjectVertices(secondVertices, candidate, secondMinimum, secondMaximum);
				if (firstMaximum < secondMinimum - ContactMargin
					|| secondMaximum < firstMinimum - ContactMargin) {
					separated = true;
					return;
				}
				const float overlap = std::min(firstMaximum, secondMaximum) - std::max(firstMinimum, secondMinimum);
				if (overlap < shallowest - bias) {
					shallowest = overlap;
					axis = candidate;
				}
			}
		};
		testOutline(firstVertices, 0.5f);
		if (!separated)
			testOutline(secondVertices, 0.0f);
		if (separated)
			return false;

		if ((second.position - first.position).dot(axis) < 0.0f)
			axis = -axis;
		axis = SnapContactNormal(axis);
		normal = axis;
		depth = shallowest;
		return true;
	}

	inline bool CirclePolygonContact(const ObjectState& circle, const ObjectState& polygon, Contact& contact) {
		const Polygon outline = WorldVertices(polygon);
		if (outline.count < 3)
			return false;

		float closestDistance = std::numeric_limits<float>::max();
		olc::vf2d closestPoint = polygon.position;
		olc::vf2d closestOutward = { 0.0f, -1.0f };
		for (int i = 0; i < outline.count; ++i) {
			const olc::vf2d& a = outline.vertices[i];
			const olc::vf2d& b = outline.vertices[(i + 1) % outline.count];
			const olc::vf2d edge = b - a;
			const float squared = edge.dot(edge);
			if (squared < 0.0001f)
				continue;
			const float t = std::clamp((circle.position - a).dot(edge) / squared, 0.0f, 1.0f);
			const olc::vf2d point = a + edge * t;
			const float distance = (circle.position - point).mag();
			if (distance < closestDistance) {
				closestDistance = distance;
				closestPoint = point;
				closestOutward = edge.perp() / std::sqrt(squared);
			}
		}

		if (!ContainsPoint(outline, circle.position)) {
			if (closestDistance >= circle.radius + ContactMargin || closestDistance < 0.0001f)
				return false;
			contact.normal = (closestPoint - circle.position) / closestDistance;
			contact.depth = circle.radius - closestDistance;
		}
		else {
			contact.normal = closestOutward;
			contact.depth = circle.radius + closestDistance;
		}
		contact.point = closestPoint;
		return true;
	}

	inline bool CircleCircleContact(const ObjectState& first, const ObjectState& second, Contact& contact) {
		const olc::vf2d offset = second.position - first.position;
		const float distance = offset.mag();
		if (distance >= first.radius + second.radius + ContactMargin)
			return false;
		contact.normal = distance > 0.0001f ? offset / distance : olc::vf2d{ 1.0f, 0.0f };
		contact.depth = first.radius + second.radius - distance;
		contact.point = first.position + contact.normal * first.radius;
		return true;
	}

	inline int PolygonContacts(const ObjectState& first, const ObjectState& second,
		const olc::vf2d& normal, float depth, Contact* contacts) {
		const Polygon firstOutline = WorldVertices(first);
		const Polygon secondOutline = WorldVertices(second);
		int count = 0;
		for (int i = 0; i < secondOutline.count && count < MaxContacts; ++i)
			if (ContainsPoint(firstOutline, secondOutline.vertices[i], 0.05f))
				contacts[count++] = { secondOutline.vertices[i], normal, depth };
		for (int i = 0; i < firstOutline.count && count < MaxContacts; ++i)
			if (ContainsPoint(secondOutline, firstOutline.vertices[i], 0.05f))
				contacts[count++] = { firstOutline.vertices[i], normal, depth };
		if (count > 0)
			return count;

		olc::vf2d firstSupport = first.position;
		float firstDeepest = -std::numeric_limits<float>::max();
		for (int i = 0; i < firstOutline.count; ++i) {
			const float projection = firstOutline.vertices[i].dot(normal);
			if (projection > firstDeepest) {
				firstDeepest = projection;
				firstSupport = firstOutline.vertices[i];
			}
		}
		olc::vf2d secondSupport = second.position;
		float secondDeepest = std::numeric_limits<float>::max();
		for (int i = 0; i < secondOutline.count; ++i) {
			const float projection = secondOutline.vertices[i].dot(normal);
			if (projection < secondDeepest) {
				secondDeepest = projection;
				secondSupport = secondOutline.vertices[i];
			}
		}
		contacts[0] = { (firstSupport + secondSupport) * 0.5f, normal, depth };
		return 1;
	}

	inline void ApplyContact(ObjectState& first, ObjectState& second, const Contact& contact) {
		const olc::vf2d armFirst = contact.point - first.position;
		const olc::vf2d armSecond = contact.point - second.position;

		const olc::vf2d firstVelocity = first.velocity + armFirst.perp() * first.angularVelocity;
		const olc::vf2d secondVelocity = second.velocity + armSecond.perp() * second.angularVelocity;
		const float normalSpeed = (secondVelocity - firstVelocity).dot(contact.normal);
		if (normalSpeed > 0.0f)
			return;

		const float firstLever = armFirst.cross(contact.normal);
		const float secondLever = armSecond.cross(contact.normal);
		const float normalMass = first.inverseMass + second.inverseMass
			+ first.inverseInertia * firstLever * firstLever + second.inverseInertia * secondLever * secondLever;
		if (normalMass <= 0.0f)
			return;

		const float restitution = normalSpeed < -RestitutionThreshold ? Restitution : 0.0f;
		const float impulse = -(1.0f + restitution) * normalSpeed / normalMass;
		const olc::vf2d push = contact.normal * impulse;
		first.velocity -= push * first.inverseMass;
		second.velocity += push * second.inverseMass;
		first.angularVelocity -= first.inverseInertia * armFirst.cross(push);
		second.angularVelocity += second.inverseInertia * armSecond.cross(push);

		const olc::vf2d tangent = contact.normal.perp();
		const olc::vf2d firstTangent = first.velocity + armFirst.perp() * first.angularVelocity;
		const olc::vf2d secondTangent = second.velocity + armSecond.perp() * second.angularVelocity;
		const float tangentSpeed = (secondTangent - firstTangent).dot(tangent);
		const float firstFrictionLever = armFirst.cross(tangent);
		const float secondFrictionLever = armSecond.cross(tangent);
		const float tangentMass = first.inverseMass + second.inverseMass
			+ first.inverseInertia * firstFrictionLever * firstFrictionLever
			+ second.inverseInertia * secondFrictionLever * secondFrictionLever;
		if (tangentMass <= 0.0f)
			return;

		const float limit = ContactFriction * impulse;
		const float frictionImpulse = std::clamp(-tangentSpeed / tangentMass, -limit, limit);
		const olc::vf2d scrape = tangent * frictionImpulse;
		first.velocity -= scrape * first.inverseMass;
		second.velocity += scrape * second.inverseMass;
		first.angularVelocity -= first.inverseInertia * armFirst.cross(scrape);
		second.angularVelocity += second.inverseInertia * armSecond.cross(scrape);
	}

	inline void CorrectContact(ObjectState& first, ObjectState& second, const Contact& contact) {
		const float inverseMass = first.inverseMass + second.inverseMass;
		if (inverseMass <= 0.0f)
			return;
		const float correction = std::max(contact.depth - ContactSlop, 0.0f) * ContactCorrection / inverseMass;
		const olc::vf2d normal = SnapContactNormal(contact.normal);
		first.position -= normal * (correction * first.inverseMass);
		second.position += normal * (correction * second.inverseMass);
	}

	inline void ResolveShapePair(ObjectState& first, ObjectState& second) {
		Contact contacts[MaxContacts];
		int count = 0;

		if (first.shape == Shape::Circle && second.shape == Shape::Circle) {
			if (!CircleCircleContact(first, second, contacts[0]))
				return;
			count = 1;
		}
		else if (first.shape == Shape::Polygon && second.shape == Shape::Polygon) {
			olc::vf2d normal;
			float depth = 0.0f;
			if (!PolygonPenetration(first, second, normal, depth))
				return;
			count = PolygonContacts(first, second, normal, depth, contacts);
		}
		else if (first.shape == Shape::Circle) {
			if (!CirclePolygonContact(first, second, contacts[0]))
				return;
			count = 1;
		}
		else {
			if (!CirclePolygonContact(second, first, contacts[0]))
				return;
			contacts[0].normal = -contacts[0].normal;
			count = 1;
		}
		if (count <= 0)
			return;

		if (first.sleeping && !second.sleeping && second.velocity.mag() > WakeSpeed)
			Wake(first);
		if (second.sleeping && !first.sleeping && first.velocity.mag() > WakeSpeed)
			Wake(second);

		for (int pass = 0; pass < 2; ++pass)
			for (int index = 0; index < count; ++index)
				ApplyContact(first, second, contacts[index]);

		int deepest = 0;
		for (int index = 1; index < count; ++index)
			if (contacts[index].depth > contacts[deepest].depth)
				deepest = index;
		CorrectContact(first, second, contacts[deepest]);

		const olc::vf2d normal = contacts[deepest].normal;
		if (normal.y > 0.5f && (second.inverseMass <= 0.0f || second.supported))
			first.supported = true;
		if (normal.y < -0.5f && (first.inverseMass <= 0.0f || first.supported))
			second.supported = true;
	}

	inline const std::array<ObjectState, 5>& StaticTerrainBodies() {
		static const std::array<ObjectState, 5> bodies = [] {
			std::array<ObjectState, 5> built;
			for (size_t i = 0; i < built.size(); ++i)
				built[i] = StaticTerrain(TestTerrain()[i]);
			return built;
		}();
		return bodies;
	}

	inline void ResolveTerrain(ObjectState& object, size_t terrainIndex) {
		const Terrain& terrain = TestTerrain()[terrainIndex];
		if (object.position.x + object.radius < terrain.pos.x
			|| object.position.x - object.radius > terrain.pos.x + terrain.size.x
			|| object.position.y + object.radius < terrain.pos.y
			|| object.position.y - object.radius > terrain.pos.y + terrain.size.y)
			return;
		ObjectState ground = StaticTerrainBodies()[terrainIndex];
		ResolveShapePair(ground, object);
	}

	inline void IntegrateRotation(ObjectState& body, float step) {
		if (body.inverseInertia <= 0.0f)
			return;
		const float damping = body.grounded ? AngularDamping + GroundedSpinDamping : AngularDamping;
		body.angularVelocity -= body.angularVelocity * std::min(1.0f, damping * step);
		body.angularVelocity = std::clamp(body.angularVelocity, -MaxAngularSpeed, MaxAngularSpeed);
		body.angle = std::remainder(body.angle + body.angularVelocity * step, 6.2831853f);
	}

	inline std::array<olc::vf2d, PartCount> RagdollTargets(const RagdollState& ragdoll) {
		const olc::vf2d torso = ragdoll.parts[Torso].position;
		std::array<olc::vf2d, PartCount> targets;
		for (size_t i = 0; i < PartCount; ++i) {
			const olc::vf2d offset = RagdollOffsets()[i];
			targets[i] = torso + olc::vf2d{ offset.x * ragdoll.facing, offset.y };
		}

		if (ragdoll.ladder) {
			const Terrain& ladder = TestLadders()[ragdoll.ladder - 1];
			const float rungX = ladder.pos.x + ladder.size.x * 0.5f;
			const int count = static_cast<int>(ladder.size.y / LadderRungSpacing);
			const int base = static_cast<int>(std::floor((torso.y - ladder.pos.y) / LadderRungSpacing));
			const int offset = (base % 2 + 2) % 2;
			for (size_t limb = 0; limb < 2; ++limb) {
				const int stagger = limb == 0 ? offset : 1 - offset;
				const size_t armJoint = limb == 0 ? 2 : 4;
				const size_t legJoint = limb == 0 ? 6 : 8;
				const RagdollPart elbow = limb == 0 ? LeftElbow : RightElbow;
				const RagdollPart hand = limb == 0 ? LeftHand : RightHand;
				const RagdollPart knee = limb == 0 ? LeftKnee : RightKnee;
				const RagdollPart foot = limb == 0 ? LeftFoot : RightFoot;
				const float handY = ladder.pos.y + std::clamp(base - 1 + stagger, 0, count) * LadderRungSpacing;
				const float footY = ladder.pos.y + std::clamp(base + 2 - stagger, 0, count) * LadderRungSpacing;
				const bool reaching = limb == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight;
				const olc::vf2d handTarget = reaching ? torso + ragdoll.input.aim : olc::vf2d{rungX, handY};
				const TwoBoneIkResult arm = SolveTwoBoneIk(torso, handTarget,
					RagdollJoints()[armJoint].length, RagdollJoints()[armJoint + 1].length, ragdoll.facing);
				const TwoBoneIkResult leg = SolveTwoBoneIk(targets[Hips], {rungX, footY},
					RagdollJoints()[legJoint].length, RagdollJoints()[legJoint + 1].length, -ragdoll.facing);
				targets[elbow] = arm.joint;
				targets[hand] = arm.end;
				targets[knee] = leg.joint;
				targets[foot] = leg.end;
			}
			return targets;
		}

		const MovementState movement = GetMovementState(ragdoll.input);
		const float stride = std::clamp(std::abs(ragdoll.input.moveX), 0.0f, 1.0f);
		const float swing = std::sin(ragdoll.gaitPhase) * stride;
		if (movement == MovementState::Crawling) {
			targets[Head] = torso + olc::vf2d{14.0f * ragdoll.facing, -2.0f};
			targets[Hips] = torso + olc::vf2d{-12.0f * ragdoll.facing, 0.0f};
		}
		const olc::vf2d hip = targets[Hips];
		for (int leg = 0; leg < 2; ++leg) {
			const float phase = leg == 0 ? swing : -swing;
			const RagdollPart knee = leg == 0 ? LeftKnee : RightKnee;
			const RagdollPart foot = leg == 0 ? LeftFoot : RightFoot;
			const size_t joint = leg == 0 ? 6 : 8;
			olc::vf2d offset;
			if (movement == MovementState::Crawling)
				offset = {(-18.0f + phase * 7.0f) * ragdoll.facing, 7.0f - std::max(0.0f, phase) * 3.0f};
			else {
				const float strideLength = movement == MovementState::Running ? 14.0f : movement == MovementState::Crouching ? 6.0f : 11.0f;
				const float legHeight = movement == MovementState::Crouching ? 13.0f : 25.0f;
				offset = {(phase * strideLength + (leg == 0 ? -2.0f : 2.0f)) * ragdoll.facing,
					legHeight - std::max(0.0f, phase) * 5.0f};
			}
			const olc::vf2d target = hip + offset;
			const TwoBoneIkResult pose = SolveTwoBoneIk(hip, target, RagdollJoints()[joint].length, RagdollJoints()[joint + 1].length, -ragdoll.facing);
			targets[knee] = pose.joint;
			targets[foot] = pose.end;
		}

		for (size_t arm = 0; arm < 2; ++arm) {
			const bool reaching = arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight;
			const size_t joint = arm == 0 ? 2 : 4;
			const RagdollPart elbow = arm == 0 ? LeftElbow : RightElbow;
			const RagdollPart hand = arm == 0 ? LeftHand : RightHand;
			const float phase = arm == 0 ? -swing : swing;
			const olc::vf2d offset = reaching ? ragdoll.input.aim : movement == MovementState::Crawling
				? olc::vf2d{(9.0f + phase * 6.0f) * ragdoll.facing, 10.0f}
				: olc::vf2d{RagdollOffsets()[hand].x * ragdoll.facing, RagdollOffsets()[hand].y};
			const TwoBoneIkResult pose = SolveTwoBoneIk(torso, torso + offset,
				RagdollJoints()[joint].length, RagdollJoints()[joint + 1].length, ragdoll.facing);
			targets[elbow] = pose.joint;
			targets[hand] = pose.end;
		}
		return targets;
	}

	inline void DriveRagdoll(RagdollState& ragdoll, float deltaTime) {
		const auto targets = RagdollTargets(ragdoll);
		ObjectState& torsoPart = ragdoll.parts[Torso];
		const olc::vf2d torsoVelocity = torsoPart.velocity;
		olc::vf2d reaction;
		for (size_t i = 1; i < PartCount; ++i) {
			ObjectState& part = ragdoll.parts[i];
			olc::vf2d acceleration = (targets[i] - part.position) * 180.0f - (part.velocity - torsoVelocity) * 24.0f;
			const float magnitude = acceleration.mag();
			if (magnitude > 5000.0f)
				acceleration *= 5000.0f / magnitude;
			part.velocity += acceleration * deltaTime;
			reaction -= acceleration * (torsoPart.inverseMass / part.inverseMass);
		}
		torsoPart.velocity += reaction * deltaTime;
	}

	inline void ApplyRagdollInput(RagdollState& ragdoll, const InputState& input) {
		ragdoll.input = input;
		if (input.hotbar < HotbarSlots)
			ragdoll.inventory.selected = input.hotbar;
		ragdoll.ladderDetachTime = std::max(0.0f, ragdoll.ladderDetachTime - FixedDeltaTime);
		const olc::vf2d position = ragdoll.parts[Torso].position;
		if (ragdoll.ladder && (input.jump || std::abs(input.moveX) > 0.01f)) {
			ragdoll.ladder = 0;
			ragdoll.ladderDetachTime = 0.3f;
			if (input.jump)
				for (ObjectState& part : ragdoll.parts)
					part.velocity.y = -JumpSpeed;
		}
		if (!ragdoll.ladder && ragdoll.ladderDetachTime == 0.0f && std::abs(input.climb) > 0.01f
			&& std::abs(input.moveX) <= 0.01f && !input.jump) {
			for (size_t i = 0; i < TestLadders().size(); ++i) {
				const Terrain& ladder = TestLadders()[i];
				if (std::abs(position.x - (ladder.pos.x + ladder.size.x * 0.5f)) <= 18.0f
					&& position.y >= ladder.pos.y && position.y <= ladder.pos.y + ladder.size.y - 20.0f) {
					ragdoll.ladder = static_cast<uint8_t>(i + 1);
					ragdoll.facing = position.x < ladder.pos.x + ladder.size.x * 0.5f ? 1.0f : -1.0f;
					break;
				}
			}
		}
		if (ragdoll.ladder) {
			const Terrain& ladder = TestLadders()[ragdoll.ladder - 1];
			const float center = ladder.pos.x + ladder.size.x * 0.5f;
			const float speedX = std::clamp((center - ragdoll.facing * 12.0f - position.x) * 8.0f, -55.0f, 55.0f);
			float speedY = std::clamp(input.climb, -1.0f, 1.0f) * 90.0f;
			if ((position.y <= ladder.pos.y + 22.0f && speedY < 0.0f)
				|| (position.y >= ladder.pos.y + ladder.size.y - 35.0f && speedY > 0.0f))
				speedY = 0.0f;
			const olc::vf2d change = olc::vf2d{speedX, speedY} - ragdoll.parts[Torso].velocity;
			for (ObjectState& part : ragdoll.parts)
				part.velocity += change;
			return;
		}
		const MovementState movement = GetMovementState(input);
		const bool grounded = ragdoll.parts[LeftFoot].grounded || ragdoll.parts[RightFoot].grounded
			|| (movement == MovementState::Crawling && (ragdoll.parts[Torso].grounded || ragdoll.parts[Hips].grounded
				|| ragdoll.parts[LeftHand].grounded || ragdoll.parts[RightHand].grounded));
		if (std::abs(input.moveX) > 0.01f) {
			ragdoll.facing = input.moveX > 0.0f ? 1.0f : -1.0f;
			if (grounded) {
				const float rate = movement == MovementState::Running ? 14.0f : movement == MovementState::Crawling ? 6.0f : 10.0f;
				ragdoll.gaitPhase = std::fmod(ragdoll.gaitPhase + FixedDeltaTime * rate * std::min(std::abs(input.moveX), 1.0f), 6.2831853f);
			}
		}
		else if ((input.reachLeft || input.reachRight) && std::abs(input.aim.x) > 0.01f) {
			ragdoll.facing = input.aim.x > 0.0f ? 1.0f : -1.0f;
		}
		const float targetSpeed = std::clamp(input.moveX, -1.0f, 1.0f) * MovementSpeed(movement);
		const float change = std::clamp(targetSpeed - ragdoll.parts[Torso].velocity.x, -30.0f, 30.0f);
		for (ObjectState& part : ragdoll.parts)
			part.velocity.x += change;
		if (input.jump && grounded)
			for (ObjectState& part : ragdoll.parts)
				part.velocity.y = std::min(part.velocity.y, -JumpSpeed);
	}

	inline void StabilizeRagdoll(RagdollState& ragdoll) {
		const auto targets = RagdollTargets(ragdoll);
		ObjectState& torso = ragdoll.parts[Torso];
		for (size_t i = 1; i < PartCount; ++i) {
			ObjectState& part = ragdoll.parts[i];
			const float mass = torso.inverseMass + part.inverseMass;
			if (mass <= 0.0f)
				continue;
			const olc::vf2d correction = (targets[i] - part.position) * (0.2f / mass);
			part.position += correction * part.inverseMass;
			torso.position -= correction * torso.inverseMass;
		}
	}

	inline void ResolveJoint(ObjectState& first, ObjectState& second, float length) {
		const olc::vf2d offset = second.position - first.position;
		const float distance = offset.mag();
		const float mass = first.inverseMass + second.inverseMass;
		if (distance < 0.0001f || mass <= 0.0f)
			return;
		const olc::vf2d normal = offset / distance;
		const olc::vf2d correction = normal * ((distance - length) / mass);
		first.position += correction * first.inverseMass;
		second.position -= correction * second.inverseMass;
		const float speed = (second.velocity - first.velocity).dot(normal) / mass;
		first.velocity += normal * (speed * first.inverseMass);
		second.velocity -= normal * (speed * second.inverseMass);
	}

	inline void ResolveBendLimit(ObjectState& root, ObjectState& joint, ObjectState& end, float bendSign) {
		const olc::vf2d axis = end.position - root.position;
		const float length = axis.mag();
		if (length < 0.0001f)
			return;
		const olc::vf2d direction = axis / length;
		const olc::vf2d normal = direction.perp() * bendSign;
		const float t = std::clamp((joint.position - root.position).dot(direction) / length, 0.0f, 1.0f);
		const float distance = (joint.position - root.position).dot(normal);
		const float mass = joint.inverseMass + root.inverseMass * (1.0f - t) * (1.0f - t)
			+ end.inverseMass * t * t;
		if (distance >= 0.0f || mass <= 0.0f)
			return;
		const float correction = -distance / mass;
		joint.position += normal * (correction * joint.inverseMass);
		root.position -= normal * (correction * root.inverseMass * (1.0f - t));
		end.position -= normal * (correction * end.inverseMass * t);
		const float speed = (joint.velocity - root.velocity * (1.0f - t) - end.velocity * t).dot(normal);
		if (speed < 0.0f) {
			const float impulse = -speed / mass;
			joint.velocity += normal * (impulse * joint.inverseMass);
			root.velocity -= normal * (impulse * root.inverseMass * (1.0f - t));
			end.velocity -= normal * (impulse * end.inverseMass * t);
		}
	}

	inline void ResolveRagdollBendLimits(RagdollState& ragdoll) {
		ResolveBendLimit(ragdoll.parts[Torso], ragdoll.parts[LeftElbow], ragdoll.parts[LeftHand], ragdoll.facing);
		ResolveBendLimit(ragdoll.parts[Torso], ragdoll.parts[RightElbow], ragdoll.parts[RightHand], ragdoll.facing);
		ResolveBendLimit(ragdoll.parts[Hips], ragdoll.parts[LeftKnee], ragdoll.parts[LeftFoot], -ragdoll.facing);
		ResolveBendLimit(ragdoll.parts[Hips], ragdoll.parts[RightKnee], ragdoll.parts[RightFoot], -ragdoll.facing);
	}

	inline bool AimingAtContact(const olc::vf2d& aim, const olc::vf2d& outwardNormal) {
		const float length = aim.mag();
		return length > 0.0001f && aim.dot(outwardNormal) < -0.25f * length;
	}

	inline olc::vf2d ClosestOnObject(const ObjectState& object, const olc::vf2d& position) {
		if (object.shape != Shape::Polygon) {
			const olc::vf2d offset = position - object.position;
			const float distance = offset.mag();
			return distance > 0.0001f ? object.position + offset * (object.radius / distance) : object.position;
		}
		const Polygon polygon = WorldVertices(object);
		olc::vf2d closest = polygon.vertices[0];
		float closestDistance = (closest - position).mag2();
		for (int i = 0; i < polygon.count; ++i) {
			const olc::vf2d a = polygon.vertices[i];
			const olc::vf2d b = polygon.vertices[(i + 1) % polygon.count];
			const olc::vf2d edge = b - a;
			const float lengthSquared = edge.mag2();
			const float t = lengthSquared > 0.0001f
				? std::clamp((position - a).dot(edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
			const olc::vf2d point = a + edge * t;
			const float distance = (point - position).mag2();
			if (distance < closestDistance) {
				closestDistance = distance;
				closest = point;
			}
		}
		return closest;
	}

	inline void ResolveHandGrip(RagdollState& ragdoll, size_t arm, const std::vector<ObjectState*>& objects) {
		HandGrip& grip = ragdoll.grips[arm];
		ObjectState& hand = ragdoll.parts[arm == 0 ? LeftHand : RightHand];
		if (!(arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight)) {
			grip = {};
			return;
		}

		const auto& terrain = TestTerrain();
		if (grip.target == 0) {
			for (size_t i = 0; i < terrain.size(); ++i) {
				const olc::vf2d closest = olc::utils::geom2d::closest(terrain[i], hand.position);
				ObjectState contact = hand;
				ResolveTerrain(contact, i);
				const olc::vf2d offset = contact.position - closest;
				const float distance = offset.mag();
				if ((hand.position - closest).mag() <= hand.radius + 0.5f && distance > 0.0001f
					&& AimingAtContact(ragdoll.input.aim, offset / distance)) {
					grip.target = static_cast<uint8_t>(i + 1);
					grip.anchor = contact.position;
					break;
				}
			}
			for (size_t i = 0; grip.target == 0 && i < objects.size(); ++i) {
				const olc::vf2d closest = ClosestOnObject(*objects[i], hand.position);
				const olc::vf2d offset = hand.position - closest;
				const float distance = offset.mag();
				if (distance > hand.radius + 0.5f)
					continue;
				const olc::vf2d normal = distance > 0.0001f ? offset / distance : olc::vf2d{0.0f, -1.0f};
				if (!AimingAtContact(ragdoll.input.aim, normal))
					continue;
				grip.target = static_cast<uint8_t>(terrain.size() + i + 1);
				grip.anchor = Rotate(hand.position - objects[i]->position, -objects[i]->angle);
				grip.angleOffset = std::remainder(objects[i]->angle - hand.angle, 6.2831853f);
			}
		}
		if (grip.target == 0)
			return;
		if (grip.target <= terrain.size()) {
			hand.position = grip.anchor;
			hand.velocity = {0.0f, 0.0f};
			return;
		}
		const size_t objectIndex = grip.target - terrain.size() - 1;
		if (objectIndex >= objects.size()) {
			grip = {};
			return;
		}
		ObjectState& object = *objects[objectIndex];
		const float mass = hand.inverseMass + object.inverseMass;
		if (mass <= 0.0f)
			return;
		const olc::vf2d anchor = object.position + Rotate(grip.anchor, object.angle);
		const olc::vf2d correction = (anchor - hand.position) / mass;
		hand.position += correction * hand.inverseMass;
		object.position -= correction * object.inverseMass;
		const olc::vf2d impulse = (object.velocity - hand.velocity) / mass;
		hand.velocity += impulse * hand.inverseMass;
		object.velocity -= impulse * object.inverseMass;

		const float angularMass = hand.inverseInertia + object.inverseInertia;
		if (angularMass <= 0.0f)
			return;
		const float error = std::remainder(hand.angle + grip.angleOffset - object.angle, 6.2831853f);
		const float spin = object.angularVelocity - hand.angularVelocity;
		const float target = std::clamp(error * GripRotationStiffness, -MaxGripSpin, MaxGripSpin);
		const float change = (target - spin) * GripRotationDamping / angularMass;
		object.angularVelocity += change * object.inverseInertia;
		hand.angularVelocity -= change * hand.inverseInertia;
	}

	inline void HoldGroundedFooting(RagdollState& ragdoll) {
		const bool idle = !ragdoll.ladder && std::abs(ragdoll.input.moveX) <= 0.01f;
		const bool supported = ragdoll.parts[LeftFoot].grounded || ragdoll.parts[RightFoot].grounded;
		if (!idle || !supported) {
			ragdoll.footingAnchored = false;
			return;
		}

		for (ObjectState& part : ragdoll.parts)
			part.velocity.x = 0.0f;

		const olc::vf2d torso = ragdoll.parts[Torso].position;
		if (!ragdoll.footingAnchored) {
			ragdoll.footingAnchor = torso;
			ragdoll.footingAnchored = true;
			return;
		}
		const float slip = torso.x - ragdoll.footingAnchor.x;
		if (std::abs(slip) > FootingSlipLimit) {
			ragdoll.footingAnchor = torso;
			return;
		}
		for (ObjectState& part : ragdoll.parts)
			part.position.x -= slip;
	}

	inline void StepPhysics(const std::vector<RagdollState*>& ragdolls,
		const std::vector<ObjectState*>& objects, float deltaTime) {
		constexpr int Substeps = 4;
		constexpr int SolverIterations = 12;
		const float step = deltaTime / Substeps;
		std::vector<ObjectState*> bodies;
		std::vector<size_t> owners;
		for (size_t i = 0; i < ragdolls.size(); ++i)
			for (ObjectState& part : ragdolls[i]->parts) {
				bodies.push_back(&part);
				owners.push_back(i);
			}
		for (ObjectState* object : objects) {
			bodies.push_back(object);
			owners.push_back(ragdolls.size() + owners.size());
		}
		for (ObjectState* body : bodies)
			body->grounded = false;

		for (int substep = 0; substep < Substeps; ++substep) {
			for (ObjectState* body : bodies)
				body->supported = false;
			for (RagdollState* ragdoll : ragdolls)
				for (size_t arm = 0; arm < ragdoll->grips.size(); ++arm)
					ResolveHandGrip(*ragdoll, arm, objects);
			for (RagdollState* ragdoll : ragdolls)
				DriveRagdoll(*ragdoll, step);
			for (size_t i = 0; i < bodies.size(); ++i) {
				ObjectState* body = bodies[i];
				if (body->inverseMass <= 0.0f || body->sleeping)
					continue;
				if (owners[i] >= ragdolls.size() || !ragdolls[owners[i]]->ladder)
					body->velocity.y += Gravity * step;
				body->position += body->velocity * step;
				IntegrateRotation(*body, step);
			}
			for (int iteration = 0; iteration < SolverIterations; ++iteration) {
				for (RagdollState* ragdoll : ragdolls) {
					StabilizeRagdoll(*ragdoll);
					for (const RagdollJoint& joint : RagdollJoints())
						ResolveJoint(ragdoll->parts[joint.first], ragdoll->parts[joint.second], joint.length);
					ResolveRagdollBendLimits(*ragdoll);
				}
				for (ObjectState* body : bodies)
					if (body->inverseMass > 0.0f)
						for (size_t i = 0; i < TestTerrain().size(); ++i)
							ResolveTerrain(*body, i);
				for (size_t first = 0; first < bodies.size(); ++first)
					for (size_t second = first + 1; second < bodies.size(); ++second) {
						if (owners[first] == owners[second])
							continue;
						const ObjectState& a = *bodies[first];
						const ObjectState& b = *bodies[second];
						if ((a.position - b.position).mag() > a.radius + b.radius + ContactMargin)
							continue;
						ResolveShapePair(*bodies[first], *bodies[second]);
					}
				for (RagdollState* ragdoll : ragdolls)
					for (size_t arm = 0; arm < ragdoll->grips.size(); ++arm)
						ResolveHandGrip(*ragdoll, arm, objects);
			}

			for (ObjectState* body : bodies)
				if (body->inverseMass > 0.0f)
					for (size_t i = 0; i < TestTerrain().size(); ++i)
						ResolveTerrain(*body, i);

			for (ObjectState* body : bodies)
				body->grounded = body->grounded || body->supported;

			for (ObjectState* object : objects)
				UpdateSleep(*object, step);
			for (RagdollState* ragdoll : ragdolls)
				HoldGroundedFooting(*ragdoll);
			for (RagdollState* ragdoll : ragdolls) {
				if (ragdoll->ladder || std::abs(ragdoll->input.moveX) > 0.01f)
					continue;
				for (const RagdollPart foot : { LeftFoot, RightFoot }) {
					const ObjectState& part = ragdoll->parts[foot];
					if (!part.grounded)
						continue;
					for (ObjectState* object : objects) {
						if (object->inverseMass <= 0.0f || object->sleeping)
							continue;
						if ((part.position - object->position).mag() > part.radius + object->radius + 1.0f)
							continue;
						object->velocity.x = 0.0f;
						object->angularVelocity *= RestDamping;
					}
				}
			}
			for (ObjectState* body : bodies) {
				if (!body->grounded || body->velocity.mag() >= RestSpeed)
					continue;
				body->velocity *= RestDamping;
				body->angularVelocity *= RestDamping;
			}
		}
	}
}
