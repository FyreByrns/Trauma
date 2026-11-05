#pragma once
#include "Simulation.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace Trauma {
	namespace MovementProbeDetail {
		constexpr float ProbeGaitPhase = 0.78539816339f;
		constexpr int ProbeSpeedTicks = 90;
		constexpr float ProbePi = 3.14159265359f;

		struct Scenario {
			PhysicsWorld physics;
			RagdollState ragdoll;

			Scenario() {
				ResetWorld(physics);
				CreateTerrainBodies(physics);
				ragdoll = CreateRagdoll({ 0.0f, 100.0f });
			}

			~Scenario() {
				DestroyWorld(physics);
			}

			Scenario(const Scenario&) = delete;
			Scenario& operator=(const Scenario&) = delete;
		};

		inline void Equip(Scenario& scenario, ItemId item) {
			scenario.ragdoll.inventory.selected = 0;
			scenario.ragdoll.inventory.hotbar[0] = MakeStack(item);
		}

		inline InputState MakeInput(float moveX, float aimX, bool run, bool aiming) {
			InputState input;
			input.moveX = moveX;
			input.aim = { aimX, 0.0f };
			input.run = run;
			input.reachRight = aiming;
			input.grab = false;
			input.hotbar = 0;
			return input;
		}

		inline void Step(Scenario& scenario, const InputState& input) {
			std::vector<RagdollState*> ragdolls = { &scenario.ragdoll };
			std::vector<ObjectState*> objects;
			ApplyRagdollInput(scenario.physics, scenario.ragdoll, input);
			StepPhysics(scenario.physics, ragdolls, objects, FixedDeltaTime);
		}

		inline float MeasureSpeed(ItemId item, const InputState& input) {
			Scenario scenario;
			Equip(scenario, item);
			for (int tick = 0; tick < ProbeSpeedTicks; ++tick)
				Step(scenario, input);
			return scenario.ragdoll.parts[Torso].velocity.x;
		}

		inline float MeasureSwing(ItemId item, const InputState& input) {
			Scenario scenario;
			Equip(scenario, item);
			ApplyRagdollInput(scenario.physics, scenario.ragdoll, input);
			scenario.ragdoll.gaitPhase = 1.0f;
			const auto targets = RagdollTargets(scenario.ragdoll);
			return targets[RightHand].x - targets[LeftHand].x;
		}

		struct BobMeasurement {
			float bob = 0.0f;
			float targetShift = 0.0f;
		};

		inline BobMeasurement MeasureBob(ItemId item, const InputState& input) {
			Scenario scenario;
			Equip(scenario, item);
			ApplyRagdollInput(scenario.physics, scenario.ragdoll, input);
			scenario.ragdoll.gaitPhase = 0.0f;
			const auto baseline = RagdollTargets(scenario.ragdoll);
			scenario.ragdoll.gaitPhase = ProbeGaitPhase;
			const auto shifted = RagdollTargets(scenario.ragdoll);
			return {
				ArmBob(scenario.ragdoll),
				shifted[RightHand].y - baseline[RightHand].y
			};
		}

		struct SwayMeasurement {
			float degrees = 0.0f;
			float offset = 0.0f;
			size_t rays = 0;
		};

		inline SwayMeasurement MeasureSway(ItemId item, const InputState& input) {
			Scenario scenario;
			Equip(scenario, item);
			ApplyRagdollInput(scenario.physics, scenario.ragdoll, input);
			scenario.ragdoll.gaitPhase = ProbeGaitPhase;
			RagdollState baseline = scenario.ragdoll;
			baseline.gaitPhase = 0.0f;
			const GunBarrelRays baseRays = ComputeGunBarrelRays(baseline, 0);
			const GunBarrelRays rays = ComputeGunBarrelRays(scenario.ragdoll, 0);
			if (baseRays.count == 0 || rays.count == 0)
				return {};
			const float baseAngle = std::atan2(baseRays.rays[0].direction.y, baseRays.rays[0].direction.x);
			const float angle = std::atan2(rays.rays[0].direction.y, rays.rays[0].direction.x);
			const float delta = std::remainder(angle - baseAngle, TwoPi);
			const olc::vf2d baseAim = rays.rays[0].origin + baseRays.rays[0].direction * GunRange;
			const olc::vf2d shotAim = rays.rays[0].origin + rays.rays[0].direction * GunRange;
			return {
				std::abs(delta) * 180.0f / ProbePi,
				(shotAim - baseAim).mag(),
				rays.count
			};
		}
	}

	inline bool RunMovementProbe() {
		using namespace MovementProbeDetail;
		using std::cout;
		using std::fixed;
		using std::setprecision;

		const ItemId gun = FindItemId("gun");
		const ItemId wrench = FindItemId("wrench");
		if (!IsGun(gun) || !IsItem(wrench)) {
			cout << "[movement-probe] required items are missing" << std::endl;
			return false;
		}

		cout << fixed << setprecision(3);
		int failures = 0;
		const auto check = [&](bool condition, const char* label) {
			if (condition)
				return;
			++failures;
			cout << "[movement-probe] FAIL " << label << std::endl;
		};

		int facingPasses = 0;
		for (float aim : { -1.0f, 1.0f }) {
			for (float move : { -1.0f, 1.0f }) {
				Scenario scenario;
				Equip(scenario, gun);
				const InputState input = MakeInput(move, aim, false, false);
				scenario.ragdoll.facing = aim > 0.0f ? -1.0f : 1.0f;
				for (int tick = 0; tick < 12; ++tick)
					Step(scenario, input);
				const bool pass = scenario.ragdoll.facing == (aim > 0.0f ? 1.0f : -1.0f);
				facingPasses += pass ? 1 : 0;
				cout << "[movement-probe] facing aim=" << aim << " move=" << move
					<< " facing=" << scenario.ragdoll.facing << (pass ? " PASS" : " FAIL") << std::endl;
			}
		}
		check(facingPasses == 4, "facing must track the mouse for every movement direction");

		const BobMeasurement walk = MeasureBob(gun, MakeInput(1.0f, 1.0f, false, false));
		const BobMeasurement run = MeasureBob(gun, MakeInput(1.0f, 1.0f, true, false));
		const BobMeasurement aimingWalk = MeasureBob(gun, MakeInput(1.0f, 1.0f, false, true));
		const BobMeasurement aimingRun = MeasureBob(gun, MakeInput(1.0f, 1.0f, true, true));
		const SwayMeasurement walkSway = MeasureSway(gun, MakeInput(1.0f, 1.0f, false, false));
		const SwayMeasurement runSway = MeasureSway(gun, MakeInput(1.0f, 1.0f, true, false));
		const SwayMeasurement aimingWalkSway = MeasureSway(gun, MakeInput(1.0f, 1.0f, false, true));
		const SwayMeasurement aimingRunSway = MeasureSway(gun, MakeInput(1.0f, 1.0f, true, true));

		cout << "[movement-probe] bob walk=" << walk.bob << " target_shift=" << walk.targetShift
			<< " run=" << run.bob << " target_shift=" << run.targetShift << std::endl;
		cout << "[movement-probe] aiming bob walk=" << aimingWalk.bob
			<< " target_shift=" << aimingWalk.targetShift
			<< " run=" << aimingRun.bob << " target_shift=" << aimingRun.targetShift << std::endl;
		cout << "[movement-probe] low-ready sway walk=" << walkSway.degrees << " deg "
			<< walkSway.offset << " px@" << GunRange << " run=" << runSway.degrees << " deg "
			<< runSway.offset << " px rays=" << runSway.rays << std::endl;
		cout << "[movement-probe] aiming sway walk=" << aimingWalkSway.degrees << " deg "
			<< aimingWalkSway.offset << " px run=" << aimingRunSway.degrees << " deg "
			<< aimingRunSway.offset << " px rays=" << aimingRunSway.rays << std::endl;

		check(walk.bob > 0.5f, "walking arm bob must be nonzero");
		check(run.bob > walk.bob * 1.5f, "running arm bob must be larger than walking");
		check(std::abs(walk.targetShift) > 0.1f, "walking bob must displace the arm target");
		check(std::abs(run.targetShift) > std::abs(walk.targetShift),
			"running bob must displace the arm target more");
		check(walkSway.rays > 0 && walkSway.degrees > 0.5f && walkSway.offset > 1.0f,
			"walking bob must displace gun and shot direction");
		check(runSway.degrees > walkSway.degrees && runSway.offset > walkSway.offset,
			"running bob must displace gun and shot direction more");

		const float aimingWalkSpeed = std::abs(MeasureSpeed(gun, MakeInput(1.0f, 1.0f, false, true)));
		const float aimingRunSpeed = std::abs(MeasureSpeed(gun, MakeInput(1.0f, 1.0f, true, true)));
		cout << "[movement-probe] aiming speed walk=" << aimingWalkSpeed
			<< " run=" << aimingRunSpeed << std::endl;

		check(aimingRunSway.rays > 0, "aiming while running must still produce gun rays");
		check(aimingRunSpeed < aimingWalkSpeed * 0.95f,
			"aiming while running must be slower than aiming while walking");
		check(aimingRun.bob > aimingWalk.bob * 1.5f,
			"aiming while running must move the arms more than aiming while walking");
		check(aimingRunSway.degrees > aimingWalkSway.degrees
				&& aimingRunSway.offset > aimingWalkSway.offset,
			"aiming while running must displace the shot more");

		const InputState forwardInput = MakeInput(1.0f, 1.0f, false, false);
		const InputState backwardInput = MakeInput(-1.0f, 1.0f, false, false);
		const float forwardSpeed = std::abs(MeasureSpeed(wrench, forwardInput));
		const float backwardSpeed = std::abs(MeasureSpeed(wrench, backwardInput));
		const float forwardSwing = MeasureSwing(wrench, forwardInput);
		const float backwardSwing = MeasureSwing(wrench, backwardInput);
		cout << "[movement-probe] backpedal speed=" << backwardSpeed << " forward=" << forwardSpeed
			<< " arm_span=" << backwardSwing << " forward_arm_span=" << forwardSwing << std::endl;

		check(backwardSpeed < forwardSpeed * 0.8f, "backpedaling must be slower than walking forward");
		check(forwardSwing > 0.0f && backwardSwing < 0.0f,
			"backpedaling must reverse the arm swing");

		if (failures != 0) {
			cout << "[movement-probe] FAIL " << failures << " check(s)" << std::endl;
			return false;
		}
		cout << "[movement-probe] PASS" << std::endl;
		return true;
	}
}
