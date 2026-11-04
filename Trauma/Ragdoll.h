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

	inline bool SameBody(b2BodyId first, b2BodyId second) {
		return first.index1 == second.index1 && first.world0 == second.world0
			&& first.generation == second.generation;
	}

	inline b2JointId CreateRevoluteJoint(PhysicsWorld& physics, b2BodyId first, b2BodyId second,
		const olc::vf2d& anchor) {
		b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
		def.base.bodyIdA = first;
		def.base.bodyIdB = second;
		const b2Transform firstTransform = b2Body_GetTransform(first);
		const b2Transform secondTransform = b2Body_GetTransform(second);
		def.base.localFrameA.p = { anchor.x - firstTransform.p.x, anchor.y - firstTransform.p.y };
		def.base.localFrameA.q = b2MakeRot(0.0f);
		def.base.localFrameB.p = { anchor.x - secondTransform.p.x, anchor.y - secondTransform.p.y };
		def.base.localFrameB.q = b2MakeRot(0.0f);
		def.base.collideConnected = false;
		def.base.constraintHertz = 0.0f;
		def.base.constraintDampingRatio = 1.0f;
		def.enableMotor = false;
		def.enableLimit = false;
		return b2CreateRevoluteJoint(physics.world, &def);
	}

	inline b2JointId CreateWeldJoint(PhysicsWorld& physics, b2BodyId first, b2BodyId second,
		const olc::vf2d& anchor) {
		b2WeldJointDef def = b2DefaultWeldJointDef();
		def.base.bodyIdA = first;
		def.base.bodyIdB = second;
		const b2Transform firstTransform = b2Body_GetTransform(first);
		const b2Transform secondTransform = b2Body_GetTransform(second);
		def.base.localFrameA.p = { anchor.x - firstTransform.p.x, anchor.y - firstTransform.p.y };
		def.base.localFrameA.q = b2MakeRot(0.0f);
		def.base.localFrameB.p = { anchor.x - secondTransform.p.x, anchor.y - secondTransform.p.y };
		def.base.localFrameB.q = b2MakeRot(0.0f);
		def.base.collideConnected = false;
		def.base.constraintHertz = 0.0f;
		def.base.constraintDampingRatio = 1.0f;
		def.linearHertz = 0.0f;
		def.angularHertz = 0.0f;
		def.linearDampingRatio = 1.0f;
		def.angularDampingRatio = 1.0f;
		return b2CreateWeldJoint(physics.world, &def);
	}

	inline PhysicsRig& EnsureRig(PhysicsWorld& physics, const RagdollState& ragdoll) {
		RagdollGroup(physics, &ragdoll);
		PhysicsRig* rig = nullptr;
		for (PhysicsRig& candidate : physics.rigs)
			if (candidate.key == &ragdoll) {
				rig = &candidate;
				break;
			}
		rig->seen = true;

		bool rebuilt = false;
		for (size_t i = 0; i < PartCount; ++i) {
			const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], rig->group);
			if (!b2Body_IsValid(rig->bodies[i]) || !SameBody(rig->bodies[i], body)) {
				rig->bodies[i] = body;
				rebuilt = true;
			}
		}

		if (!rebuilt && b2Joint_IsValid(rig->joints[0]))
			return *rig;

		for (b2JointId& grip : rig->grips) {
			if (b2Joint_IsValid(grip))
				b2DestroyJoint(grip);
			grip = b2_nullJointId;
		}
		rig->gripTargets = {};

		const auto& joints = RagdollJoints();
		for (size_t j = 0; j < joints.size(); ++j) {
			if (b2Joint_IsValid(rig->joints[j]))
				b2DestroyJoint(rig->joints[j]);
			rig->joints[j] = CreateRevoluteJoint(physics, rig->bodies[joints[j].first],
				rig->bodies[joints[j].second],
				(ragdoll.parts[joints[j].first].position + ragdoll.parts[joints[j].second].position) * 0.5f);
		}
		return *rig;
	}

	inline void DriveRagdoll(PhysicsWorld& physics, RagdollState& ragdoll) {
		const auto targets = RagdollTargets(ragdoll);
		PhysicsRig& rig = EnsureRig(physics, ragdoll);
		const olc::vf2d torsoVelocity = ragdoll.parts[Torso].velocity;
		const float torsoInverseMass = ragdoll.parts[Torso].inverseMass;
		olc::vf2d reaction;
		for (size_t i = 1; i < PartCount; ++i) {
			const b2BodyId body = rig.bodies[i];
			if (!b2Body_IsValid(body) || ragdoll.parts[i].inverseMass <= 0.0f)
				continue;
			const ObjectState& part = ragdoll.parts[i];
			olc::vf2d acceleration = (targets[i] - part.position) * LimbDriveStiffness
				- (part.velocity - torsoVelocity) * LimbDriveDamping;
			const float magnitude = acceleration.mag();
			if (magnitude > LimbDriveMaxAcceleration)
				acceleration *= LimbDriveMaxAcceleration / magnitude;
			const float mass = 1.0f / part.inverseMass;
			b2Body_ApplyForceToCenter(body, { acceleration.x * mass, acceleration.y * mass }, true);
			reaction -= acceleration * (torsoInverseMass / part.inverseMass);
		}
		const b2BodyId torsoBody = rig.bodies[Torso];
		if (b2Body_IsValid(torsoBody) && torsoInverseMass > 0.0f) {
			const float mass = 1.0f / torsoInverseMass;
			b2Body_ApplyForceToCenter(torsoBody, { reaction.x * mass, reaction.y * mass }, true);
		}
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

	inline bool AimingAtContact(const olc::vf2d& aim, const olc::vf2d& outwardNormal) {
		const float length = aim.mag();
		return length > 0.0001f && aim.dot(outwardNormal) < -0.25f * length;
	}

	inline void ResolveHandGrip(RagdollState& ragdoll, size_t arm, const std::vector<ObjectState*>& objects) {
		HandGrip& grip = ragdoll.grips[arm];
		ObjectState& hand = ragdoll.parts[arm == 0 ? LeftHand : RightHand];
		hand.angle = HandFacingAngle(ragdoll, arm);
		if (!(arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight) || !ragdoll.input.grab) {
			grip = {};
			return;
		}
		if (grip.target != 0)
			return;

		const auto& terrain = TestTerrain();
		for (size_t i = 0; i < terrain.size(); ++i) {
			const olc::vf2d closest = olc::utils::geom2d::closest(terrain[i], hand.position);
			const olc::vf2d offset = hand.position - closest;
			const float distance = offset.mag();
			if (distance > hand.radius + 0.5f || distance < 0.0001f)
				continue;
			if (!AimingAtContact(ragdoll.input.aim, offset / distance))
				continue;
			grip.target = static_cast<uint8_t>(i + 1);
			grip.anchor = closest;
			break;
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

	inline void SyncGripJoints(PhysicsWorld& physics, RagdollState& ragdoll,
		const std::vector<ObjectState*>& objects) {
		PhysicsRig& rig = EnsureRig(physics, ragdoll);
		const size_t terrainCount = TestTerrain().size();
		for (size_t arm = 0; arm < ragdoll.grips.size(); ++arm) {
			const HandGrip& grip = ragdoll.grips[arm];
			b2BodyId target = b2_nullBodyId;
			if (grip.target > 0) {
				if (grip.target <= terrainCount)
					target = TerrainBody(physics, grip.target - 1);
				else {
					const size_t index = grip.target - terrainCount - 1;
					if (index < objects.size())
						target = EnsureBody(physics, *objects[index]);
				}
			}

			const bool sameTarget = rig.gripTargets[arm] == grip.target;
			if (sameTarget && b2Joint_IsValid(rig.grips[arm]))
				continue;
			if (b2Joint_IsValid(rig.grips[arm]))
				b2DestroyJoint(rig.grips[arm]);
			rig.grips[arm] = b2_nullJointId;
			rig.gripTargets[arm] = grip.target;
			if (grip.target == 0 || !b2Body_IsValid(target))
				continue;
			if (!b2Body_IsValid(rig.bodies[arm == 0 ? LeftHand : RightHand]))
				continue;
			rig.grips[arm] = CreateWeldJoint(physics, rig.bodies[arm == 0 ? LeftHand : RightHand],
				target, ragdoll.parts[arm == 0 ? LeftHand : RightHand].position);
		}
	}

	inline void HoldGroundedFooting(PhysicsWorld& physics, RagdollState& ragdoll) {
		const int group = RagdollGroup(physics, &ragdoll);
		const bool idle = !ragdoll.ladder && std::abs(ragdoll.input.moveX) <= 0.01f;
		const bool feet = ragdoll.parts[LeftFoot].grounded || ragdoll.parts[RightFoot].grounded;
		const bool crawlContact = GetMovementState(ragdoll.input) == MovementState::Crawling
			&& (ragdoll.parts[Torso].grounded || ragdoll.parts[Hips].grounded
				|| ragdoll.parts[LeftHand].grounded || ragdoll.parts[RightHand].grounded);
		if (!idle || !(feet || crawlContact)) {
			ragdoll.footingAnchored = false;
			return;
		}

		for (size_t i = 0; i < PartCount; ++i) {
			const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], group);
			b2Vec2 velocity = b2Body_GetLinearVelocity(body);
			velocity.x = 0.0f;
			b2Body_SetLinearVelocity(body, velocity);
		}

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
		for (size_t i = 0; i < PartCount; ++i) {
			const b2BodyId body = EnsureBody(physics, ragdoll.parts[i], group);
			const b2Transform transform = b2Body_GetTransform(body);
			b2Body_SetTransform(body, { transform.p.x - slip, transform.p.y }, transform.q);
		}
	}
}
