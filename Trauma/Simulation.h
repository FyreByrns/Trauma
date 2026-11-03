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
				ragdoll.gaitPhase = std::fmod(ragdoll.gaitPhase + FixedDeltaTime * rate * std::min(std::abs(input.moveX), 1.0f), TwoPi);
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
