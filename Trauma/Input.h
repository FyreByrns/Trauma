#pragma once
#include "olcPixelGameEngine3.h"
#include <cstdint>

namespace Trauma {
	constexpr float MoveSpeed = 220.0f;
	constexpr float JumpSpeed = 520.0f;
	constexpr float BackwardMoveFactor = 0.65f;
	constexpr float AimingRunMoveFactor = 0.45f;
	constexpr float BackwardGaitFactor = 0.85f;

	struct InputState {
		float moveX = 0.0f;
		bool jump = false;
		bool reachLeft = false;
		bool reachRight = false;
		bool grab = true;
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

}
