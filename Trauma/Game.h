#pragma once
#include "Client.h"
#include "olcPixelGameEngine3.h"
#include "olcUTIL3_Camera2D.h"
#include <string>
#include <vector>

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
			SlotAddress source;
			ItemId item = ItemNone;
		};

		struct SlotResult {
			bool clicked = false;
			bool dropped = false;
			bool hovered = false;
			ItemDrag payload;
			SlotAddress dropTarget;
			olc::vf2d screenMin;
			olc::vf2d screenMax;
		};

		struct DropPreview {
			bool active = false;
			olc::vf2d position;
			ItemId item = ItemNone;
			bool shelf = false;
			bool moving = false;
			uint8_t movingShelf = 0;
		};

		struct OpenContainerWindow {
			SlotAddress address;
			olc::vf2d anchor;
			olc::vf2d position;
			bool positionValid = false;
		};

		struct ContainerWindowSnapshot {
			SlotAddress address;
			std::string title;
			olc::vf2d position;
			olc::vf2d size;
			olc::vf2d previousPosition;
			bool hadPrevious = false;
		};

		bool UpdatePlay(float fElapsedTime);
		void LeavePlay();
		void UpdateClientInput(float deltaTime);
		void UpdateInventoryInput();
		void UpdateGrabMode();
		void UpdateGunFire();
		void ReloadGun();
		void UpdateDropMode();
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
		void DrawShotTracers();
		void DrawObject(const Client::ObjectView& object, const olc::vf2d& position,
			float angle, olc::Pixel colour);
		void DrawRagdoll(const std::array<olc::vf2d, PartCount>& pose, float facing, olc::Pixel colour,
			const RagdollState* ragdoll);
		void DrawItem(ItemId item, const ObjectState& body);
		void DrawShelfBoards(const olc::vf2d& position, olc::Pixel colour);
		void DrawDropPreview();
		void DrawItemGhost(ItemId item);

		void BuildUi();
		void BuildStatus();
		void BuildHud();
		void BuildPickupLabel();
		void BuildPauseMenu();
		void BuildSettings();
		SlotResult BuildSlot(const char* id, const Inventory& inventory,
			const SlotAddress& address, const ItemStack& stack, const char* caption, bool selected);
		bool IsContainerOpen(const SlotAddress& address) const;
		void ToggleContainer(const SlotAddress& address, const olc::vf2d& anchor);
		void CloseContainer(const SlotAddress& address);
		bool BuildContainerWindow(const Inventory& inventory, const OpenContainerWindow& state,
			ContainerWindowSnapshot& snapshot);
		void SnapContainerWindows(std::vector<ContainerWindowSnapshot>& windows);
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
		bool settingsOpen = false;
		bool styleSaveSucceeded = false;
		double styleSavedAt = -1.0;
		bool quitting = false;
		uint8_t hotbarSelection = 0;
		std::vector<OpenContainerWindow> openContainers;

		ItemDrag drag;
		bool dragDelivered = false;
		bool dropMode = false;
		bool shelfMode = false;
		bool movingShelfActive = false;
		uint8_t movingShelf = 0;
		olc::vf2d hudMin;
		olc::vf2d hudMax;
		DropPreview dropPreview;
	};
}
