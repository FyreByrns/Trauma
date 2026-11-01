#pragma once
#include "olcPixelGameEngine3.h"
#include <algorithm>
#include <cstdint>

namespace Trauma {
	constexpr int MaxPlayers = 20;

	struct InputState {
		float moveX = 0.0f;
		bool jump = false;
	};

	struct ObjectState {
		olc::vf2d position;
		olc::vf2d velocity;
		bool grounded = false;
		uint32_t lastInput = 0;
	};

	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float Gravity = 1600.0f;

	inline void StepPhysics(ObjectState& object, const InputState& input, float deltaTime) {
		object.velocity.y += Gravity * deltaTime;
		object.grounded = false;
		object.position.x += object.velocity.x * deltaTime;

		// collision resolution

		// collision response
	}
}
