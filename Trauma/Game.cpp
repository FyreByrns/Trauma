#include "Game.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <utility>

namespace Trauma {
	namespace {
		constexpr int MenuOptionCount = 3;
		constexpr olc::vf2d MenuButtonSize = { 360.0f, 56.0f };
		constexpr float MenuButtonSpacing = 70.0f;
		constexpr olc::vf2d MenuTextScale = { 2.0f, 2.0f };
		constexpr float HotbarSlotSize = 46.0f;
		constexpr float HotbarSlotGap = 4.0f;
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
			const std::array<olc::Key, 10> slots = { olc::Key::K1, olc::Key::K2, olc::Key::K3, olc::Key::K4,
				olc::Key::K5, olc::Key::K6, olc::Key::K7, olc::Key::K8, olc::Key::K9, olc::Key::K0 };
			for (size_t i = 0; i < slots.size(); ++i)
				if (keyboard.GetKey(slots[i]).bPressed)
					hotbarSelection = static_cast<uint8_t>(i);
		}


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
		if (client.GetStatus() == Client::Status::Connected)
			UpdateDrag();
		else
			dragging = false;

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
		DrawHud();
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
		input.hotbar = hotbarSelection;

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

	void Game::DrawHud() {
		if (client.GetStatus() != Client::Status::Connected)
			return;

		const Inventory& inventory = client.GetLocalRagdoll().inventory;
		const float hotbarWidth = static_cast<float>(HotbarSlots) * HotbarSlotSize
			+ static_cast<float>(HotbarSlots - 1) * HotbarSlotGap;
		const olc::vf2d origin = HudOrigin();
		DrawHotbar(origin, inventory);
		DrawClothingSlots(origin + olc::vf2d(hotbarWidth + HotbarSlotGap * 3.0f, 0.0f), inventory);
		if (dragging && dragItem != Item::None)
			DrawSlot(mouse.GetPosition() - olc::vf2d(HotbarSlotSize * 0.5f, HotbarSlotSize * 0.5f),
				HotbarSlotSize, dragItem, true);
	}

	olc::vf2d Game::HudOrigin() {
	const float hotbarWidth = static_cast<float>(HotbarSlots) * HotbarSlotSize
		+ static_cast<float>(HotbarSlots - 1) * HotbarSlotGap;
	const float clothingCount = static_cast<float>(ClothingSlot::Count);
	const float clothingWidth = clothingCount * HotbarSlotSize
		+ (clothingCount - 1.0f) * HotbarSlotGap;
	const float spacing = HotbarSlotGap * 3.0f;
	const olc::vi2d screen = ScreenSize();
	return { std::floor((static_cast<float>(screen.x) - hotbarWidth - spacing - clothingWidth) * 0.5f),
		static_cast<float>(screen.y) - HotbarSlotSize - 12.0f };
}

	bool Game::SlotAt(const olc::vf2d& point, bool& clothing, size_t& index) {
		const olc::vf2d origin = HudOrigin();
		const float hotbarWidth = static_cast<float>(HotbarSlots) * HotbarSlotSize
			+ static_cast<float>(HotbarSlots - 1) * HotbarSlotGap;
		for (size_t i = 0; i < HotbarSlots; ++i) {
			const olc::vf2d slot = origin
				+ olc::vf2d(static_cast<float>(i) * (HotbarSlotSize + HotbarSlotGap), 0.0f);
			if (point.x >= slot.x && point.x < slot.x + HotbarSlotSize
				&& point.y >= slot.y && point.y < slot.y + HotbarSlotSize) {
				clothing = false;
				index = i;
				return true;
			}
		}
		const olc::vf2d clothingOrigin = origin + olc::vf2d(hotbarWidth + HotbarSlotGap * 3.0f, 0.0f);
		for (size_t i = 0; i < static_cast<size_t>(ClothingSlot::Count); ++i) {
			const olc::vf2d slot = clothingOrigin
				+ olc::vf2d(static_cast<float>(i) * (HotbarSlotSize + HotbarSlotGap), 0.0f);
			if (point.x >= slot.x && point.x < slot.x + HotbarSlotSize
				&& point.y >= slot.y && point.y < slot.y + HotbarSlotSize) {
				clothing = true;
				index = i;
				return true;
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
		if (item != Item::None) {
			draw.FilledRect(origin + olc::vf2d(4.0f, 4.0f), { size - 8.0f, size - 8.0f }, ItemColour(item));
		}
		if (highlighted)
			draw.Rect(origin - olc::vf2d(2.0f, 2.0f), { size + 4.0f, size + 4.0f }, olc::Pixel(215, 230, 245));
		draw.Rect(origin, { size, size }, highlighted ? olc::Pixel(160, 190, 215) : olc::Pixel(70, 85, 100));
	}

	void Game::DrawHotbar(const olc::vf2d& origin, const Inventory& inventory) {
		for (size_t i = 0; i < HotbarSlots; ++i) {
			const olc::vf2d slot = origin
				+ olc::vf2d(static_cast<float>(i) * (HotbarSlotSize + HotbarSlotGap), 0.0f);
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
			const olc::vf2d slot = origin
				+ olc::vf2d(static_cast<float>(i) * (HotbarSlotSize + HotbarSlotGap), 0.0f);
			const Item shown = dragging && dragFromClothing && dragIndex == i
				? Item::None : inventory.clothing[i];
			DrawSlot(slot, HotbarSlotSize, shown, false);
			draw.StringProp(slot + olc::vf2d(3.0f, -16.0f), ClothingName(static_cast<ClothingSlot>(i)),
				olc::Pixel(145, 165, 185));
		}
	}
}
