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
			scene = Scene::Menu;
			return true;
		}

		const float deltaTime = std::min(fElapsedTime, 0.1f);

		if (keyboard.GetKey(olc::Key::R).bPressed && client.GetStatus() == Client::Status::Disconnected)
			client.Connect(host, port);


		if (client.GetStatus() == Client::Status::Connected) {
			fixedAccumulator += deltaTime;
			int steps = 0;
			while (fixedAccumulator >= FixedDeltaTime && steps < 5) {
				const InputState input = SampleInput(false);

				previousLocal = client.GetLocalState().position;
				client.StepInput(input);
				currentLocal = client.GetLocalState().position;

				fixedAccumulator -= FixedDeltaTime;
				++steps;
			}
		}
		else {
			fixedAccumulator = 0.0f;
		}

		client.Poll(deltaTime);

		if (client.GetStatus() == Client::Status::Connected) {
			currentLocal = client.GetLocalState().position;
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

		if (keyboard.GetKey(olc::Key::A).bHeld || keyboard.GetKey(olc::Key::LEFT).bHeld)
			input.moveX -= 1.0f;
		if (keyboard.GetKey(olc::Key::D).bHeld || keyboard.GetKey(olc::Key::RIGHT).bHeld)
			input.moveX += 1.0f;

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
			{ 4000, 4000 });
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
			{ 4000, 4000 });
		camera.EnableWorldBoundary(true);
		camera.SetLazyFollowRate(9.0f);
		camera.SetTarget(cameraTarget);
		camera.SetMode(olc::utils::Camera2D::Mode::Simple);
		camera.Update(0.0f);
		camera.SetMode(olc::utils::Camera2D::Mode::LazyFollow);
	}

	void Game::DrawScene() {
		DrawObjects();
	}

	void Game::DrawObjects() {
		for (const Client::ObjectView& object : client.GetObjects()) {
			DrawObject(object.position, olc::Colour::CYAN, false);
		}

		if (client.GetStatus() != Client::Status::Connected)
			return;

		DrawObject(renderedLocal, olc::Colour::CYAN, true);
	}

	void Game::DrawObject(const olc::vf2d& position, olc::Pixel colour, bool bLocal) {
		draw.FilledCircle(position, 10, colour);
	}
}
