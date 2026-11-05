#define IMGUI_DEFINE_MATH_OPERATORS
#include "Game.h"
#include "Ui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace Trauma {
	namespace {
		constexpr float MaxFrameTime = 0.1f;
		constexpr int MaxFixedStepsPerFrame = 5;

		constexpr float HotbarSlotSize = 46.0f;
		constexpr float HotbarSlotGap = 4.0f;
		constexpr float ContainerButtonWidth = 26.0f;
		constexpr float ContainerButtonHeight = 17.0f;
		constexpr float HotbarGroupSpacing = HotbarSlotGap * 3.0f;
		constexpr float HotbarBottomMargin = 12.0f;

		constexpr float GrabToggleWidth = 92.0f;
		constexpr float SlotCaptionFontSize = 12.0f;
		constexpr float DropOffset = 4.0f;

		constexpr float CameraLazyFollowRate = 9.0f;
		constexpr float CameraMinZoom = 0.8f;
		constexpr float CameraMaxZoom = 4.0f;
		const olc::vf2d CameraWorldOrigin = { -4000.0f, -4000.0f };
		const olc::vf2d CameraWorldSize = { 8000.0f, 8000.0f };

		constexpr const char* ItemPayload = "trauma/item";

		constexpr ImGuiWindowFlags OverlayFlags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

		constexpr ImU32 SlotFill = IM_COL32(18, 24, 32, 215);
		constexpr ImU32 SlotBorder = IM_COL32(70, 85, 100, 255);
		constexpr ImU32 SlotHoverBorder = IM_COL32(215, 230, 245, 255);
		constexpr ImU32 SlotSelectedBorder = IM_COL32(160, 190, 215, 255);
		constexpr ImU32 SlotSelectedHighlight = IM_COL32(215, 230, 245, 255);
		constexpr ImU32 SlotCaption = IM_COL32(145, 165, 185, 255);
		constexpr ImU32 SlotQuantity = IM_COL32(235, 240, 245, 255);
		constexpr ImVec4 HudText = ImVec4(0.84f, 0.90f, 0.96f, 1.0f);
		constexpr ImVec4 HudMuted = ImVec4(0.57f, 0.65f, 0.73f, 1.0f);

		ImU32 ToImColour(olc::Pixel colour, float alpha = 1.0f) {
			return IM_COL32(colour.r, colour.g, colour.b,
				static_cast<int>(alpha * 255.0f + 0.5f));
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
		const float deltaTime = std::min(fElapsedTime, MaxFrameTime);
		Ui::BeginFrame(*this, deltaTime);

		if (keyboard.GetKey(olc::Key::ESCAPE).bPressed) {
			paused = !paused;
			dropMode = false;
			shelfMode = false;
			movingShelfActive = false;
		}

		UpdateClientInput(deltaTime);
		client.Poll(deltaTime);

		UpdateLocalRenderState();
		UpdateRenderPosition();
		UpdateCamera(deltaTime);

		if (!paused)
			UpdateDropPreview();

		draw.Clear(olc::Pixel(5, 10, 17));
		draw.SetWorldTransform(camera.GetWorldTransform());
		DrawScene();
		draw.WorldReset();

		BuildUi();
		Ui::EndFrame(*this);
		return !quitting;
	}

	void Game::LeavePlay() {
		client.Disconnect();
		fixedAccumulator = 0.0f;
		localRenderReady = false;
		jumpQueued = false;
		paused = false;
		drag = ItemDrag{};
		openContainers.clear();
		dropMode = false;
		shelfMode = false;
		movingShelfActive = false;
	}

	void Game::UpdateClientInput(float deltaTime) {
		if (!paused && keyboard.GetKey(olc::Key::R).bPressed
			&& client.GetStatus() == Client::Status::Disconnected)
			client.Connect(host, port);

		if (client.GetStatus() != Client::Status::Connected) {
			fixedAccumulator = 0.0f;
			jumpQueued = false;
			drag = ItemDrag{};
			openContainers.clear();
			dropMode = false;
			shelfMode = false;
			movingShelfActive = false;
			return;
		}

		if (!paused) {
			UpdateInventoryInput();
			UpdateGrabMode();
			UpdateGunFire();
			UpdateDropMode();
		}

		jumpQueued = jumpQueued || (!paused && keyboard.GetKey(olc::Key::SPACE).bPressed);
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
			if (keyboard.GetKey(slotKeys[i]).bPressed) {
				hotbarSelection = static_cast<uint8_t>(i);
				dropMode = false;
				shelfMode = false;
				movingShelfActive = false;
			}

		const int wheel = mouse.GetWheel();
		if (wheel != 0) {
			const int slotCount = static_cast<int>(HotbarSlots);
			const int step = wheel > 0 ? -1 : 1;
			const int selected = (static_cast<int>(hotbarSelection) + step + slotCount) % slotCount;
			hotbarSelection = static_cast<uint8_t>(selected);
			dropMode = false;
			shelfMode = false;
			movingShelfActive = false;
		}

		if (keyboard.GetKey(olc::Key::E).bPressed) {
			Client::PickupTarget target;
			if (client.GetPickupCandidate(target))
				client.PickupItem(target);
		}
	}

	void Game::UpdateGrabMode() {
		if (keyboard.GetKey(olc::Key::G).bPressed)
			grabsEnabled = !grabsEnabled;
	}

	void Game::UpdateGunFire() {
		const Inventory& inventory = client.GetLocalRagdoll().inventory;
		if (!IsGun(inventory.hotbar[hotbarSelection].item) || dropMode || shelfMode
			|| drag.active || Ui::WantsMouse() || keyboard.GetKey(olc::Key::Q).bPressed
			|| !mouse.GetButton(0).bPressed)
			return;
		client.FireGun(hotbarSelection, camera.GetWorldTransform().inverse(mouse.GetPosition()));
	}

	void Game::UpdateDropMode() {
		const ItemStack& selected = client.GetLocalRagdoll().inventory.hotbar[hotbarSelection];
		if (keyboard.GetKey(olc::Key::Q).bPressed) {
			if (IsShelf(selected.item)) {
				shelfMode = !shelfMode;
				dropMode = false;
			} else {
				dropMode = !dropMode && selected.item != ItemNone;
				shelfMode = false;
			}
			movingShelfActive = false;
		}

		if (shelfMode) {
			if (!IsShelf(selected.item)) {
				shelfMode = false;
				movingShelfActive = false;
				return;
			}

			if (dropPreview.active && !Ui::WantsMouse() && mouse.GetButton(0).bPressed) {
				if (movingShelfActive) {
					client.MoveShelf(movingShelf, dropPreview.position);
					movingShelfActive = false;
				} else {
					const olc::vf2d cursor = camera.GetWorldTransform().inverse(mouse.GetPosition());
					const int index = client.ShelfAt(cursor);
					const olc::vf2d hand = DropHandOrigin(client.GetLocalRagdoll(), cursor);
					if (index >= 0 && (client.GetShelves()[index].position - hand).mag() <= ShelfPlaceRange) {
						movingShelf = static_cast<uint8_t>(index);
						movingShelfActive = true;
					} else {
						client.PlaceShelf(dropPreview.position);
					}
				}
			}
			return;
		}

		if (!dropMode)
			return;

		if (selected.item == ItemNone) {
			dropMode = false;
			return;
		}

		if (dropPreview.active && !Ui::WantsMouse() && mouse.GetButton(0).bPressed) {
			client.DropItem(HotbarAddress(hotbarSelection), dropPreview.position);
			dropMode = false;
		}
	}

	void Game::UpdateDropPreview() {
		dropPreview = DropPreview{};
		const ItemStack& selected = client.GetLocalRagdoll().inventory.hotbar[hotbarSelection];
		const ItemId item = drag.active ? drag.item : selected.item;
		const bool shelfPreview = drag.active ? IsShelf(drag.item) : shelfMode;
		if ((!drag.active && !dropMode && !shelfPreview) || item == ItemNone)
			return;

		if (Ui::WantsMouse())
			return;

		const olc::vf2d pointer = mouse.GetPosition();
		if (pointer.x >= hudMin.x && pointer.x <= hudMax.x
			&& pointer.y >= hudMin.y && pointer.y <= hudMax.y)
			return;

		const olc::vf2d target = camera.GetWorldTransform().inverse(pointer);
		const olc::vf2d hand = DropHandOrigin(client.GetLocalRagdoll(), target);
		if (shelfPreview) {
			olc::vf2d offset = target - hand;
			const float distance = offset.mag();
			if (distance > ShelfPlaceRange)
				offset *= ShelfPlaceRange / distance;
			dropPreview.active = true;
			dropPreview.item = item;
			dropPreview.shelf = true;
			dropPreview.position = hand + offset;
			dropPreview.moving = movingShelfActive;
			dropPreview.movingShelf = movingShelf;
			return;
		}

		const ObjectState ghost = CreateItemBody(item, target);
		if (ghost.radius <= 0.0f)
			return;

		std::array<ObjectState, MaxWorldItems + MaxPlayers> obstacles{};
		size_t obstacleCount = 0;
		for (const Client::WorldItem& worldItem : client.GetWorldItems())
			if (worldItem.stack.item != ItemNone && obstacleCount < obstacles.size())
				obstacles[obstacleCount++] = worldItem.body;
		for (const Client::ObjectView& view : client.GetObjects()) {
			if (obstacleCount >= obstacles.size())
				break;
			if (view.shape == Shape::Circle) {
				obstacles[obstacleCount++] = CreateCircle(view.position, view.radius, 1.0f);
				continue;
			}
			if (view.vertexCount < 3)
				continue;
			obstacles[obstacleCount++] = CreatePolygon(view.position,
				std::vector<olc::vf2d>(view.localVertices.begin(), view.localVertices.begin() + view.vertexCount),
				1.0f, view.angle);
		}

		olc::vf2d offset = target - hand;
		const float distance = offset.mag();
		if (distance > DropOffset)
			offset *= DropOffset / distance;

		dropPreview.active = true;
		dropPreview.item = item;
		dropPreview.position = ClampDropPosition(hand, hand + offset, ghost.radius,
			obstacles.data(), obstacleCount);
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
		if (paused) {
			input.aim = { 1.0f, 0.0f };
			return input;
		}

		input.jump = bJump;
		input.run = keyboard.GetKey(olc::Key::SHIFT).bHeld;
		input.crouch = keyboard.GetKey(olc::Key::CTRL).bHeld;
		input.crawl = keyboard.GetKey(olc::Key::C).bHeld;

		const bool pointerOnUi = Ui::WantsMouse();
		input.reachLeft = mouse.GetButton(0).bHeld && !pointerOnUi;
		input.reachRight = mouse.GetButton(1).bHeld && !pointerOnUi;
		input.grab = grabsEnabled;

		const olc::vf2d mouseWorld = camera.GetWorldTransform().inverse(mouse.GetPosition());
		const olc::vf2d aim = mouseWorld - client.GetLocalRagdoll().parts[Torso].position;
		input.aim = { std::clamp(aim.x, -2000.0f, 2000.0f), std::clamp(aim.y, -2000.0f, 2000.0f) };

		if ((drag.active || dropMode || shelfMode) && dropPreview.active) {
			input.reachLeft = false;
			input.reachRight = true;
			input.grab = false;
		}

		if (IsGun(client.GetLocalRagdoll().inventory.hotbar[hotbarSelection].item)) {
			input.reachLeft = false;
			input.reachRight = true;
			input.grab = false;
		}

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

		const float alpha = std::clamp(fixedAccumulator / FixedDeltaTime, 0.0f, 1.0f);
		for (size_t index = 0; index < MaxShelves; ++index) {
			const Client::ShelfView& shelf = client.GetShelves()[index];
			if (!shelf.active || (movingShelfActive && index == movingShelf && dropPreview.active))
				continue;
			const olc::vf2d shelfPosition = shelf.previousPosition.lerp(shelf.position, alpha);
			DrawShelfBoards(shelfPosition, olc::Pixel(135, 98, 62));
			for (const Client::ShelfItemView& item : shelf.items) {
				if (item.stack.item == ItemNone)
					continue;
				ObjectState body = item.body;
				body.position = shelfPosition + item.previousPosition.lerp(item.body.position, alpha);
				body.angle = item.previousAngle
					+ std::remainder(item.body.angle - item.previousAngle, TwoPi) * alpha;
				DrawItem(item.stack.item, body);
			}
		}

		DrawObjects();
		DrawShotTracers();

		for (const Client::WorldItem& item : client.GetWorldItems())
			if (item.stack.item != ItemNone)
				DrawItem(item.stack.item, item.body);

		DrawDropPreview();
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

	void Game::DrawShotTracers() {
		for (const Client::ShotTracer& shot : client.GetShotTracers()) {
			draw.Line(shot.start, shot.end, olc::Pixel(255, 220, 130));
			draw.FilledCircle(shot.start, 2.0f, olc::Pixel(255, 235, 170));
		}
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

	void Game::DrawItem(ItemId item, const ObjectState& body) {
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

	void Game::DrawShelfBoards(const olc::vf2d& position, olc::Pixel colour) {
		const olc::Pixel edge(65, 45, 28);
		for (const ObjectState& board : ShelfBoards()) {
			const olc::vf2d topLeft = position + board.position - board.halfExtents;
			const olc::vf2d size = board.halfExtents * 2.0f;
			draw.FilledRect(topLeft, size, colour);
			draw.Line(topLeft, topLeft + olc::vf2d{ size.x, 0.0f }, edge);
		}
	}

	void Game::DrawDropPreview() {
		if (!dropPreview.active)
			return;

		if (dropPreview.shelf) {
			DrawShelfBoards(dropPreview.position, olc::Pixel(225, 235, 245));
			if (dropPreview.moving && dropPreview.movingShelf < MaxShelves) {
				const Client::ShelfView& shelf = client.GetShelves()[dropPreview.movingShelf];
				if (shelf.active)
					for (const Client::ShelfItemView& item : shelf.items) {
						if (item.stack.item == ItemNone)
							continue;
						ObjectState body = item.body;
						body.position += dropPreview.position;
						DrawItem(item.stack.item, body);
					}
			}
			return;
		}

		const ObjectState body = CreateItemBody(dropPreview.item, dropPreview.position);
		if (body.radius <= 0.0f)
			return;

		const olc::Pixel colour(225, 235, 245);
		if (body.shape != Shape::Polygon || body.vertexCount < 3) {
			constexpr int facets = 20;
			olc::vf2d previous{ body.position.x + body.radius, body.position.y };
			for (int i = 1; i <= facets; ++i) {
				const float turn = TwoPi * static_cast<float>(i) / static_cast<float>(facets);
				const olc::vf2d next{ body.position.x + std::cos(turn) * body.radius,
					body.position.y + std::sin(turn) * body.radius };
				draw.Line(previous, next, colour);
				previous = next;
			}
			return;
		}

		const Polygon outline = WorldVertices(body);
		for (int i = 0; i < outline.count; ++i)
			draw.Line(outline.vertices[i], outline.vertices[(i + 1) % outline.count], colour);
	}

	void Game::BuildUi() {
		dragDelivered = false;
		BuildStatus();
		if (client.GetStatus() == Client::Status::Connected) {
			BuildHud();
			BuildPickupLabel();
			if (dropMode && client.GetLocalRagdoll().inventory.hotbar[hotbarSelection].item != ItemNone)
				DrawItemGhost(client.GetLocalRagdoll().inventory.hotbar[hotbarSelection].item);
		}
		if (paused)
			BuildPauseMenu();
	}

	void Game::BuildPickupLabel() {
		Client::PickupTarget target;
		if (!client.GetPickupCandidate(target))
			return;

		ItemId item = ItemNone;
		olc::vf2d worldPosition;
		float radius = 0.0f;
		if (target.shelf) {
			if (target.index >= MaxShelves || target.item >= MaxShelfItems)
				return;
			const Client::ShelfView& shelf = client.GetShelves()[target.index];
			if (!shelf.active)
				return;
			const Client::ShelfItemView& view = shelf.items[target.item];
			item = view.stack.item;
			worldPosition = shelf.position + view.body.position;
			radius = view.body.radius;
		} else {
			if (target.index >= MaxWorldItems)
				return;
			const Client::WorldItem& view = client.GetWorldItems()[target.index];
			item = view.stack.item;
			worldPosition = view.body.position;
			radius = view.body.radius;
		}
		if (item == ItemNone)
			return;

		const olc::vf2d screen = camera.GetWorldTransform().forwardRound(
			worldPosition - olc::vf2d{ 0.0f, radius + 10.0f });
		const char* name = ItemName(item);
		const ImVec2 size = ImGui::CalcTextSize(name);
		const ImVec2 position(screen.x - size.x * 0.5f, screen.y - size.y);
		ImDrawList* foreground = ImGui::GetForegroundDrawList();
		foreground->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(5, 10, 17, 220), name);
		foreground->AddText(position, SlotCaption, name);
	}

	void Game::BuildStatus() {
		ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		ImGui::Begin("##status", nullptr, OverlayFlags | ImGuiWindowFlags_AlwaysAutoResize);
		switch (client.GetStatus()) {
			case Client::Status::Connected:
				ImGui::TextColored(ImVec4(0.59f, 0.78f, 0.86f, 1.0f), "connected");
				break;
			case Client::Status::Connecting:
				ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.43f, 1.0f), "connecting...");
				break;
			default:
				ImGui::TextColored(ImVec4(0.75f, 0.55f, 0.55f, 1.0f), "disconnected");
				ImGui::TextColored(HudMuted, "press R to connect");
				break;
		}
		ImGui::TextColored(HudMuted, "%.0f fps", ImGui::GetIO().Framerate);
		ImGui::End();
	}

	void Game::BuildHud() {
		const Inventory& inventory = client.GetLocalRagdoll().inventory;
		ImGuiIO& io = ImGui::GetIO();

		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - HotbarBottomMargin),
			ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(HotbarSlotGap, HotbarSlotGap));
		ImGui::Begin("##hud", nullptr,
			OverlayFlags | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
		hudMin = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y };
		hudMax = { hudMin.x + ImGui::GetWindowSize().x, hudMin.y + ImGui::GetWindowSize().y };

		std::array<SlotResult, HotbarSlots> hotbarResults{};

		const bool hasHotbarContainer = std::any_of(inventory.hotbar.begin(), inventory.hotbar.end(),
			[](const ItemStack& stack) { return ItemContainerCapacity(stack.item) > 0; });

		const ItemStack& held = inventory.hotbar[inventory.selected];
		if (IsGun(held.item)) {
			const ItemStack* magazine = LoadedMagazine(inventory, inventory.selected);
			if (magazine)
				ImGui::TextColored(HudText, "gun  magazine x%u",
					static_cast<unsigned>(magazine->quantity));
			else
				ImGui::TextColored(HudText, "gun  no magazine");
		}
		else if (IsShelf(held.item))
			ImGui::TextColored(HudText, "%s x%u", ItemName(held.item),
				static_cast<unsigned>(inventory.shelfKits));
		else if (ItemContainerCapacity(held.item) > 0) {
			const ItemContainerDefinition& definition = ItemContainerInfo(held.item);
			ImGui::TextColored(HudText, "%s  %ux%u", ItemName(held.item),
				static_cast<unsigned>(definition.width),
				static_cast<unsigned>(definition.height));
		}
		else
			ImGui::TextColored(HudText, "%s", held.item == ItemNone ? " " : ItemName(held.item));

		if (hasHotbarContainer)
			ImGui::Dummy(ImVec2(1.0f, ContainerButtonHeight));

		for (size_t index = 0; index < HotbarSlots; ++index) {
			if (index > 0)
				ImGui::SameLine();
			char id[16] = {};
			char caption[4] = {};
			std::snprintf(id, sizeof(id), "hotbar%zu", index);
			std::snprintf(caption, sizeof(caption), "%zu", (index + 1) % 10);

			hotbarResults[index] = BuildSlot(id, inventory, HotbarAddress(index),
				inventory.hotbar[index], caption, index == inventory.selected);
			const SlotResult& result = hotbarResults[index];
			if (result.clicked) {
				if (dropMode)
					client.MoveItem(HotbarAddress(hotbarSelection), HotbarAddress(index));
				hotbarSelection = static_cast<uint8_t>(index);
				dropMode = false;
				shelfMode = false;
				movingShelfActive = false;
			}
			if (result.dropped) {
				client.MoveItem(result.payload.source, result.dropTarget);
				dragDelivered = true;
			}
		}

		ImGui::SameLine(0.0f, HotbarGroupSpacing);
		for (size_t index = 0; index < inventory.clothing.size(); ++index) {
			if (index > 0)
				ImGui::SameLine();
			char id[16] = {};
			std::snprintf(id, sizeof(id), "clothing%zu", index);

			const SlotResult result = BuildSlot(id, inventory, ClothingAddress(index),
				inventory.clothing[index], SlotKindName(ClothingKind(index)), false);
			if (result.dropped) {
				client.MoveItem(result.payload.source, result.dropTarget);
				dragDelivered = true;
			}
		}

		ImGui::SameLine(0.0f, HotbarGroupSpacing);
		if (ImGui::Button(grabsEnabled ? "GRAB\nON" : "GRAB\nOFF", ImVec2(GrabToggleWidth, HotbarSlotSize)))
			grabsEnabled = !grabsEnabled;

		const ImVec2 hudCursor = ImGui::GetCursorScreenPos();
		for (size_t index = 0; index < HotbarSlots; ++index) {
			const ItemStack& item = inventory.hotbar[index];
			if (ItemContainerCapacity(item.item) == 0)
				continue;

			const SlotAddress address = HotbarAddress(index);
			const olc::vf2d slotMin = hotbarResults[index].screenMin;
			const ImVec2 buttonMin(slotMin.x + (HotbarSlotSize - ContainerButtonWidth) * 0.5f,
				slotMin.y - ContainerButtonHeight);
			const ImVec2 buttonMax(buttonMin.x + ContainerButtonWidth, slotMin.y);
			const olc::vf2d windowAnchor(slotMin.x + HotbarSlotSize * 0.5f,
				buttonMin.y - 6.0f);

			ImGui::PushID(static_cast<int>(index));
			ImGui::SetCursorScreenPos(buttonMin);
			ImGui::InvisibleButton("##containerToggle",
				ImVec2(ContainerButtonWidth, ContainerButtonHeight));
			const bool buttonHovered = ImGui::IsItemHovered();
			const bool buttonClicked = ImGui::IsItemClicked()
				|| ImGui::IsItemClicked(ImGuiMouseButton_Right);
			if (buttonClicked)
				ToggleContainer(address, windowAnchor);
			ImGui::PopID();

			const bool isOpen = IsContainerOpen(address);
			ImDrawList* list = ImGui::GetWindowDrawList();
			const ImU32 fill = isOpen ? IM_COL32(82, 125, 153, 245)
				: buttonHovered ? IM_COL32(58, 78, 96, 245)
				: IM_COL32(44, 57, 70, 235);
			const ImU32 border = (isOpen || buttonHovered) ? SlotHoverBorder : SlotBorder;
			list->AddRectFilled(buttonMin, buttonMax, fill);
			list->AddRect(buttonMin, buttonMax, border);
			const ImVec2 labelSize = ImGui::CalcTextSize("INV");
			list->AddText(ImVec2(buttonMin.x + (ContainerButtonWidth - labelSize.x) * 0.5f,
				buttonMin.y + (ContainerButtonHeight - labelSize.y) * 0.5f),
				IM_COL32(235, 240, 245, 255), "INV");
			if (buttonHovered)
				ImGui::SetTooltip(isOpen ? "Close %s inventory" : "Open %s inventory",
					ItemName(item.item));
		}
		ImGui::SetCursorScreenPos(hudCursor);

		ImGui::End();
		ImGui::PopStyleVar(3);

		openContainers.erase(std::remove_if(openContainers.begin(), openContainers.end(),
			[&](const OpenContainerWindow& open) {
				const ItemStack* owner = ResolveSlot(inventory, open.address);
				return !owner || ItemContainerCapacity(owner->item) == 0;
			}), openContainers.end());

		const std::vector<OpenContainerWindow> openWindows = openContainers;
		for (const OpenContainerWindow& open : openWindows)
			if (IsContainerOpen(open.address))
				BuildContainerWindow(inventory, open.address, open.anchor);

		ResolveItemDrag();
	}

	Game::SlotResult Game::BuildSlot(const char* id, const Inventory& inventory,
		const SlotAddress& address, const ItemStack& stack, const char* caption, bool selected) {
		SlotResult result;
		result.dropTarget = address;
		ImGui::PushID(id);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 extent(HotbarSlotSize, HotbarSlotSize);
		const ImVec2 corner = origin + extent;
		ImGui::InvisibleButton("##slot", extent);
		const bool hovered = ImGui::IsItemHovered();
		result.hovered = hovered;
		result.clicked = ImGui::IsItemClicked();

		const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		if (rightClicked && stack.item != ItemNone && ItemContainerCapacity(stack.item) > 0) {
			const bool hasContainerButton = address.kind == SlotKind::Hotbar && address.depth == 0;
			ToggleContainer(address, olc::vf2d{
				origin.x + HotbarSlotSize * 0.5f,
				origin.y - (hasContainerButton ? ContainerButtonHeight : 0.0f) - 6.0f });
		}

		if (stack.item != ItemNone) {
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
				ItemDrag payload;
				payload.active = true;
				payload.source = address;
				payload.item = stack.item;
				ImGui::SetDragDropPayload(ItemPayload, &payload, sizeof(payload));
				drag = payload;
				ImGui::EndDragDropSource();
			}
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(ItemPayload)) {
				result.dropped = true;
				result.payload = *static_cast<const ItemDrag*>(accepted->Data);
				if (stack.item != ItemNone && ItemContainerCapacity(stack.item) > 0) {
					const ItemStack* dragged = ResolveSlot(inventory, result.payload.source);
					if (dragged && dragged->item != ItemNone) {
						const size_t capacity = ItemContainerCapacity(stack.item);
						for (size_t cell = 0; cell < capacity; ++cell) {
							SlotAddress child;
							if (!ChildSlot(address, cell, child))
								continue;
							const ItemStack* occupant = ResolveSlot(inventory, child);
							if (!occupant || occupant->item != ItemNone)
								continue;
							if (!CanMoveStackTo(inventory, result.payload.source, child, *dragged))
								continue;
							result.dropTarget = child;
							break;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImDrawList* list = ImGui::GetWindowDrawList();
		result.screenMin = { origin.x, origin.y };
		result.screenMax = { corner.x, corner.y };
		list->AddRectFilled(origin, corner, SlotFill);
		if (stack.item != ItemNone) {
			list->AddRectFilled(origin + ImVec2(4.0f, 4.0f), corner - ImVec2(4.0f, 4.0f),
				ToImColour(ItemColour(stack.item)));
		}
		if (selected)
			list->AddRect(origin - ImVec2(2.0f, 2.0f), corner + ImVec2(2.0f, 2.0f), SlotSelectedHighlight);
		list->AddRect(origin, corner,
			hovered ? SlotHoverBorder : (selected ? SlotSelectedBorder : SlotBorder));
		list->PushClipRect(origin, corner, true);
		list->AddText(ImGui::GetFont(), SlotCaptionFontSize, origin + ImVec2(3.0f, 1.0f), SlotCaption, caption);
		const uint8_t count = IsShelf(stack.item) ? inventory.shelfKits : stack.quantity;
		if (count > 0) {
			char quantity[8] = {};
			std::snprintf(quantity, sizeof(quantity), "x%u", static_cast<unsigned>(count));
			const ImVec2 size = ImGui::CalcTextSize(quantity);
			list->AddText(ImGui::GetFont(), SlotCaptionFontSize,
				corner - size - ImVec2(4.0f, 2.0f), SlotQuantity, quantity);
		}
		list->PopClipRect();

		if (hovered && stack.item != ItemNone) {
			const char* name = ItemName(stack.item);
			if (IsShelf(stack.item))
				ImGui::SetTooltip("%s x%u", name, static_cast<unsigned>(inventory.shelfKits));
			else if (stack.quantity > 0)
				ImGui::SetTooltip("%s x%u", name, static_cast<unsigned>(stack.quantity));
			else
				ImGui::SetTooltip("%s", name);
		}

		ImGui::PopID();
		return result;
	}

	bool Game::IsContainerOpen(const SlotAddress& address) const {
		return std::any_of(openContainers.begin(), openContainers.end(),
			[&](const OpenContainerWindow& open) { return SameSlot(open.address, address); });
	}

	void Game::ToggleContainer(const SlotAddress& address, const olc::vf2d& anchor) {
		const auto existing = std::find_if(openContainers.begin(), openContainers.end(),
			[&](const OpenContainerWindow& open) { return SameSlot(open.address, address); });
		if (existing != openContainers.end()) {
			openContainers.erase(existing);
			return;
		}
		openContainers.push_back({ address, anchor });
	}

	void Game::CloseContainer(const SlotAddress& address) {
		openContainers.erase(std::remove_if(openContainers.begin(), openContainers.end(),
			[&](const OpenContainerWindow& open) { return SameSlot(open.address, address); }),
			openContainers.end());
	}

	void Game::BuildContainerWindow(const Inventory& inventory, const SlotAddress& address,
		const olc::vf2d& anchor) {
		const SlotAddress ownerAddress = address;
		const ItemStack* owner = ResolveSlot(inventory, ownerAddress);
		if (!owner || ItemContainerCapacity(owner->item) == 0)
			return;

		const ItemContainerDefinition& definition = ItemContainerInfo(owner->item);
		std::string title = definition.title.empty() ? "Inventory" : definition.title;
		title += "###itemInventory_";
		title += std::to_string(static_cast<unsigned>(ownerAddress.kind));
		title += "_";
		title += std::to_string(ownerAddress.index);
		title += "_";
		title += std::to_string(ownerAddress.depth);
		for (size_t i = 0; i < ownerAddress.depth; ++i) {
			title += "_";
			title += std::to_string(ownerAddress.path[i]);
		}

		bool open = true;
		ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y), ImGuiCond_Appearing, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.94f);
		ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(HotbarSlotGap, HotbarSlotGap));

		const size_t sectionCount = std::min<size_t>(
			definition.sectionCount > 0 ? definition.sectionCount : 1, MaxItemContainerSections);
		for (size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex) {
			ItemContainerSection section;
			if (definition.sectionCount > 0)
				section = definition.sections[sectionIndex];
			else {
				section.width = definition.width;
				section.height = definition.height;
			}

			if (sectionIndex > 0)
				ImGui::Spacing();
			if (!section.name.empty())
				ImGui::TextColored(HudMuted, "%s", section.name.c_str());

			for (uint8_t row = 0; row < section.height; ++row) {
				for (uint8_t column = 0; column < section.width; ++column) {
					if (column > 0)
						ImGui::SameLine(0.0f, HotbarSlotGap);
					const size_t x = static_cast<size_t>(section.x) + column;
					const size_t y = static_cast<size_t>(section.y) + row;
					if (x >= definition.width || y >= definition.height)
						continue;
					const size_t cell = y * static_cast<size_t>(definition.width) + x;
					if (cell >= ItemContainerCapacity(owner->item))
						continue;
					SlotAddress address;
					if (!ChildSlot(ownerAddress, cell, address))
						continue;
					const ItemStack empty;
					const ItemStack& stack = owner->container ? owner->container->slots[cell] : empty;
					char id[32] = {};
					std::snprintf(id, sizeof(id), "container%zu", cell);
					const SlotResult result = BuildSlot(id, inventory, address, stack,
						IsGun(owner->item) && cell == 0 ? "MAG" : "", false);
					if (result.dropped) {
						client.MoveItem(result.payload.source, result.dropTarget);
						dragDelivered = true;
					}
					if (result.clicked && dropMode) {
						client.MoveItem(HotbarAddress(hotbarSelection), address);
						dropMode = false;
					}
				}
			}
		}

		ImGui::PopStyleVar();
		ImGui::End();

		if (!open)
			CloseContainer(ownerAddress);
	}

	void Game::DrawItemGhost(ItemId item) {
		ImDrawList* foreground = ImGui::GetForegroundDrawList();
		const ImVec2 pointer = ImGui::GetIO().MousePos;
		const ImVec2 half(HotbarSlotSize * 0.5f, HotbarSlotSize * 0.5f);
		foreground->AddRectFilled(pointer - half, pointer + half, SlotFill);
		foreground->AddRectFilled(pointer - half + ImVec2(4.0f, 4.0f),
			pointer + half - ImVec2(4.0f, 4.0f), ToImColour(ItemColour(item)));
		foreground->AddRect(pointer - half, pointer + half, SlotHoverBorder);
	}

	void Game::ResolveItemDrag() {
		if (!drag.active)
			return;

		DrawItemGhost(drag.item);

		if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			return;

		if (!dragDelivered && dropPreview.active) {
			if (IsShelf(drag.item))
				client.PlaceShelf(dropPreview.position);
			else
				client.DropItem(drag.source, dropPreview.position);
		}
		drag = ItemDrag{};
	}

	void Game::BuildPauseMenu() {
		ImGuiIO& io = ImGui::GetIO();

		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.04f, 0.07f, 0.55f));
		ImGui::Begin("##dim", nullptr, OverlayFlags);
		ImGui::End();
		ImGui::PopStyleColor();

		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
			ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("Paused", nullptr,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

		const ImVec2 buttonSize(200.0f, 0.0f);
		if (ImGui::Button("Resume", buttonSize))
			paused = false;
		if (ImGui::Button("Disconnect", buttonSize))
			LeavePlay();
		if (ImGui::Button("Quit", buttonSize)) {
			LeavePlay();
			quitting = true;
		}

		ImGui::End();
	}
}
