#include "Game.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace Trauma {
	namespace {
		constexpr float MaxFrameTime = 0.1f;
		constexpr int MaxFixedStepsPerFrame = 5;

		constexpr float HotbarSlotSize = 46.0f;
		constexpr float HotbarSlotGap = 4.0f;
		constexpr float HotbarGroupSpacing = HotbarSlotGap * 3.0f;
		constexpr float HotbarBottomMargin = 12.0f;

		constexpr float GrabToggleWidth = 92.0f;
		constexpr float GrabToggleGap = HotbarGroupSpacing;

		constexpr float CameraLazyFollowRate = 9.0f;
		constexpr float CameraMinZoom = 0.8f;
		constexpr float CameraMaxZoom = 4.0f;
		constexpr float CameraZoomPerWheelNotch = 0.15f;
		const olc::vf2d CameraWorldOrigin = { -4000.0f, -4000.0f };
		const olc::vf2d CameraWorldSize = { 8000.0f, 8000.0f };

		float SlotPitch() {
			return HotbarSlotSize + HotbarSlotGap;
		}

		olc::vf2d SlotOrigin(const olc::vf2d& origin, size_t index) {
			return origin + olc::vf2d(SlotPitch() * static_cast<float>(index), 0.0f);
		}

		float GroupWidth(size_t slots) {
			if (slots == 0)
				return 0.0f;
			return static_cast<float>(slots) * HotbarSlotSize
				+ static_cast<float>(slots - 1) * HotbarSlotGap;
		}
	}

	Game::Game(std::string serverHost, uint16_t serverPort)
		: host(std::move(serverHost)), port(serverPort) {
		sAppName = "Trauma";
	}

	bool Game::OnUserCreate() {
		ResetCamera();
		return true;
	}

	bool Game::OnUserUpdate(float fElapsedTime) {
		return UpdatePlay(fElapsedTime);
	}

	bool Game::UpdatePlay(float fElapsedTime) {
		if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) {
			LeavePlay();
			return true;
		}

		const float deltaTime = std::min(fElapsedTime, MaxFrameTime);
		UpdateClientInput(deltaTime);
		client.Poll(deltaTime);

		if (client.GetStatus() == Client::Status::Connected)
			UpdateDrag();
		else
			dragging = false;

		UpdateLocalRenderState();
		UpdateRenderPosition();
		UpdateCamera(deltaTime);

		draw.Clear(olc::Pixel(5, 10, 17));
		draw.SetWorldTransform(camera.GetWorldTransform());
		DrawScene();
		draw.WorldReset();
		DrawHud();
		return true;
	}

	void Game::LeavePlay() {
		client.Disconnect();
		fixedAccumulator = 0.0f;
		localRenderReady = false;
		jumpQueued = false;
		dragging = false;
		dragItem = Item::None;
	}

	void Game::UpdateClientInput(float deltaTime) {
		if (keyboard.GetKey(olc::Key::R).bPressed && client.GetStatus() == Client::Status::Disconnected)
			client.Connect(host, port);

		if (client.GetStatus() != Client::Status::Connected) {
			fixedAccumulator = 0.0f;
			jumpQueued = false;
			return;
		}

		UpdateInventoryInput();
		UpdateGrabMode();

		jumpQueued = jumpQueued || keyboard.GetKey(olc::Key::SPACE).bPressed;
		fixedAccumulator += deltaTime;

		int steps = 0;
		while (fixedAccumulator >= FixedDeltaTime && steps < MaxFixedStepsPerFrame) {
			const InputState input = SampleInput(jumpQueued);
			jumpQueued = false;

			previousLocal = client.GetLocalRagdoll().parts[Torso].position;
			client.StepInput(input);
			currentLocal = client.GetLocalRagdoll().parts[Torso].position;

			fixedAccumulator -= FixedDeltaTime;
			++steps;
		}
	}

	void Game::UpdateInventoryInput() {
		const std::array<olc::Key, HotbarSlots> slotKeys = {
			olc::Key::K1, olc::Key::K2, olc::Key::K3, olc::Key::K4, olc::Key::K5,
			olc::Key::K6, olc::Key::K7, olc::Key::K8, olc::Key::K9, olc::Key::K0 };
		for (size_t i = 0; i < slotKeys.size(); ++i)
			if (keyboard.GetKey(slotKeys[i]).bPressed)
				hotbarSelection = static_cast<uint8_t>(i);

		if (keyboard.GetKey(olc::Key::E).bPressed) {
			size_t index = 0;
			if (client.GetPickupCandidate(index))
				client.PickupItem(index);
		}
	}

	void Game::UpdateGrabMode() {
		if (keyboard.GetKey(olc::Key::G).bPressed)
			grabsEnabled = !grabsEnabled;
		if (mouse.GetButton(0).bPressed && GrabToggleAt(mouse.GetPosition()))
			grabsEnabled = !grabsEnabled;
	}

	void Game::UpdateLocalRenderState() {
		if (client.GetStatus() != Client::Status::Connected) {
			localRenderReady = false;
			return;
		}

		currentLocal = client.GetLocalRagdoll().parts[Torso].position;
		if (!localRenderReady) {
			previousLocal = currentLocal;
			localRenderReady = true;
		}
	}

	InputState Game::SampleInput(bool bJump) const {
		InputState input;
		input.jump = bJump;
		input.run = keyboard.GetKey(olc::Key::SHIFT).bHeld;
		input.crouch = keyboard.GetKey(olc::Key::CTRL).bHeld;
		input.crawl = keyboard.GetKey(olc::Key::C).bHeld;
		input.reachLeft = mouse.GetButton(0).bHeld;
		input.reachRight = mouse.GetButton(1).bHeld;
		input.grab = grabsEnabled;

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
		input.hotbar = hotbarSelection;

		return input;
	}

	void Game::UpdateRenderPosition() {
		if (!localRenderReady) {
			renderedLocal = currentLocal;
			return;
		}

		const float alpha = std::clamp(fixedAccumulator / FixedDeltaTime, 0.0f, 1.0f);
		renderedLocal = previousLocal.lerp(currentLocal, alpha);
	}

	void Game::UpdateCamera(float fElapsedTime) {
		if (client.GetStatus() == Client::Status::Connected && localRenderReady) {
			cameraTarget = { renderedLocal.x, renderedLocal.y - 10.0f };

			if (!cameraInitialised) {
				ApplyCameraSettings();
				cameraInitialised = true;
			}
		}

		const int wheel = mouse.GetWheel();
		if (wheel != 0)
			SetCameraZoom(cameraZoom + static_cast<float>(wheel) * CameraZoomPerWheelNotch);

		camera.Update(fElapsedTime);
	}

	void Game::ResetCamera() {
		camera = olc::utils::Camera2D(ScreenSize(), { cameraZoom, cameraZoom });
		cameraTarget = { 0.0f, 0.0f };
		ApplyCameraSettings();
	}

	void Game::ApplyCameraSettings() {
		camera.SetWorldBoundary(CameraWorldOrigin, CameraWorldSize);
		camera.EnableWorldBoundary(true);
		camera.SetLazyFollowRate(CameraLazyFollowRate);
		camera.SetTarget(cameraTarget);
		camera.SetMode(olc::utils::Camera2D::Mode::Simple);
		camera.Update(0.0f);
		camera.SetMode(olc::utils::Camera2D::Mode::LazyFollow);
	}

	void Game::SetCameraZoom(float zoom) {
		const float clamped = std::clamp(zoom, CameraMinZoom, CameraMaxZoom);
		if (clamped == cameraZoom)
			return;

		cameraZoom = clamped;
		camera = olc::utils::Camera2D(ScreenSize(), { cameraZoom, cameraZoom });
		ApplyCameraSettings();
	}

	void Game::DrawScene() {
		for (const Terrain& ladder : TestLadders()) {
			const olc::Pixel colour(190, 150, 90);
			const float x = ladder.pos.x + ladder.size.x * 0.5f;
			draw.Line({ x, ladder.pos.y }, { x, ladder.pos.y + ladder.size.y }, colour);
			for (float y = ladder.pos.y; y <= ladder.pos.y + ladder.size.y; y += LadderRungSpacing)
				draw.Line({ x - 3.0f, y }, { x + 3.0f, y }, colour);
		}
		for (const Terrain& terrain : TestTerrain())
			draw.FilledRect(terrain.pos, terrain.size, olc::Pixel(65, 80, 95));

		DrawObjects();

		for (const Client::WorldItem& item : client.GetWorldItems())
			if (item.item != Item::None)
				DrawItem(item.item, item.body);
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
				float angle = object.angle;
				client.GetPredictedBody(object.id, alpha, position, angle);
				DrawObject(object, position, angle, olc::Pixel(235, 166, 75));
			}
		}

		if (client.GetStatus() != Client::Status::Connected)
			return;

		DrawRagdoll(client.GetLocalPose(alpha), client.GetLocalRagdoll().facing, olc::Colour::CYAN);
	}

	void Game::DrawObject(const Client::ObjectView& object, const olc::vf2d& position,
		float angle, olc::Pixel colour) {
		if (object.shape != Shape::Polygon || object.vertexCount < 3) {
			draw.FilledCircle(position, object.radius, colour);
			draw.Line(position, position + Rotate({ object.radius, 0.0f }, angle), olc::Pixel(150, 100, 45));
			return;
		}

		std::array<olc::vf2d, MaxPolygonVertices> vertices{};
		for (int i = 0; i < object.vertexCount; ++i)
			vertices[i] = position + Rotate(object.localVertices[i], angle);

		for (int i = 1; i + 1 < object.vertexCount; ++i)
			draw.FilledTriangle(vertices[0], vertices[i], vertices[i + 1], colour);
		for (int i = 0; i < object.vertexCount; ++i)
			draw.Line(vertices[i], vertices[(i + 1) % object.vertexCount], olc::Pixel(150, 100, 45));
	}

	void Game::DrawRagdoll(const std::array<olc::vf2d, PartCount>& pose, float facing, olc::Pixel colour) {
		const olc::Pixel farColour(45, 125, 135);
		const auto drawLimb = [&](RagdollPart root, RagdollPart joint, RagdollPart end, olc::Pixel limbColour) {
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
		draw.FilledCircle(pose[Head] + olc::vf2d{ facing * 4.0f, -1.0f }, 1.5f, olc::Pixel(5, 10, 17));
	}

	void Game::DrawHud() {
		if (client.GetStatus() != Client::Status::Connected)
			return;

		const Inventory& inventory = client.GetLocalRagdoll().inventory;
		const olc::vf2d origin = HudOrigin();
		DrawHotbar(origin, inventory);
		DrawClothingSlots(origin + olc::vf2d(GroupWidth(HotbarSlots) + HotbarGroupSpacing, 0.0f), inventory);
		DrawGrabToggle();

		if (dragging && dragItem != Item::None)
			DrawSlot(mouse.GetPosition() - olc::vf2d(HotbarSlotSize * 0.5f, HotbarSlotSize * 0.5f),
				HotbarSlotSize, dragItem, true);
	}

	olc::vf2d Game::HudOrigin() {
		const float hotbarWidth = GroupWidth(HotbarSlots);
		const float clothingWidth = GroupWidth(static_cast<size_t>(ClothingSlot::Count));
		const float hudWidth = hotbarWidth + HotbarGroupSpacing + clothingWidth
			+ GrabToggleGap + GrabToggleWidth;
		const olc::vi2d screen = ScreenSize();
		return {
			std::floor((static_cast<float>(screen.x) - hudWidth) * 0.5f),
			static_cast<float>(screen.y) - HotbarSlotSize - HotbarBottomMargin
		};
	}

	bool Game::SlotAt(const olc::vf2d& point, bool& clothing, size_t& index) {
		struct Group {
			olc::vf2d origin;
			size_t slots;
			bool clothing;
		};

		const olc::vf2d origin = HudOrigin();
		const Group groups[] = {
			{ origin, HotbarSlots, false },
			{ origin + olc::vf2d(GroupWidth(HotbarSlots) + HotbarGroupSpacing, 0.0f),
				static_cast<size_t>(ClothingSlot::Count), true }
		};

		for (const Group& group : groups) {
			for (size_t i = 0; i < group.slots; ++i) {
				const olc::vf2d slot = SlotOrigin(group.origin, i);
				if (point.x >= slot.x && point.x < slot.x + HotbarSlotSize
					&& point.y >= slot.y && point.y < slot.y + HotbarSlotSize) {
					clothing = group.clothing;
					index = i;
					return true;
				}
			}
		}
		return false;
	}

	void Game::UpdateDrag() {
		const olc::vf2d cursor = mouse.GetPosition();
		if (mouse.GetButton(0).bPressed) {
			bool clothing = false;
			size_t index = 0;
			if (SlotAt(cursor, clothing, index)) {
				const Inventory& inventory = client.GetLocalRagdoll().inventory;
				const Item item = clothing ? inventory.clothing[index] : inventory.hotbar[index];
				if (item != Item::None) {
					dragging = true;
					dragFromClothing = clothing;
					dragIndex = index;
					dragItem = item;
				}
			}
			return;
		}

		if (!dragging || mouse.GetButton(0).bHeld)
			return;

		dragging = false;
		bool clothing = false;
		size_t index = 0;
		if (SlotAt(cursor, clothing, index))
			client.MoveItem(dragFromClothing, dragIndex, clothing, index);
		else
			client.DropItem(dragFromClothing, dragIndex, camera.GetWorldTransform().inverse(cursor));
		dragItem = Item::None;
	}

	void Game::DrawItem(Item item, const ObjectState& body) {
		if (body.shape != Shape::Polygon || body.vertexCount < 3) {
			draw.FilledCircle(body.position, body.radius, ItemColour(item));
			draw.Line(body.position, body.position + Rotate({ body.radius, 0.0f }, body.angle),
				olc::Pixel(30, 35, 40));
			return;
		}

		std::array<olc::vf2d, MaxPolygonVertices> vertices{};
		for (int i = 0; i < body.vertexCount; ++i)
			vertices[i] = body.position + Rotate(body.localVertices[i], body.angle);
		for (int i = 1; i + 1 < body.vertexCount; ++i)
			draw.FilledTriangle(vertices[0], vertices[i], vertices[i + 1], ItemColour(item));
	}

	void Game::DrawSlot(const olc::vf2d& origin, float size, Item item, bool highlighted) {
		draw.FilledRect(origin, { size, size }, olc::Pixel(18, 24, 32));
		if (item != Item::None)
			draw.FilledRect(origin + olc::vf2d(4.0f, 4.0f), { size - 8.0f, size - 8.0f }, ItemColour(item));
		if (highlighted)
			draw.Rect(origin - olc::vf2d(2.0f, 2.0f), { size + 4.0f, size + 4.0f }, olc::Pixel(215, 230, 245));
		draw.Rect(origin, { size, size }, highlighted ? olc::Pixel(160, 190, 215) : olc::Pixel(70, 85, 100));
	}

	void Game::DrawHotbar(const olc::vf2d& origin, const Inventory& inventory) {
		for (size_t i = 0; i < HotbarSlots; ++i) {
			const olc::vf2d slot = SlotOrigin(origin, i);
			const Item shown = dragging && !dragFromClothing && dragIndex == i
				? Item::None : inventory.hotbar[i];
			DrawSlot(slot, HotbarSlotSize, shown, i == inventory.selected);
			draw.StringProp(slot + olc::vf2d(3.0f, 2.0f), std::to_string((i + 1) % 10),
				olc::Pixel(145, 165, 185));
		}

		const Item selected = inventory.hotbar[inventory.selected];
		if (selected != Item::None)
			draw.StringProp(origin + olc::vf2d(0.0f, -16.0f), ItemName(selected), olc::Pixel(215, 230, 245));
	}

	void Game::DrawClothingSlots(const olc::vf2d& origin, const Inventory& inventory) {
		for (size_t i = 0; i < inventory.clothing.size(); ++i) {
			const olc::vf2d slot = SlotOrigin(origin, i);
			const Item shown = dragging && dragFromClothing && dragIndex == i
				? Item::None : inventory.clothing[i];
			DrawSlot(slot, HotbarSlotSize, shown, false);
			draw.StringProp(slot + olc::vf2d(3.0f, -16.0f), ClothingName(static_cast<ClothingSlot>(i)),
				olc::Pixel(145, 165, 185));
		}
	}

	olc::vf2d Game::GrabToggleOrigin() {
		const float x = GroupWidth(HotbarSlots) + HotbarGroupSpacing
			+ GroupWidth(static_cast<size_t>(ClothingSlot::Count)) + GrabToggleGap;
		return HudOrigin() + olc::vf2d(x, 0.0f);
	}

	bool Game::GrabToggleAt(const olc::vf2d& point) {
		const olc::vf2d origin = GrabToggleOrigin();
		return point.x >= origin.x && point.x < origin.x + GrabToggleWidth
			&& point.y >= origin.y && point.y < origin.y + HotbarSlotSize;
	}

	void Game::DrawGrabToggle() {
		const olc::vf2d origin = GrabToggleOrigin();
		const bool hovered = GrabToggleAt(mouse.GetPosition());
		draw.FilledRect(origin, { GrabToggleWidth, HotbarSlotSize },
			grabsEnabled ? olc::Pixel(32, 74, 92) : olc::Pixel(24, 32, 42));
		draw.Rect(origin, { GrabToggleWidth, HotbarSlotSize },
			hovered ? olc::Pixel(215, 230, 245) : olc::Pixel(70, 85, 100));
		draw.StringProp(origin + olc::vf2d(8.0f, 8.0f), "GRAB", olc::Pixel(215, 230, 245));
		draw.StringProp(origin + olc::vf2d(8.0f, 24.0f), grabsEnabled ? "ON" : "OFF",
			grabsEnabled ? olc::Pixel(150, 200, 220) : olc::Pixel(190, 140, 140));
	}
}
