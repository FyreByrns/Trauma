#pragma once
#include "Input.h"
#include "Items.h"
#include "Physics.h"
#include "Ragdoll.h"
#include "World.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Trauma {
	constexpr int MaxPlayers = 20;
	constexpr int SolverSubSteps = 4;

	inline void ApplyRagdollInput(PhysicsWorld& physics, RagdollState& ragdoll, const InputState& input) {
		ragdoll.input = input;
		const int group = RagdollGroup(physics, &ragdoll);
		if (input.hotbar < HotbarSlots)
			ragdoll.inventory.selected = input.hotbar;
		ragdoll.ladderDetachTime = std::max(0.0f, ragdoll.ladderDetachTime - FixedDeltaTime);
		const olc::vf2d position = ragdoll.parts[Torso].position;
		if (ragdoll.ladder && (input.jump || std::abs(input.moveX) > 0.01f)) {
			ragdoll.ladder = 0;
			ragdoll.ladderDetachTime = 0.3f;
			if (input.jump)
				for (size_t i = 0; i < PartCount; ++i) {
					const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], group);
					b2Body_SetLinearVelocity(body, { 0.0f, -JumpSpeed });
				}
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
			for (size_t i = 0; i < PartCount; ++i) {
				const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], group);
				b2Body_SetLinearVelocity(body, { speedX, speedY });
			}
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
				ragdoll.gaitPhase = std::fmod(ragdoll.gaitPhase + FixedDeltaTime * rate * std::min(std::abs(input.moveX), 1.0f), TwoPi);
			}
		}
		else if ((input.reachLeft || input.reachRight) && std::abs(input.aim.x) > 0.01f) {
			ragdoll.facing = input.aim.x > 0.0f ? 1.0f : -1.0f;
		}

		const b2BodyId torso = EnsureBody(physics, ragdoll.parts[Torso], group);
		const float torsoSpeed = b2Body_GetLinearVelocity(torso).x;
		const float targetSpeed = std::clamp(input.moveX, -1.0f, 1.0f) * MovementSpeed(movement);
		const float change = std::clamp(targetSpeed - torsoSpeed, -30.0f, 30.0f);
		for (size_t i = 0; i < PartCount; ++i) {
			const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], group);
			b2Vec2 velocity = b2Body_GetLinearVelocity(body);
			velocity.x += change;
			if (input.jump && grounded)
				velocity.y = std::min(velocity.y, -JumpSpeed);
			b2Body_SetLinearVelocity(body, velocity);
		}
	}

	inline void StepPhysics(PhysicsWorld& physics, const std::vector<RagdollState*>& ragdolls,
		const std::vector<ObjectState*>& objects, float deltaTime) {
		if (!b2World_IsValid(physics.world))
			ResetWorld(physics);

		for (RagdollState* ragdoll : ragdolls)
			EnsureRig(physics, *ragdoll);
		for (ObjectState* object : objects)
			EnsureBody(physics, *object);

		for (RagdollState* ragdoll : ragdolls) {
			for (size_t arm = 0; arm < ragdoll->grips.size(); ++arm)
				ResolveHandGrip(*ragdoll, arm, objects);
		}

		for (RagdollState* ragdoll : ragdolls)
			DriveRagdoll(physics, *ragdoll);
		for (RagdollState* ragdoll : ragdolls)
			SyncGripJoints(physics, *ragdoll, objects);

		b2World_Step(physics.world, deltaTime, SolverSubSteps);

		for (RagdollState* ragdoll : ragdolls)
			for (size_t i = 0; i < PartCount; ++i)
				SyncBody(ragdoll->parts[i],
					EnsureBody(physics, ragdoll->parts[i], RagdollGroup(physics, ragdoll)));
		for (ObjectState* object : objects)
			SyncBody(*object, EnsureBody(physics, *object));

		for (RagdollState* ragdoll : ragdolls)
			HoldGroundedFooting(physics, *ragdoll);

		PruneBodies(physics);
	}
}
