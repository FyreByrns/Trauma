#pragma once
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Geometry2D.h"
#include "IK.h"
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
		bool reachLeft = false;
		bool reachRight = false;
		olc::vf2d aim = { 1.0f, 0.0f };
		bool run = false;
		bool crouch = false;
		bool crawl = false;
		float climb = 0.0f;
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

	struct ObjectState {
		olc::vf2d position;
		olc::vf2d velocity;
		float radius = PlayerRadius;
		float inverseMass = 1.0f;
		bool grounded = false;
		uint32_t lastInput = 0;
	};

	enum RagdollPart : size_t {
		Torso, Head, Hips, LeftElbow, LeftHand, RightElbow, RightHand,
		LeftKnee, LeftFoot, RightKnee, RightFoot, PartCount
	};

	struct HandGrip {
		uint8_t target = 0;
		olc::vf2d anchor;
	};

	struct RagdollState {
		std::array<ObjectState, PartCount> parts;
		std::array<HandGrip, 2> grips{};
		InputState input;
		float facing = 1.0f;
		float gaitPhase = 0.0f;
		uint8_t ladder = 0;
		float ladderDetachTime = 0.0f;
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
				ResolveTerrain(contact, terrain[i]);
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
				if ((hand.position - objects[i]->position).mag() <= hand.radius + objects[i]->radius + 0.5f) {
					const olc::vf2d offset = hand.position - objects[i]->position;
					const float distance = offset.mag();
					const olc::vf2d normal = distance > 0.0001f ? offset / distance : olc::vf2d{0.0f, -1.0f};
					if (!AimingAtContact(ragdoll.input.aim, normal))
						continue;
					grip.target = static_cast<uint8_t>(terrain.size() + i + 1);
					grip.anchor = normal * (hand.radius + objects[i]->radius);
				}
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
		const olc::vf2d correction = (object.position + grip.anchor - hand.position) / mass;
		hand.position += correction * hand.inverseMass;
		object.position -= correction * object.inverseMass;
		const olc::vf2d impulse = (object.velocity - hand.velocity) / mass;
		hand.velocity += impulse * hand.inverseMass;
		object.velocity -= impulse * object.inverseMass;
	}

	inline void StepRagdollPhysics(const std::vector<RagdollState*>& ragdolls, const std::vector<ObjectState*>& objects, float deltaTime) {
		constexpr int Substeps = 4;
		constexpr int SolverIterations = 6;
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
			for (RagdollState* ragdoll : ragdolls)
				for (size_t arm = 0; arm < ragdoll->grips.size(); ++arm)
					ResolveHandGrip(*ragdoll, arm, objects);
			for (RagdollState* ragdoll : ragdolls)
				DriveRagdoll(*ragdoll, step);
			for (size_t i = 0; i < bodies.size(); ++i) {
				ObjectState* body = bodies[i];
				if (body->inverseMass <= 0.0f)
					continue;
				if (owners[i] >= ragdolls.size() || !ragdolls[owners[i]]->ladder)
					body->velocity.y += Gravity * step;
				body->position += body->velocity * step;
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
						for (const Terrain& terrain : TestTerrain())
							ResolveTerrain(*body, terrain);
				for (size_t first = 0; first < bodies.size(); ++first)
					for (size_t second = first + 1; second < bodies.size(); ++second)
						if (owners[first] != owners[second])
							ResolveBodies(*bodies[first], *bodies[second]);
				for (RagdollState* ragdoll : ragdolls)
					for (size_t arm = 0; arm < ragdoll->grips.size(); ++arm)
						ResolveHandGrip(*ragdoll, arm, objects);
			}
		}
	}
}
