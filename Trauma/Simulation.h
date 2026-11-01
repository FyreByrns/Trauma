#pragma once
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Geometry2D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Trauma {
	constexpr int MaxPlayers = 20;
	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float Gravity = 1600.0f;
	constexpr float MoveSpeed = 220.0f;
	constexpr float JumpSpeed = 520.0f;
	constexpr float PlayerRadius = 10.0f;
	constexpr size_t TestBodyCount = 3;

	struct InputState {
		float moveX = 0.0f;
		bool jump = false;
	};

	struct ObjectState {
		olc::vf2d position;
		olc::vf2d velocity;
		float radius = PlayerRadius;
		float inverseMass = 1.0f;
		bool grounded = false;
		uint32_t lastInput = 0;
	};

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

	inline std::array<ObjectState, TestBodyCount> CreateTestBodies() {
		std::array<ObjectState, TestBodyCount> bodies{};
		bodies[0].position = {20.0f, 0.0f};
		bodies[0].radius = 16.0f;
		bodies[0].inverseMass = 0.5f;
		bodies[1].position = {75.0f, -50.0f};
		bodies[1].radius = 24.0f;
		bodies[1].inverseMass = 0.25f;
		bodies[2].position = {125.0f, -110.0f};
		bodies[2].radius = 12.0f;
		bodies[2].inverseMass = 0.75f;
		return bodies;
	}

	inline void ApplyPlayerInput(ObjectState& object, const InputState& input) {
		object.velocity.x = std::clamp(input.moveX, -1.0f, 1.0f) * MoveSpeed;
		if (input.jump && object.grounded)
			object.velocity.y = -JumpSpeed;
	}

	inline void ResolveTerrain(ObjectState& object, const Terrain& terrain) {
		namespace geom = olc::utils::geom2d;
		const geom::circle<float> circle{object.position, object.radius};
		if (!geom::overlaps(circle, terrain))
			return;

		const olc::vf2d closest = geom::closest(terrain, object.position);
		const bool inside = geom::contains(terrain, object.position);
		const olc::vf2d offset = inside ? closest - object.position : object.position - closest;
		const float distance = offset.mag();
		olc::vf2d normal;
		if (distance > 0.0001f)
			normal = offset / distance;
		else if (closest.y == terrain.pos.y)
			normal = {0.0f, -1.0f};
		else if (closest.y == terrain.pos.y + terrain.size.y)
			normal = {0.0f, 1.0f};
		else if (closest.x == terrain.pos.x)
			normal = {-1.0f, 0.0f};
		else
			normal = {1.0f, 0.0f};

		object.position += normal * (inside ? object.radius + distance : object.radius - distance);
		const float speed = object.velocity.dot(normal);
		if (speed < 0.0f) {
			const float restitution = speed < -70.0f ? 0.15f : 0.0f;
			object.velocity -= normal * ((1.0f + restitution) * speed);
			const olc::vf2d tangent = normal.perp();
			const float friction = std::clamp(object.velocity.dot(tangent), -0.3f * -speed, 0.3f * -speed);
			object.velocity -= tangent * friction;
		}
		if (normal.y < -0.5f)
			object.grounded = true;
	}

	inline void ResolveBodies(ObjectState& first, ObjectState& second) {
		namespace geom = olc::utils::geom2d;
		if (!geom::overlaps(geom::circle<float>{first.position, first.radius},
			geom::circle<float>{second.position, second.radius}))
			return;

		const float inverseMass = first.inverseMass + second.inverseMass;
		if (inverseMass <= 0.0f)
			return;

		const olc::vf2d offset = second.position - first.position;
		const float distance = offset.mag();
		const olc::vf2d normal = distance > 0.0001f ? offset / distance : olc::vf2d{1.0f, 0.0f};
		const float penetration = first.radius + second.radius - distance;
		first.position -= normal * (penetration * first.inverseMass / inverseMass);
		second.position += normal * (penetration * second.inverseMass / inverseMass);

		const float speed = (second.velocity - first.velocity).dot(normal);
		if (speed < 0.0f) {
			const float restitution = speed < -70.0f ? 0.15f : 0.0f;
			const float impulse = -(1.0f + restitution) * speed / inverseMass;
			first.velocity -= normal * (impulse * first.inverseMass);
			second.velocity += normal * (impulse * second.inverseMass);
		}
		if (normal.y > 0.5f)
			first.grounded = true;
		if (normal.y < -0.5f)
			second.grounded = true;
	}

	inline void StepPhysics(const std::vector<ObjectState*>& bodies, float deltaTime) {
		constexpr int Substeps = 4;
		constexpr int SolverIterations = 4;
		const float step = deltaTime / Substeps;
		for (ObjectState* body : bodies)
			body->grounded = false;

		for (int substep = 0; substep < Substeps; ++substep) {
			for (ObjectState* body : bodies) {
				if (body->inverseMass <= 0.0f)
					continue;
				body->velocity.y += Gravity * step;
				body->position += body->velocity * step;
			}

			for (int iteration = 0; iteration < SolverIterations; ++iteration) {
				for (ObjectState* body : bodies) {
					if (body->inverseMass > 0.0f)
						for (const Terrain& terrain : TestTerrain())
							ResolveTerrain(*body, terrain);
				}
				for (size_t first = 0; first < bodies.size(); ++first)
					for (size_t second = first + 1; second < bodies.size(); ++second)
						ResolveBodies(*bodies[first], *bodies[second]);
			}
		}
	}

	inline void StepPlayer(ObjectState& object, const InputState& input, float deltaTime) {
		ApplyPlayerInput(object, input);
		StepPhysics({&object}, deltaTime);
	}
}
