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
	constexpr float PoseVelocityGain = 12.0f;
	constexpr float PoseMaxVelocity = 700.0f;
	constexpr float PoseMaxForce = 12000.0f;
	constexpr float HipsPoseMaxForce = PoseMaxForce * 4.0f;
	constexpr float WalkArmBob = 2.0f;
	constexpr float RunArmBob = 4.5f;
	constexpr float AimingArmBobScale = 1.2f;
	constexpr float BackwardArmBobScale = 0.7f;
	constexpr float AimBobPerPixel = 0.012f;

	constexpr float GripSpringHertz = 20.0f;
	constexpr float GripSpringDamping = 1.0f;
	constexpr float GripConstraintHertz = 60.0f;

	constexpr float JointBendTolerance = 0.12f;
	constexpr float JointMaxBend = 2.9f;
	constexpr float JointLimitGain = 250.0f;
	constexpr float JointLimitMaxImpulse = 200.0f;

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
		float crouchTurn = 0.0f;
		float gaitPhase = 0.0f;
		uint8_t ladder = 0;
		float ladderDetachTime = 0.0f;
		float gunRecoil = 0.0f;
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

	inline uint8_t ItemHandMask(const RagdollState& ragdoll, ItemId item) {
		const HandControl control = ItemHandControl(item);
		if (control == HandControl::None)
			return 0;
		if (control == HandControl::Left)
			return 1;
		if (control == HandControl::Right)
			return 2;
		const uint8_t free = static_cast<uint8_t>((ragdoll.grips[0].target == 0 ? 1 : 0)
			| (ragdoll.grips[1].target == 0 ? 2 : 0));
		if (ItemHandCount(item) >= 2)
			return free == 3 ? 3 : 0;
		if ((free & 2) != 0)
			return 2;
		return static_cast<uint8_t>(free & 1);
	}

	inline size_t ItemPrimaryArm(uint8_t mask) {
		return (mask & 2) != 0 ? 1 : 0;
	}

	inline ItemId SelectedHotbarItem(const RagdollState& ragdoll) {
		const size_t selected = ragdoll.inventory.selected;
		return selected < ragdoll.inventory.hotbar.size()
			? ragdoll.inventory.hotbar[selected].item : ItemNone;
	}

	inline bool IsGunAiming(const RagdollState& ragdoll) {
		return ragdoll.input.reachRight && IsGun(SelectedHotbarItem(ragdoll));
	}

	inline bool GunFollowsHand(const RagdollState& ragdoll) {
		return ragdoll.ladder != 0
			|| (!ragdoll.input.reachRight
				&& GetMovementState(ragdoll.input) == MovementState::Crawling);
	}

	inline bool MovingBackward(const RagdollState& ragdoll) {
		return std::abs(ragdoll.input.moveX) > 0.01f
			&& ragdoll.input.moveX * ragdoll.facing < 0.0f;
	}

	inline float ArmBob(const RagdollState& ragdoll) {
		const MovementState movement = GetMovementState(ragdoll.input);
		if (ragdoll.ladder != 0 || movement == MovementState::Crawling)
			return 0.0f;
		const float stride = std::clamp(std::abs(ragdoll.input.moveX), 0.0f, 1.0f);
		if (stride < 0.01f)
			return 0.0f;
		const float amplitude = movement == MovementState::Running ? RunArmBob : WalkArmBob;
		const float aiming = IsGunAiming(ragdoll) ? AimingArmBobScale : 1.0f;
		const float backward = MovingBackward(ragdoll) ? BackwardArmBobScale : 1.0f;
		return std::sin(ragdoll.gaitPhase * 2.0f) * amplitude * stride * aiming * backward;
	}

	inline float AimBob(const RagdollState& ragdoll) {
		if (GunFollowsHand(ragdoll))
			return 0.0f;
		return ArmBob(ragdoll) * AimBobPerPixel;
	}

	inline olc::vf2d HeldItemDirection(const std::array<olc::vf2d, PartCount>& pose,
		const InputState& input, float facing, bool leftHand, ItemId item, float recoil = 0.0f,
		bool followHand = false, float aimBob = 0.0f) {
		const bool gun = IsGun(item);
		olc::vf2d direction = gun && !followHand ? input.aim
			: pose[leftHand ? LeftHand : RightHand] - pose[leftHand ? LeftElbow : RightElbow];
		if (direction.mag2() < 0.0001f)
			direction = { facing, 0.0f };
		const float angle = std::atan2(direction.y, direction.x)
			+ (gun ? recoil + (followHand ? 0.0f : aimBob) : 0.0f);
		return Rotate({ 1.0f, 0.0f }, angle);
	}

	inline float HeldItemCenterOffset(ItemId item) {
		const ItemBodyDefinition& body = ItemDefinitionFor(item).body;
		if (body.shape == ItemBodyShape::Box)
			return body.halfExtents.x * 0.7f;
		if (body.shape == ItemBodyShape::Circle)
			return body.radius * 0.5f;
		return 0.0f;
	}

	inline float HeldItemMuzzleOffset(ItemId item) {
		const ItemBodyDefinition& body = ItemDefinitionFor(item).body;
		if (body.shape == ItemBodyShape::Box)
			return body.halfExtents.x;
		if (body.shape == ItemBodyShape::Circle)
			return body.radius;
		return 0.0f;
	}

	struct GunBarrelRay {
		olc::vf2d origin;
		olc::vf2d direction;
	};

	struct GunBarrelRays {
		std::array<GunBarrelRay, MaxGunBarrels> rays{};
		size_t count = 0;
	};

	inline GunBarrelRays ComputeGunBarrelRays(const RagdollState& ragdoll, size_t hotbar) {
		GunBarrelRays result;
		if (hotbar >= ragdoll.inventory.hotbar.size())
			return result;
		const ItemId item = ragdoll.inventory.hotbar[hotbar].item;
		if (!IsGun(item))
			return result;
		const uint8_t handMask = ItemHandMask(ragdoll, item);
		if (handMask == 0)
			return result;

		std::array<olc::vf2d, PartCount> pose;
		for (size_t i = 0; i < PartCount; ++i)
			pose[i] = ragdoll.parts[i].position;
		const bool leftHand = ItemPrimaryArm(handMask) == 0;
		const olc::vf2d unit = HeldItemDirection(pose, ragdoll.input, ragdoll.facing,
			leftHand, item, ragdoll.gunRecoil, GunFollowsHand(ragdoll), AimBob(ragdoll));
		const olc::vf2d hand = pose[leftHand ? LeftHand : RightHand];
		const olc::vf2d center = hand + unit * HeldItemCenterOffset(item);
		const float bodyAngle = std::atan2(unit.y, unit.x);
		const GunPoseDefinition& gunPose = ItemDefinitionFor(item).gunPose;
		if (gunPose.barrelCount == 0) {
			result.count = 1;
			result.rays[0].origin = center + Rotate({ HeldItemMuzzleOffset(item), 0.0f }, bodyAngle);
			result.rays[0].direction = unit;
			return result;
		}

		result.count = std::min<size_t>(gunPose.barrelCount, MaxGunBarrels);
		for (size_t i = 0; i < result.count; ++i) {
			result.rays[i].origin = center + Rotate(gunPose.barrels[i].position, bodyAngle);
			result.rays[i].direction = Rotate({ 1.0f, 0.0f },
				bodyAngle + gunPose.barrels[i].angle);
		}
		return result;
	}

	inline float GunRecoilKick(const RagdollState& ragdoll, ItemId item) {
		if (!IsGun(item))
			return 0.0f;
		const GunRecoilDefinition& recoil = ItemDefinitionFor(item).gunPose.recoil;
		if (std::abs(recoil.amount) < 0.000001f)
			return 0.0f;
		const uint8_t handMask = ItemHandMask(ragdoll, item);
		if (handMask == 0)
			return 0.0f;

		std::array<olc::vf2d, PartCount> pose;
		for (size_t i = 0; i < PartCount; ++i)
			pose[i] = ragdoll.parts[i].position;
		const bool leftHand = ItemPrimaryArm(handMask) == 0;
		const olc::vf2d base = HeldItemDirection(pose, ragdoll.input, ragdoll.facing, leftHand,
			item, 0.0f, GunFollowsHand(ragdoll), AimBob(ragdoll));
		const float cross = base.x * recoil.direction.y - base.y * recoil.direction.x;
		return recoil.amount * cross;
	}

	inline void ApplyGunRecoil(RagdollState& ragdoll, size_t hotbar) {
		if (hotbar >= ragdoll.inventory.hotbar.size())
			return;
		ragdoll.gunRecoil += GunRecoilKick(ragdoll, ragdoll.inventory.hotbar[hotbar].item);
	}

	inline void StepGunRecoil(RagdollState& ragdoll) {
		const size_t selected = ragdoll.inventory.selected;
		if (selected >= ragdoll.inventory.hotbar.size()) {
			ragdoll.gunRecoil = 0.0f;
			return;
		}
		const ItemId item = ragdoll.inventory.hotbar[selected].item;
		if (!IsGun(item)) {
			ragdoll.gunRecoil = 0.0f;
			return;
		}
		if (ItemHandMask(ragdoll, item) == 0) {
			ragdoll.gunRecoil = 0.0f;
			return;
		}
		const bool aiming = ragdoll.input.reachRight;
		const GunRecoilDefinition& recoil = ItemDefinitionFor(item).gunPose.recoil;
		const float recovery = aiming ? recoil.aimingRecovery : recoil.lowReadyRecovery;
		ragdoll.gunRecoil *= std::exp(-std::max(0.0f, recovery) * FixedDeltaTime);
		if (std::abs(ragdoll.gunRecoil) < 0.00001f)
			ragdoll.gunRecoil = 0.0f;
	}

	inline std::array<olc::vf2d, PartCount> RagdollTargets(const RagdollState& ragdoll) {
		const float facing = ragdoll.facing;
		const olc::vf2d torso = ragdoll.parts[Torso].position;
		const MovementState movement = GetMovementState(ragdoll.input);
		const float stride = std::clamp(std::abs(ragdoll.input.moveX), 0.0f, 1.0f);
		const bool backward = MovingBackward(ragdoll);
		const float travel = backward && movement != MovementState::Crawling ? -1.0f : 1.0f;
		const float swing = std::sin(ragdoll.gaitPhase) * stride * travel;
		const float armBob = ArmBob(ragdoll);
		const olc::vf2d armBobOffset = { 0.0f, armBob };

		std::array<olc::vf2d, PartCount> targets;
		for (size_t i = 0; i < PartCount; ++i) {
			const olc::vf2d offset = RagdollOffsets()[i];
			targets[i] = torso + olc::vf2d{ offset.x * facing, offset.y };
		}

		if (ragdoll.ladder) {
			const Terrain& ladder = TestLadders()[ragdoll.ladder - 1];
			const float rungX = ladder.pos.x + ladder.size.x * 0.5f;
			const int rungCount = std::max(1, static_cast<int>(ladder.size.y / LadderRungSpacing));
			const int base = static_cast<int>(std::floor((torso.y - ladder.pos.y) / LadderRungSpacing));
			const bool leftStep = std::sin(ragdoll.gaitPhase) >= 0.0f;
			targets[Head] = torso + olc::vf2d{ 0.0f, -14.0f };
			targets[Hips] = torso + olc::vf2d{ 0.0f, 12.0f };
			const int hipRung = static_cast<int>(std::floor((targets[Hips].y - ladder.pos.y) / LadderRungSpacing));
			for (size_t limb = 0; limb < 2; ++limb) {
				const bool reaching = limb == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight;
				const bool armStep = limb == 0 ? leftStep : !leftStep;
				const bool legStep = !armStep;
				const size_t armJoint = limb == 0 ? 2 : 4;
				const size_t legJoint = limb == 0 ? 6 : 8;
				const RagdollPart elbow = limb == 0 ? LeftElbow : RightElbow;
				const RagdollPart hand = limb == 0 ? LeftHand : RightHand;
				const RagdollPart knee = limb == 0 ? LeftKnee : RightKnee;
				const RagdollPart foot = limb == 0 ? LeftFoot : RightFoot;
				const int handRung = std::clamp(base - (armStep ? 1 : 0), 0, rungCount);
				const int footRung = std::clamp(hipRung + 2 + (legStep ? 0 : 1), 0, rungCount);
				const olc::vf2d handTarget = reaching ? torso + ragdoll.input.aim
					: olc::vf2d{ rungX, ladder.pos.y + static_cast<float>(handRung) * LadderRungSpacing };
				const TwoBoneIkResult arm = SolveTwoBoneIk(torso, handTarget,
					RagdollJoints()[armJoint].length, RagdollJoints()[armJoint + 1].length, facing);
				const TwoBoneIkResult leg = SolveTwoBoneIk(targets[Hips],
					{ rungX, ladder.pos.y + static_cast<float>(footRung) * LadderRungSpacing },
					RagdollJoints()[legJoint].length, RagdollJoints()[legJoint + 1].length, -facing);
				targets[elbow] = arm.joint;
				targets[hand] = arm.end;
				targets[knee] = leg.joint;
				targets[foot] = leg.end;
			}
			return targets;
		}

		if (movement == MovementState::Crawling) {
			targets[Head] = torso + olc::vf2d{ 14.0f * facing, 3.0f };
			targets[Hips] = torso + olc::vf2d{ -12.0f * facing, -2.0f };
		}

		const float legHeight = movement == MovementState::Running ? 23.0f
			: movement == MovementState::Crouching
				? 13.0f + 11.0f * std::clamp(ragdoll.crouchTurn / 0.15f, 0.0f, 1.0f) : 24.0f;
		const float strideLength = movement == MovementState::Running ? 17.0f
			: movement == MovementState::Crouching ? 5.0f : 11.0f;
		const float lift = movement == MovementState::Running ? 9.0f
			: movement == MovementState::Crouching ? 2.0f : 6.0f;
		const float armSwing = movement == MovementState::Running ? 11.0f
			: movement == MovementState::Crouching ? 2.0f : 7.0f;
		const ItemId selectedItem = ragdoll.inventory.selected < ragdoll.inventory.hotbar.size()
			? ragdoll.inventory.hotbar[ragdoll.inventory.selected].item : ItemNone;
		const bool holdingGun = IsGun(selectedItem);
		const uint8_t heldMask = holdingGun ? ItemHandMask(ragdoll, selectedItem) : 0;
		const size_t heldArm = heldMask != 0 ? ItemPrimaryArm(heldMask) : PartCount;
		const bool gunAiming = holdingGun && ragdoll.input.reachRight;
		const GunPoseDefinition& gunPose = ItemDefinitionFor(selectedItem).gunPose;
		const olc::vf2d aimDirection = ragdoll.input.aim.mag2() > 0.0001f
			? ragdoll.input.aim.norm() : olc::vf2d{ facing, 0.0f };
		const float aimAngle = std::atan2(aimDirection.y, aimDirection.x)
			+ (holdingGun ? ragdoll.gunRecoil + AimBob(ragdoll) : 0.0f);

		const olc::vf2d hip = targets[Hips];
		for (int leg = 0; leg < 2; ++leg) {
			const float phase = leg == 0 ? swing : -swing;
			const float swingLift = leg == 0 ? std::cos(ragdoll.gaitPhase) : -std::cos(ragdoll.gaitPhase);
			const size_t joint = leg == 0 ? 6 : 8;
			const RagdollPart knee = leg == 0 ? LeftKnee : RightKnee;
			const RagdollPart foot = leg == 0 ? LeftFoot : RightFoot;
			olc::vf2d offset;
			if (movement == MovementState::Crawling)
				offset = olc::vf2d{ (-18.0f + phase * 7.0f) * facing, 7.0f - std::max(0.0f, phase) * 3.0f };
			else
				offset = olc::vf2d{
					(phase * strideLength + (leg == 0 ? -2.0f : 2.0f)) * facing,
					legHeight - std::max(0.0f, swingLift) * lift
				};
			const TwoBoneIkResult pose = SolveTwoBoneIk(hip, hip + offset,
				RagdollJoints()[joint].length, RagdollJoints()[joint + 1].length, -facing);
			targets[knee] = pose.joint;
			targets[foot] = pose.end;
		}

		for (size_t arm = 0; arm < 2; ++arm) {
			const bool reaching = arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight;
			const size_t joint = arm == 0 ? 2 : 4;
			const RagdollPart elbow = arm == 0 ? LeftElbow : RightElbow;
			const RagdollPart hand = arm == 0 ? LeftHand : RightHand;
			const float phase = arm == 0 ? -swing : swing;
			olc::vf2d offset;
			if (gunAiming && arm == heldArm)
				offset = Rotate(gunPose.aimingHand, aimAngle) + armBobOffset;
			else if (gunAiming && heldMask == 3)
				offset = Rotate(gunPose.aimingHand, aimAngle) - Rotate({ 6.0f, 0.0f }, aimAngle)
					+ armBobOffset;
			else if (movement == MovementState::Crawling)
				offset = olc::vf2d{ (12.0f + phase * 5.0f) * facing, 10.0f - std::max(0.0f, phase) * 2.0f };
			else if (arm == heldArm)
				offset = Rotate(gunPose.lowReadyHand, aimAngle) + armBobOffset;
			else if (holdingGun && heldMask == 3)
				offset = Rotate(gunPose.lowReadyHand, aimAngle) - Rotate({ 6.0f, 0.0f }, aimAngle)
					+ armBobOffset;
			else if (reaching && !holdingGun)
				offset = ragdoll.input.aim;
			else
				offset = olc::vf2d{ (4.0f + phase * armSwing) * facing, 22.0f } + armBobOffset;
			const TwoBoneIkResult pose = SolveTwoBoneIk(torso, torso + offset,
				RagdollJoints()[joint].length, RagdollJoints()[joint + 1].length, facing);
			targets[elbow] = pose.joint;
			targets[hand] = pose.end;
		}
		return targets;
	}

	inline bool SameBody(b2BodyId first, b2BodyId second) {
		return first.index1 == second.index1 && first.world0 == second.world0
			&& first.generation == second.generation;
	}

	inline olc::vf2d LocalAnchor(const b2Transform& transform, const olc::vf2d& anchor) {
		const olc::vf2d offset = anchor - olc::vf2d{ transform.p.x, transform.p.y };
		return Rotate(offset, -b2Rot_GetAngle(transform.q));
	}

	inline b2JointId CreateRod(PhysicsWorld& physics, b2BodyId first, b2BodyId second, float length) {
		b2DistanceJointDef def = b2DefaultDistanceJointDef();
		def.base.bodyIdA = first;
		def.base.bodyIdB = second;
		def.length = length;
		def.enableSpring = false;
		def.enableLimit = false;
		def.enableMotor = false;
		def.base.collideConnected = false;
		def.base.constraintHertz = 60.0f;
		def.base.constraintDampingRatio = 2.0f;
		return b2CreateDistanceJoint(physics.world, &def);
	}

	inline b2JointId CreatePoseJoint(PhysicsWorld& physics, b2BodyId torso, b2BodyId part) {
		b2MotorJointDef def = b2DefaultMotorJointDef();
		def.base.bodyIdA = torso;
		def.base.bodyIdB = part;
		def.base.collideConnected = false;
		def.linearHertz = 0.0f;
		def.angularHertz = 0.0f;
		def.maxSpringForce = 0.0f;
		def.maxSpringTorque = 0.0f;
		def.linearVelocity = { 0.0f, 0.0f };
		def.angularVelocity = 0.0f;
		def.maxVelocityForce = PoseMaxForce;
		def.maxVelocityTorque = 0.0f;
		return b2CreateMotorJoint(physics.world, &def);
	}

	inline b2JointId CreateGripJoint(PhysicsWorld& physics, b2BodyId first, b2BodyId second,
		const olc::vf2d& anchor, bool rigid) {
		b2WeldJointDef def = b2DefaultWeldJointDef();
		def.base.bodyIdA = first;
		def.base.bodyIdB = second;
		const b2Transform firstTransform = b2Body_GetTransform(first);
		const b2Transform secondTransform = b2Body_GetTransform(second);
		const olc::vf2d firstLocal = LocalAnchor(firstTransform, anchor);
		const olc::vf2d secondLocal = LocalAnchor(secondTransform, anchor);
		def.base.localFrameA.p = { firstLocal.x, firstLocal.y };
		def.base.localFrameA.q = b2MakeRot(-b2Rot_GetAngle(firstTransform.q));
		def.base.localFrameB.p = { secondLocal.x, secondLocal.y };
		def.base.localFrameB.q = b2MakeRot(-b2Rot_GetAngle(secondTransform.q));
		def.base.collideConnected = false;
		def.base.constraintHertz = GripConstraintHertz;
		def.base.constraintDampingRatio = 2.0f;
		// Zero hertz is maximum stiffness
		def.linearHertz = rigid ? 0.0f : GripSpringHertz;
		def.angularHertz = rigid ? 0.0f : GripSpringHertz;
		def.linearDampingRatio = GripSpringDamping;
		def.angularDampingRatio = GripSpringDamping;
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

		const auto& joints = RagdollJoints();
		bool jointsValid = true;
		for (size_t j = 0; j < joints.size(); ++j)
			jointsValid = jointsValid && b2Joint_IsValid(rig->joints[j]);
		for (size_t i = 1; i < PartCount; ++i)
			jointsValid = jointsValid && b2Joint_IsValid(rig->pose[i]);
		if (!rebuilt && jointsValid)
			return *rig;

		for (b2JointId& grip : rig->grips) {
			if (b2Joint_IsValid(grip))
				b2DestroyJoint(grip);
			grip = b2_nullJointId;
		}
		rig->gripTargets = {};

		for (size_t j = 0; j < joints.size(); ++j) {
			if (b2Joint_IsValid(rig->joints[j]))
				b2DestroyJoint(rig->joints[j]);
			rig->joints[j] = CreateRod(physics, rig->bodies[joints[j].first],
				rig->bodies[joints[j].second], joints[j].length);
		}
		for (size_t i = 1; i < PartCount; ++i) {
			if (b2Joint_IsValid(rig->pose[i]))
				b2DestroyJoint(rig->pose[i]);
			rig->pose[i] = CreatePoseJoint(physics, rig->bodies[Torso], rig->bodies[i]);
		}
		return *rig;
	}

	inline bool ArmGripping(const RagdollState& ragdoll, size_t index) {
		for (size_t arm = 0; arm < ragdoll.grips.size(); ++arm) {
			if (ragdoll.grips[arm].target == 0)
				continue;
			if ((arm == 0 ? LeftHand : RightHand) == index)
				return true;
		}
		return false;
	}

	inline void DriveRagdoll(PhysicsWorld& physics, RagdollState& ragdoll) {
		const auto targets = RagdollTargets(ragdoll);
		PhysicsRig& rig = EnsureRig(physics, ragdoll);

		for (size_t i = 1; i < PartCount; ++i) {
			const b2JointId joint = rig.pose[i];
			if (!b2Joint_IsValid(joint))
				continue;
			if (ArmGripping(ragdoll, i)) {
				b2MotorJoint_SetMaxVelocityForce(joint, 0.0f);
				continue;
			}
			b2MotorJoint_SetMaxVelocityForce(joint, i == Hips ? HipsPoseMaxForce : PoseMaxForce);
			olc::vf2d velocity = (targets[i] - ragdoll.parts[i].position) * PoseVelocityGain;
			const float magnitude = velocity.mag();
			if (magnitude > PoseMaxVelocity)
				velocity *= PoseMaxVelocity / magnitude;
			b2MotorJoint_SetLinearVelocity(joint, { velocity.x, velocity.y });
		}
	}

	inline float BoneBend(const olc::vf2d& root, const olc::vf2d& joint, const olc::vf2d& end) {
		const olc::vf2d upper = joint - root;
		const olc::vf2d lower = end - joint;
		return std::atan2(upper.x * lower.y - upper.y * lower.x, upper.dot(lower));
	}

	inline void LimitRagdollJoint(const RagdollState& ragdoll, const PhysicsRig& rig,
		RagdollPart rootPart, RagdollPart jointPart, RagdollPart endPart,
		const olc::vf2d& targetRoot, const olc::vf2d& targetJoint, const olc::vf2d& targetEnd) {
		const olc::vf2d root = ragdoll.parts[rootPart].position;
		const olc::vf2d joint = ragdoll.parts[jointPart].position;
		const olc::vf2d end = ragdoll.parts[endPart].position;
		const olc::vf2d lower = end - joint;
		if ((joint - root).mag2() < 0.0001f || lower.mag2() < 0.0001f)
			return;

		const float actual = BoneBend(root, joint, end);
		const float target = BoneBend(targetRoot, targetJoint, targetEnd);
		float minimum = -JointBendTolerance;
		float maximum = JointMaxBend;
		if (target < -0.02f) {
			minimum = -JointMaxBend;
			maximum = JointBendTolerance;
		}
		else if (std::abs(target) <= 0.02f) {
			minimum = -0.35f;
			maximum = 0.35f;
		}
		if (actual >= minimum && actual <= maximum)
			return;

		const float violation = actual < minimum ? minimum - actual : actual - maximum;
		const float direction = actual < minimum ? 1.0f : -1.0f;
		const olc::vf2d tangent = olc::vf2d{ -lower.y, lower.x } * (1.0f / lower.mag());
		const float magnitude = std::min(JointLimitMaxImpulse, violation * JointLimitGain);
		const olc::vf2d impulse = tangent * (direction * magnitude);
		b2Body_ApplyLinearImpulseToCenter(rig.bodies[endPart], { impulse.x, impulse.y }, true);
		b2Body_ApplyLinearImpulseToCenter(rig.bodies[jointPart], { -impulse.x, -impulse.y }, true);
	}

	inline void LimitRagdollJoints(PhysicsWorld& physics, const RagdollState& ragdoll) {
		const auto targets = RagdollTargets(ragdoll);
		const PhysicsRig& rig = EnsureRig(physics, ragdoll);
		LimitRagdollJoint(ragdoll, rig, Torso, LeftElbow, LeftHand,
			targets[Torso], targets[LeftElbow], targets[LeftHand]);
		LimitRagdollJoint(ragdoll, rig, Torso, RightElbow, RightHand,
			targets[Torso], targets[RightElbow], targets[RightHand]);
		LimitRagdollJoint(ragdoll, rig, Hips, LeftKnee, LeftFoot,
			targets[Hips], targets[LeftKnee], targets[LeftFoot]);
		LimitRagdollJoint(ragdoll, rig, Hips, RightKnee, RightFoot,
			targets[Hips], targets[RightKnee], targets[RightFoot]);
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
		const size_t selected = ragdoll.inventory.selected;
		const ItemId selectedItem = selected < ragdoll.inventory.hotbar.size()
			? ragdoll.inventory.hotbar[selected].item : ItemNone;
		const uint8_t heldMask = ItemHandMask(ragdoll, selectedItem);
		if ((heldMask & (1u << arm)) != 0) {
			if (IsGun(selectedItem)) {
				std::array<olc::vf2d, PartCount> pose;
				for (size_t i = 0; i < PartCount; ++i)
					pose[i] = ragdoll.parts[i].position;
				const olc::vf2d unit = HeldItemDirection(pose, ragdoll.input, ragdoll.facing,
					arm == 0, selectedItem, ragdoll.gunRecoil, GunFollowsHand(ragdoll), AimBob(ragdoll));
				hand.angle = std::atan2(unit.y, unit.x);
			}
			grip = {};
			return;
		}
		if (!(arm == 0 ? ragdoll.input.reachLeft : ragdoll.input.reachRight) || !ragdoll.input.grab) {
			grip = {};
			return;
		}
		const bool moving = std::abs(ragdoll.input.moveX) > 0.01f;
		if (grip.target != 0) {
			if (grip.target <= TestTerrain().size()
				&& (moving || (hand.position - grip.anchor).mag() > hand.radius + 6.0f))
				grip = {};
			return;
		}

		const auto& terrain = TestTerrain();
		for (size_t i = 0; !moving && i < terrain.size(); ++i) {
			const olc::vf2d closest = olc::utils::geom2d::closest(terrain[i], hand.position);
			const olc::vf2d offset = hand.position - closest;
			const float distance = offset.mag();
			if (distance > hand.radius + 0.5f)
				continue;
			const olc::vf2d normal = distance > 0.0001f ? offset / distance
				: (ragdoll.input.aim.mag() > 0.0001f ? -ragdoll.input.aim.norm() : olc::vf2d{ 0.0f, -1.0f });
			if (!AimingAtContact(ragdoll.input.aim, normal))
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
			const olc::vf2d normal = distance > 0.0001f ? offset / distance
				: (ragdoll.input.aim.mag() > 0.0001f ? -ragdoll.input.aim.norm() : olc::vf2d{ 0.0f, -1.0f });
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
			if (grip.target > 0 && sameTarget && b2Joint_IsValid(rig.grips[arm]))
				continue;
			if (b2Joint_IsValid(rig.grips[arm]))
				b2DestroyJoint(rig.grips[arm]);
			rig.grips[arm] = b2_nullJointId;
			rig.gripTargets[arm] = grip.target;
			if (grip.target == 0 || !b2Body_IsValid(target))
				continue;
			const b2BodyId hand = rig.bodies[arm == 0 ? LeftHand : RightHand];
			if (!b2Body_IsValid(hand))
				continue;
			const bool rigid = grip.target <= terrainCount;
			rig.grips[arm] = CreateGripJoint(physics, hand, target,
				ragdoll.parts[arm == 0 ? LeftHand : RightHand].position, rigid);
		}
	}
}
