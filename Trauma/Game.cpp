#include "Game.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace Trauma {
	namespace {
		constexpr int MenuOptionCount = 3;
		constexpr olc::vf2d MenuButtonSize = { 360.0f, 56.0f };
		constexpr float MenuButtonSpacing = 70.0f;
		constexpr olc::vf2d MenuTextScale = { 2.0f, 2.0f };
	}

	Game::Game(std::string serverHost, uint16_t serverPort)
		: host(std::move(serverHost)), port(serverPort) {
		sAppName = "Trauma";
	}

	bool Game::OnUserCreate() {
		scene = Scene::Menu;
		ResetCamera();
		return true;
	}

	bool Game::OnUserUpdate(float fElapsedTime) {
		switch (scene) {
			case Scene::Menu:
			case Scene::Editor:
			case Scene::Play:
				return UpdatePlay(fElapsedTime);
		}

		return true;
	}

	bool Game::UpdatePlay(float fElapsedTime) {
		if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) {
			client.Disconnect();
			localRenderReady = false;
			jumpQueued = false;
			scene = Scene::Menu;
			return true;
		}

		const float deltaTime = std::min(fElapsedTime, 0.1f);

		if (keyboard.GetKey(olc::Key::R).bPressed && client.GetStatus() == Client::Status::Disconnected)
			client.Connect(host, port);


		if (client.GetStatus() == Client::Status::Connected) {
			jumpQueued = jumpQueued || keyboard.GetKey(olc::Key::SPACE).bPressed;
			fixedAccumulator += deltaTime;
			int steps = 0;
			while (fixedAccumulator >= FixedDeltaTime && steps < 5) {
				const InputState input = SampleInput(jumpQueued);
				jumpQueued = false;

				previousLocal = client.GetLocalRagdoll().parts[Torso].position;
				client.StepInput(input);
				currentLocal = client.GetLocalRagdoll().parts[Torso].position;

				fixedAccumulator -= FixedDeltaTime;
				++steps;
			}
		}
		else {
			fixedAccumulator = 0.0f;
			jumpQueued = false;
		}

		client.Poll(deltaTime);

		if (client.GetStatus() == Client::Status::Connected) {
			currentLocal = client.GetLocalRagdoll().parts[Torso].position;
			if (!localRenderReady) {
				previousLocal = currentLocal;
				localRenderReady = true;
			}
		}
		else {
			localRenderReady = false;
		}

		UpdateRenderPosition();
		UpdateCamera(deltaTime);

		draw.Clear(olc::Pixel(5, 10, 17));
		draw.SetWorldTransform(camera.GetWorldTransform());
		DrawScene();
		draw.WorldReset();
		return true;
	}

	void Game::StartPlay() {
		fixedAccumulator = 0.0f;
		localRenderReady = false;
		cameraInitialised = false;
		scene = Scene::Play;
		client.Connect(host, port);
	}

	void Game::UpdateRenderPosition() {
		if (!localRenderReady) {
			renderedLocal = currentLocal;
			return;
		}

		const float alpha = std::clamp(fixedAccumulator / FixedDeltaTime, 0.0f, 1.0f);
		renderedLocal = previousLocal.lerp(currentLocal, alpha);
	}

	InputState Game::SampleInput(bool bJump) const {
		InputState input;
		input.jump = bJump;
		input.run = keyboard.GetKey(olc::Key::SHIFT).bHeld;
		input.crouch = keyboard.GetKey(olc::Key::CTRL).bHeld;
		input.crawl = keyboard.GetKey(olc::Key::C).bHeld;
		input.reachLeft = mouse.GetButton(0).bHeld;
		input.reachRight = mouse.GetButton(1).bHeld;
		const olc::vf2d mouseWorld = camera.GetWorldTransform().inverse(mouse.GetPosition());
		const olc::vf2d aim = mouseWorld - client.GetLocalRagdoll().parts[Torso].position;
		input.aim = { std::clamp(aim.x, -2000.0f, 2000.0f), std::clamp(aim.y, -2000.0f, 2000.0f) };

		if (keyboard.GetKey(olc::Key::A).bHeld || keyboard.GetKey(olc::Key::LEFT).bHeld)
			input.moveX -= 1.0f;
		if (keyboard.GetKey(olc::Key::D).bHeld || keyboard.GetKey(olc::Key::RIGHT).bHeld)
			input.moveX += 1.0f;
		if (keyboard.GetKey(olc::Key::W).bHeld || keyboard.GetKey(olc::Key::UP).bHeld)
			input.climb -= 1.0f;
		if (keyboard.GetKey(olc::Key::S).bHeld || keyboard.GetKey(olc::Key::DOWN).bHeld)
			input.climb += 1.0f;

		return input;
	}

	void Game::UpdateCamera(float fElapsedTime) {
		if (client.GetStatus() == Client::Status::Connected && localRenderReady) {
			cameraTarget = { renderedLocal.x, renderedLocal.y - 10.0f };

			if (!cameraInitialised) {
				camera.SetTarget(cameraTarget);
				camera.SetMode(olc::utils::Camera2D::Mode::Simple);
				camera.Update(0.0f);
				camera.SetMode(olc::utils::Camera2D::Mode::LazyFollow);
				cameraInitialised = true;
			}
		}

		if (keyboard.GetKey(olc::Key::Q).bHeld)
			SetCameraZoom(cameraZoom - fElapsedTime * 1.6f);
		if (keyboard.GetKey(olc::Key::E).bHeld)
			SetCameraZoom(cameraZoom + fElapsedTime * 1.6f);

		camera.Update(fElapsedTime);
	}

	void Game::ResetCamera() {
		camera = olc::utils::Camera2D(ScreenSize(), { cameraZoom, cameraZoom });
		camera.SetWorldBoundary(
			{ -4000, -4000 },
			{ 8000, 8000 });
		camera.EnableWorldBoundary(true);
		camera.SetLazyFollowRate(9.0f);
		cameraTarget = { 0.0f, 0.0f };
		camera.SetTarget(cameraTarget);
		camera.SetMode(olc::utils::Camera2D::Mode::Simple);
		camera.Update(0.0f);
		camera.SetMode(olc::utils::Camera2D::Mode::LazyFollow);
	}

	void Game::SetCameraZoom(float zoom) {
		const float clamped = std::clamp(zoom, 0.8f, 4.0f);
		if (clamped == cameraZoom)
			return;

		cameraZoom = clamped;
		camera = olc::utils::Camera2D(ScreenSize(), { cameraZoom, cameraZoom });
		camera.SetWorldBoundary(
			{ -4000, -4000 },
			{ 8000, 8000 });
		camera.EnableWorldBoundary(true);
		camera.SetLazyFollowRate(9.0f);
		camera.SetTarget(cameraTarget);
		camera.SetMode(olc::utils::Camera2D::Mode::Simple);
		camera.Update(0.0f);
		camera.SetMode(olc::utils::Camera2D::Mode::LazyFollow);
	}

	void Game::DrawScene() {
		for (const Terrain& ladder : TestLadders()) {
			const olc::Pixel colour(190, 150, 90);
			const float x = ladder.pos.x + ladder.size.x * 0.5f;
			draw.Line({x, ladder.pos.y}, {x, ladder.pos.y + ladder.size.y}, colour);
			for (float y = ladder.pos.y; y <= ladder.pos.y + ladder.size.y; y += LadderRungSpacing)
				draw.Line({x - 3.0f, y}, {x + 3.0f, y}, colour);
		}
		for (const Terrain& terrain : TestTerrain())
			draw.FilledRect(terrain.pos, terrain.size, olc::Pixel(65, 80, 95));
		DrawObjects();
	}

	void Game::DrawObjects() {
		const float alpha = std::clamp(fixedAccumulator / FixedDeltaTime, 0.0f, 1.0f);
		for (const Client::ObjectView& object : client.GetObjects()) {
			if (object.id <= MaxPlayers) {
				std::array<olc::vf2d, PartCount> pose;
				float facing = 1.0f;
				if (client.GetRemotePose(object.id, alpha, pose, facing))
					DrawRagdoll(pose, facing, olc::Colour::CYAN);
			}
			else {
				olc::vf2d position = object.position;
				client.GetPredictedBodyPosition(object.id, alpha, position);
				DrawObject(position, object.radius, olc::Pixel(235, 166, 75));
			}
		}

		if (client.GetStatus() != Client::Status::Connected)
			return;

		DrawRagdoll(client.GetLocalPose(alpha), client.GetLocalRagdoll().facing, olc::Colour::CYAN);
	}

	void Game::DrawObject(const olc::vf2d& position, float radius, olc::Pixel colour) {
		draw.FilledCircle(position, radius, colour);
	}

	void Game::DrawRagdoll(const std::array<olc::vf2d, PartCount>& pose, float facing, olc::Pixel colour) {
		const olc::Pixel farColour(45, 125, 135);
		auto drawLimb = [&](RagdollPart root, RagdollPart joint, RagdollPart end, olc::Pixel limbColour) {
			draw.Line(pose[root], pose[joint], limbColour);
			draw.Line(pose[joint], pose[end], limbColour);
			draw.FilledCircle(pose[joint], 4.0f, limbColour);
			draw.FilledCircle(pose[end], 4.0f, limbColour);
		};
		drawLimb(Torso, LeftElbow, LeftHand, farColour);
		drawLimb(Hips, LeftKnee, LeftFoot, farColour);
		draw.Line(pose[Head], pose[Torso], colour);
		draw.Line(pose[Torso], pose[Hips], colour);
		draw.FilledCircle(pose[Head], 7.0f, colour);
		draw.FilledCircle(pose[Torso], 6.0f, colour);
		draw.FilledCircle(pose[Hips], 6.0f, colour);
		drawLimb(Hips, RightKnee, RightFoot, colour);
		drawLimb(Torso, RightElbow, RightHand, colour);
		draw.FilledCircle(pose[Head] + olc::vf2d{facing * 4.0f, -1.0f}, 1.5f, olc::Pixel(5, 10, 17));
	}
}
