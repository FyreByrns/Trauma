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

	constexpr float DropClearance = 2.0f;

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

	inline ObjectState StaticTerrain(const Terrain& terrain) {
		const olc::vf2d half = terrain.size * 0.5f;
		return CreatePolygon(terrain.middle(), {
			{ -half.x, -half.y }, { half.x, -half.y },
			{ half.x, half.y }, { -half.x, half.y } }, 0.0f);
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

}
