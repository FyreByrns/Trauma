#pragma once
#include "Client.h"
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Camera2D.h"
#include <string>

namespace Trauma {
	class Game final : public olc::PixelGameEngine {
	public:
		Game(std::string serverHost, uint16_t serverPort);

	protected:
		bool OnUserCreate() override;
		bool OnUserUpdate(float fElapsedTime) override;

	private:
		enum class Scene {
			Menu,
			Play,
			Editor
		};

		bool UpdatePlay(float fElapsedTime);
		void StartPlay();

		InputState SampleInput(bool bJump) const;
		void UpdateRenderPosition();
		void UpdateCamera(float fElapsedTime);
		void ResetCamera();
		void SetCameraZoom(float zoom);
		void DrawScene();
		void DrawObjects();
		void DrawObject(const olc::vf2d& position, float radius, olc::Pixel colour);

	private:
		std::string host;
		uint16_t port = 0;
		Scene scene = Scene::Menu;
		Client client;

		float fixedAccumulator = 0.0f;
		bool localRenderReady = false;
		bool jumpQueued = false;

		olc::vf2d previousLocal;
		olc::vf2d currentLocal;
		olc::vf2d renderedLocal;

		olc::utils::Camera2D camera;
		olc::vf2d cameraTarget = { 0.0f, 0.0f };
		float cameraZoom = 2.4f;
		bool cameraInitialised = false;
	};
}
