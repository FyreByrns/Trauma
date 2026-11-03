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
		struct ItemDrag {
			bool active = false;
			bool clothing = false;
			size_t index = 0;
			Item item = Item::None;
		};

		struct SlotResult {
			bool clicked = false;
			bool dropped = false;
			ItemDrag payload;
		};

		struct DropPreview {
			bool active = false;
			olc::vf2d position;
			Item item = Item::None;
		};

		bool UpdatePlay(float fElapsedTime);
		void LeavePlay();
		void UpdateClientInput(float deltaTime);
		void UpdateInventoryInput();
		void UpdateGrabMode();
		void UpdateDropPreview();
		InputState SampleInput(bool bJump) const;
		void UpdateLocalRenderState();
		void UpdateRenderPosition();

		void UpdateCamera(float fElapsedTime);
		void ResetCamera();
		void ApplyCameraSettings();
		void SetCameraZoom(float zoom);

		void DrawScene();
		void DrawObjects();
		void DrawObject(const Client::ObjectView& object, const olc::vf2d& position,
			float angle, olc::Pixel colour);
		void DrawRagdoll(const std::array<olc::vf2d, PartCount>& pose, float facing, olc::Pixel colour);
		void DrawItem(Item item, const ObjectState& body);
		void DrawDropPreview();

		void BuildUi();
		void BuildStatus();
		void BuildHud();
		void BuildPauseMenu();
		SlotResult BuildSlot(const char* id, bool clothing, size_t index, Item item,
			const char* caption, bool selected);
		void ResolveItemDrag();

	private:
		std::string host;
		uint16_t port = 0;
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

		bool grabsEnabled = true;
		bool paused = false;
		bool quitting = false;
		uint8_t hotbarSelection = 0;

		ItemDrag drag;
		bool dragDelivered = false;
		olc::vf2d hudMin;
		olc::vf2d hudMax;
		DropPreview dropPreview;
	};
}
