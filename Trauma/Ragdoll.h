#pragma once
#include "IK.h"
#include "Input.h"
#include "Items.h"
#include "Physics.h"
#include "World.h"
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Geometry2D.h"
#include <array>
#include <cmath>
#include <vector>

namespace Trauma {
	constexpr float GripSpringStiffness = 14.0f;
	constexpr float GripSpringDamping = 0.5f;
	constexpr float GripMaxSpringSpeed = 25.0f;
	constexpr float FootingSlipLimit = 2.0f;

	constexpr float LimbDriveStiffness = 180.0f;
	constexpr float LimbDriveDamping = 24.0f;
	constexpr float LimbDriveMaxAcceleration = 5000.0f;

	enum RagdollPart : size_t {
		Torso, Head, Hips, LeftElbow, LeftHand, RightElbow, RightHand,
		LeftKnee, LeftFoot, RightKnee, RightFoot, PartCount
	};

	struct HandGrip {
		uint8_t target = 0;
		olc::vf2d anchor;
		float angleOffset = 0.0f;
	};


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
			const olc::vf2d headOffset{ 14.0f * ragdoll.facing, -2.0f };
			const olc::vf2d hipOffset{ -12.0f * ragdoll.facing, 0.0f };
			targets[Head] = torso + headOffset * (RagdollJoints()[0].length / headOffset.mag());
			targets[Hips] = torso + hipOffset * (RagdollJoints()[1].length / hipOffset.mag());
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
			olc::vf2d acceleration = (targets[i] - part.position) * LimbDriveStiffness - (part.velocity - torsoVelocity) * LimbDriveDamping;
			const float magnitude = acceleration.mag();
			if (magnitude > LimbDriveMaxAcceleration)
				acceleration *= LimbDriveMaxAcceleration / magnitude;
			part.velocity += acceleration * deltaTime;
			reaction -= acceleration * (torsoPart.inverseMass / part.inverseMass);
		}
		torsoPart.velocity += reaction * deltaTime;
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

	inline bool HandsOnItem(const RagdollState& ragdoll, const ObjectState& body) {
		const std::array<RagdollPart, 2> hands = { LeftHand, RightHand };
		const std::array<bool, 2> reaching = { ragdoll.input.reachLeft, ragdoll.input.reachRight };
		for (size_t hand = 0; hand < hands.size(); ++hand) {
			if (!reaching[hand])
				continue;
			const ObjectState& part = ragdoll.parts[hands[hand]];
			if ((ClosestOnObject(body, part.position) - part.position).mag() <= part.radius + 0.5f)
				return true;
		}
		return false;
	}

	inline olc::vf2d DropHandOrigin(const RagdollState& ragdoll, const olc::vf2d& target) {
		const olc::vf2d left = ragdoll.parts[LeftHand].position;
		const olc::vf2d right = ragdoll.parts[RightHand].position;
		if (ragdoll.input.reachLeft != ragdoll.input.reachRight)
			return ragdoll.input.reachLeft ? left : right;
		return (left - target).mag2() <= (right - target).mag2() ? left : right;
	}

	inline float HandFacingAngle(const RagdollState& ragdoll, size_t arm) {
		const ObjectState& elbow = ragdoll.parts[arm == 0 ? LeftElbow : RightElbow];
		const ObjectState& hand = ragdoll.parts[arm == 0 ? LeftHand : RightHand];
		const olc::vf2d direction = hand.position - elbow.position;
		if (direction.mag2() < 0.0001f)
			return hand.angle;
		return std::atan2(direction.y, direction.x);
	}

	inline void ResolveHandGrip(RagdollState& ragdoll, size_t arm, const std::vector<ObjectState*>& objects) {
		HandGrip& grip = ragdoll.grips[arm];
		ObjectState& hand = ragdoll.parts[arm == 0 ? LeftHand : RightHand];
		hand.angle = HandFacingAngle(ragdoll, arm);
		if (!(arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight)) {
			grip = {};
			return;
		}

		if (!ragdoll.input.grab) {
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
				grip.angleOffset = std::remainder(objects[i]->angle - hand.angle, TwoPi);
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
		Wake(object);
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
		const float error = std::remainder(hand.angle + grip.angleOffset - object.angle, TwoPi);
		const float relativeSpin = object.angularVelocity - hand.angularVelocity;
		const float springSpeed = std::clamp(error * GripSpringStiffness, -GripMaxSpringSpeed, GripMaxSpringSpeed);
		const float response = (springSpeed - relativeSpin) * GripSpringDamping / angularMass;
		object.angularVelocity += response * object.inverseInertia;
		hand.angularVelocity -= response * hand.inverseInertia;
	}

	inline void HoldGroundedFooting(RagdollState& ragdoll) {
		const bool idle = !ragdoll.ladder && std::abs(ragdoll.input.moveX) <= 0.01f;
		const bool feet = ragdoll.parts[LeftFoot].grounded || ragdoll.parts[RightFoot].grounded;
		const bool crawlContact = GetMovementState(ragdoll.input) == MovementState::Crawling
			&& (ragdoll.parts[Torso].grounded || ragdoll.parts[Hips].grounded
				|| ragdoll.parts[LeftHand].grounded || ragdoll.parts[RightHand].grounded);
		if (!idle || !(feet || crawlContact)) {
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


}
