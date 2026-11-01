#pragma once
#include "olcPixelGameEngine3.h"
#include <algorithm>
#include <cstdint>

namespace Trauma {
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

	constexpr uint8_t MaxPlayers = 16;
	constexpr float ArenaHalfWidth = 1400.0f;
	constexpr float ArenaHalfHeight = 600.0f;
	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float PlayerHalfWidth = 6.0f;
	constexpr float PlayerHalfHeight = 15.0f;
	constexpr float MoveSpeed = 220.0f;
	constexpr float JumpSpeed = 520.0f;
	constexpr float Gravity = 1600.0f;

	inline void StepPhysics(ObjectState& object, const InputState& input, float deltaTime) {
		object.velocity.x = std::clamp(input.moveX, -1.0f, 1.0f) * MoveSpeed;

		if (input.jump && object.grounded)
			object.velocity.y = -JumpSpeed;

		object.velocity.y += Gravity * deltaTime;
		object.grounded = false;

		object.position.x += object.velocity.x * deltaTime;
		
		// 
	}
}
