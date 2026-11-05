#pragma once
#include "Physics.h"
#include "olcUTIL3_Geometry2D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Trauma {
	constexpr size_t TestBodyCount = 6;
	constexpr float LadderRungSpacing = 12.0f;

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

	inline const std::array<Terrain, 1>& TestLadders() {
		static const std::array<Terrain, 1> ladders = {{{{-218.0f, -30.0f}, {16.0f, 180.0f}}}};
		return ladders;
	}

	constexpr int MaxShelves = 4;
	constexpr int MaxShelfItems = 8;
	constexpr float ShelfHalfWidth = 22.0f;
	constexpr float ShelfBoardHalfHeight = 2.5f;
	constexpr float ShelfBoardSpacing = 28.0f;
	constexpr float ShelfSlotHalfWidth = 20.0f;
	constexpr float ShelfPlaceRange = 64.0f;
	constexpr float ShelfPickTop = -58.0f;
	constexpr float ShelfPickBottom = 5.0f;

	inline std::array<ObjectState, 2> ShelfBoards() {
		return { CreateBox({ 0.0f, ShelfBoardHalfHeight }, { ShelfHalfWidth, ShelfBoardHalfHeight }, 0.0f),
			CreateBox({ 0.0f, -ShelfBoardSpacing + ShelfBoardHalfHeight },
				{ ShelfHalfWidth, ShelfBoardHalfHeight }, 0.0f) };
	}

	inline Terrain ShelfBounds(const olc::vf2d& position) {
		return { { position.x - ShelfHalfWidth, position.y - ShelfBoardSpacing },
			{ ShelfHalfWidth * 2.0f, ShelfBoardSpacing + ShelfBoardHalfHeight * 2.0f } };
	}

	inline bool ShelfSlotAt(const olc::vf2d& local, float& boardTop) {
		if (std::abs(local.x) > ShelfSlotHalfWidth)
			return false;
		if (local.y >= -23.0f && local.y <= 0.0f) {
			boardTop = 0.0f;
			return true;
		}
		if (local.y >= -58.0f && local.y <= -28.0f) {
			boardTop = -ShelfBoardSpacing;
			return true;
		}
		return false;
	}

	inline bool ShelfLocalPlacement(const olc::vf2d& world, const olc::vf2d& shelf, float radius,
		olc::vf2d& local) {
		local = world - shelf;
		float boardTop = 0.0f;
		if (!ShelfSlotAt(local, boardTop))
			return false;
		const float margin = std::min(radius, 10.0f);
		local.x = std::clamp(local.x, -ShelfSlotHalfWidth + margin, ShelfSlotHalfWidth - margin);
		local.y = boardTop - radius - 1.0f;
		return true;
	}

	constexpr float DropClearance = 2.0f;

	inline ObjectState StaticTerrain(const Terrain& terrain) {
		const olc::vf2d half = terrain.size * 0.5f;
		return CreatePolygon(terrain.middle(), {
			{ -half.x, -half.y }, { half.x, -half.y },
			{ half.x, half.y }, { -half.x, half.y } }, 0.0f);
	}

	inline void CreateTerrainBodies(PhysicsWorld& physics) {
		for (const b2BodyId body : physics.terrain)
			if (b2Body_IsValid(body))
				b2DestroyBody(body);
		physics.terrain.clear();

		for (const Terrain& terrain : TestTerrain()) {
			const ObjectState body = StaticTerrain(terrain);
			physics.terrain.push_back(BuildBody(physics, body));
		}
	}

	inline b2BodyId TerrainBody(PhysicsWorld& physics, size_t index) {
		return index < physics.terrain.size() ? physics.terrain[index] : b2_nullBodyId;
	}

	inline float SegmentBodyFraction(const olc::vf2d& start, const olc::vf2d& end,
		const ObjectState& body) {
		const olc::vf2d delta = end - start;
		const float length2 = delta.dot(delta);
		if (length2 < 0.0001f)
			return 1.0f;

		if (body.shape == Shape::Circle) {
			const olc::vf2d offset = start - body.position;
			const float along = offset.dot(delta);
			const float outside = offset.dot(offset) - body.radius * body.radius;
			if (outside <= 0.0f)
				return 0.0f;
			if (along >= 0.0f)
				return 1.0f;
			const float discriminant = along * along - length2 * outside;
			if (discriminant < 0.0f)
				return 1.0f;
			return std::clamp((-along - std::sqrt(discriminant)) / length2, 0.0f, 1.0f);
		}

		const Polygon outline = WorldVertices(body);
		if (outline.count < 3)
			return 1.0f;
		if (ContainsPoint(outline, start))
			return 0.0f;

		const olc::utils::geom2d::line<float> path(start, end);
		float nearest = 1.0f;
		for (int i = 0; i < outline.count; ++i) {
			const olc::utils::geom2d::line<float> edge(outline.vertices[i],
				outline.vertices[(i + 1) % outline.count]);
			for (const olc::vf2d& hit : olc::utils::geom2d::intersects(edge, path))
				nearest = std::min(nearest,
					std::clamp((hit - start).dot(delta) / length2, 0.0f, 1.0f));
		}
		return nearest;
	}

	inline bool SegmentRectFraction(const olc::vf2d& start, const olc::vf2d& end,
		const Terrain& rect, float& fraction) {
		const olc::vf2d delta = end - start;
		const olc::vf2d low = rect.pos;
		const olc::vf2d high = rect.pos + rect.size;
		float nearFraction = 0.0f;
		float farFraction = 1.0f;
		for (int axis = 0; axis < 2; ++axis) {
			const float origin = axis == 0 ? start.x : start.y;
			const float direction = axis == 0 ? delta.x : delta.y;
			const float lo = axis == 0 ? low.x : low.y;
			const float hi = axis == 0 ? high.x : high.y;
			if (std::abs(direction) < 0.0001f) {
				if (origin < lo || origin > hi)
					return false;
				continue;
			}
			float axisNear = (lo - origin) / direction;
			float axisFar = (hi - origin) / direction;
			if (axisNear > axisFar)
				std::swap(axisNear, axisFar);
			nearFraction = std::max(nearFraction, axisNear);
			farFraction = std::min(farFraction, axisFar);
			if (nearFraction > farFraction)
				return false;
		}
		if (farFraction < 0.0f || nearFraction > 1.0f)
			return false;
		fraction = std::max(nearFraction, 0.0f);
		return true;
	}

	inline float ReachableFraction(const olc::vf2d& start, const olc::vf2d& end, float radius,
		const ObjectState* obstacles = nullptr, size_t obstacleCount = 0) {
		const olc::vf2d delta = end - start;
		const float length = delta.mag();
		if (length < 0.0001f)
			return 1.0f;

		const float length2 = length * length;
		const olc::utils::geom2d::line<float> path(start, end);
		float nearest = 1.0f;
		for (const Terrain& terrain : TestTerrain())
			for (const olc::vf2d& hit : olc::utils::geom2d::intersects(terrain, path))
				nearest = std::min(nearest, std::clamp((hit - start).dot(delta) / length2, 0.0f, 1.0f));
		for (size_t i = 0; i < obstacleCount; ++i)
			if (obstacles[i].radius > 0.0f)
				nearest = std::min(nearest, SegmentBodyFraction(start, end, obstacles[i]));

		if (nearest >= 1.0f)
			return 1.0f;
		return std::max(nearest - (radius + DropClearance) / length, 0.0f);
	}

	inline olc::vf2d ClampDropPosition(const olc::vf2d& from, const olc::vf2d& to, float radius,
		const ObjectState* obstacles = nullptr, size_t obstacleCount = 0) {
		return from + (to - from) * ReachableFraction(from, to, radius, obstacles, obstacleCount);
	}

	inline std::array<ObjectState, TestBodyCount> CreateTestBodies() {
		std::array<ObjectState, TestBodyCount> bodies{};
		bodies[0] = CreateBox({ 30.0f, 100.0f }, { 22.0f, 22.0f }, 0.3f, 0.05f);
		bodies[1] = CreateBox({ 30.0f, 40.0f }, { 22.0f, 22.0f }, 0.3f, -0.04f);
		bodies[2] = CreateBox({ 125.0f, 60.0f }, { 30.0f, 14.0f }, 0.2f, 0.7f);
		bodies[3] = CreateCircle({ 90.0f, -40.0f }, 18.0f, 0.5f);
		bodies[4] = CreateCircle({ 90.0f, -110.0f }, 12.0f, 0.75f);
		std::vector<olc::vf2d> hexagon;
		for (int i = 0; i < 6; ++i) {
			const float turn = static_cast<float>(i) * 1.0471976f;
			hexagon.push_back({ std::cos(turn) * 20.0f, std::sin(turn) * 20.0f });
		}
		bodies[5] = CreatePolygon({ -285.0f, -10.0f }, hexagon, 0.6f, 0.35f);
		return bodies;
	}
}
