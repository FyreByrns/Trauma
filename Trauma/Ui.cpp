#include "Ui.h"
#include "olcPixelGameEngine3.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Trauma {
	namespace Ui {
		namespace {
			struct KeyBinding {
				ImGuiKey imgui;
				olc::Key engine;
			};

			const KeyBinding KeyBindings[] = {
				{ ImGuiKey_Tab, olc::Key::TAB },
				{ ImGuiKey_LeftArrow, olc::Key::LEFT },
				{ ImGuiKey_RightArrow, olc::Key::RIGHT },
				{ ImGuiKey_UpArrow, olc::Key::UP },
				{ ImGuiKey_DownArrow, olc::Key::DOWN },
				{ ImGuiKey_PageUp, olc::Key::PGUP },
				{ ImGuiKey_PageDown, olc::Key::PGDN },
				{ ImGuiKey_Home, olc::Key::HOME },
				{ ImGuiKey_End, olc::Key::END },
				{ ImGuiKey_Insert, olc::Key::INS },
				{ ImGuiKey_Delete, olc::Key::DEL },
				{ ImGuiKey_Backspace, olc::Key::BACK },
				{ ImGuiKey_Space, olc::Key::SPACE },
				{ ImGuiKey_Enter, olc::Key::RETURN },
				{ ImGuiKey_Escape, olc::Key::ESCAPE },
				{ ImGuiKey_LeftShift, olc::Key::SHIFT },
				{ ImGuiKey_LeftCtrl, olc::Key::CTRL },
				{ ImGuiKey_LeftAlt, olc::Key::ALT },
				{ ImGuiKey_0, olc::Key::K0 },
				{ ImGuiKey_1, olc::Key::K1 },
				{ ImGuiKey_2, olc::Key::K2 },
				{ ImGuiKey_3, olc::Key::K3 },
				{ ImGuiKey_4, olc::Key::K4 },
				{ ImGuiKey_5, olc::Key::K5 },
				{ ImGuiKey_6, olc::Key::K6 },
				{ ImGuiKey_7, olc::Key::K7 },
				{ ImGuiKey_8, olc::Key::K8 },
				{ ImGuiKey_9, olc::Key::K9 },
				{ ImGuiKey_A, olc::Key::A },
				{ ImGuiKey_B, olc::Key::B },
				{ ImGuiKey_C, olc::Key::C },
				{ ImGuiKey_D, olc::Key::D },
				{ ImGuiKey_E, olc::Key::E },
				{ ImGuiKey_F, olc::Key::F },
				{ ImGuiKey_G, olc::Key::G },
				{ ImGuiKey_H, olc::Key::H },
				{ ImGuiKey_I, olc::Key::I },
				{ ImGuiKey_J, olc::Key::J },
				{ ImGuiKey_K, olc::Key::K },
				{ ImGuiKey_L, olc::Key::L },
				{ ImGuiKey_M, olc::Key::M },
				{ ImGuiKey_N, olc::Key::N },
				{ ImGuiKey_O, olc::Key::O },
				{ ImGuiKey_P, olc::Key::P },
				{ ImGuiKey_Q, olc::Key::Q },
				{ ImGuiKey_R, olc::Key::R },
				{ ImGuiKey_S, olc::Key::S },
				{ ImGuiKey_T, olc::Key::T },
				{ ImGuiKey_U, olc::Key::U },
				{ ImGuiKey_V, olc::Key::V },
				{ ImGuiKey_W, olc::Key::W },
				{ ImGuiKey_X, olc::Key::X },
				{ ImGuiKey_Y, olc::Key::Y },
				{ ImGuiKey_Z, olc::Key::Z },
				{ ImGuiKey_F1, olc::Key::F1 },
				{ ImGuiKey_F2, olc::Key::F2 },
				{ ImGuiKey_F3, olc::Key::F3 },
				{ ImGuiKey_F4, olc::Key::F4 },
				{ ImGuiKey_F5, olc::Key::F5 },
				{ ImGuiKey_F6, olc::Key::F6 },
				{ ImGuiKey_F7, olc::Key::F7 },
				{ ImGuiKey_F8, olc::Key::F8 },
				{ ImGuiKey_F9, olc::Key::F9 },
				{ ImGuiKey_F10, olc::Key::F10 },
				{ ImGuiKey_F11, olc::Key::F11 },
				{ ImGuiKey_F12, olc::Key::F12 },
			};

			ImGuiContext* context = nullptr;
			std::unique_ptr<olc::Image> fontAtlas;
			bool ready = false;

			struct UiVertex {
				ImVec2 position;
				ImVec2 uv;
				olc::Pixel colour;
			};

			olc::Pixel FromImGuiColour(ImU32 colour) {
				return olc::Pixel(
					static_cast<uint8_t>(colour & 0xFF),
					static_cast<uint8_t>((colour >> 8) & 0xFF),
					static_cast<uint8_t>((colour >> 16) & 0xFF),
					static_cast<uint8_t>((colour >> 24) & 0xFF));
			}

			float PlaneDistance(const UiVertex& vertex, int plane, float limit) {
				switch (plane) {
				case 0: return vertex.position.x - limit;
				case 1: return limit - vertex.position.x;
				case 2: return vertex.position.y - limit;
				default: return limit - vertex.position.y;
				}
			}

			UiVertex Interpolate(const UiVertex& a, const UiVertex& b, float t) {
				UiVertex result;
				result.position = ImVec2(
					a.position.x + (b.position.x - a.position.x) * t,
					a.position.y + (b.position.y - a.position.y) * t);
				result.uv = ImVec2(
					a.uv.x + (b.uv.x - a.uv.x) * t,
					a.uv.y + (b.uv.y - a.uv.y) * t);
				result.colour = olc::Pixel(
					static_cast<uint8_t>(a.colour.r + (b.colour.r - a.colour.r) * t),
					static_cast<uint8_t>(a.colour.g + (b.colour.g - a.colour.g) * t),
					static_cast<uint8_t>(a.colour.b + (b.colour.b - a.colour.b) * t),
					static_cast<uint8_t>(a.colour.a + (b.colour.a - a.colour.a) * t));
				return result;
			}

			void ClipEdge(std::vector<UiVertex>& polygon, std::vector<UiVertex>& scratch, int plane, float limit) {
				scratch.clear();
				if (polygon.empty())
					return;

				for (size_t i = 0; i < polygon.size(); ++i) {
					const UiVertex& current = polygon[i];
					const UiVertex& next = polygon[(i + 1) % polygon.size()];
					const float currentDistance = PlaneDistance(current, plane, limit);
					const float nextDistance = PlaneDistance(next, plane, limit);

					if (currentDistance >= 0.0f)
						scratch.push_back(current);
					if ((currentDistance >= 0.0f) != (nextDistance >= 0.0f))
						scratch.push_back(Interpolate(current, next, currentDistance / (currentDistance - nextDistance)));
				}

				polygon.swap(scratch);
			}

			bool Initialise(olc::PGEWindow& window) {
				if (ready)
					return true;

				IMGUI_CHECKVERSION();
				context = ImGui::CreateContext();
				ImGuiIO& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.LogFilename = nullptr;
				io.BackendPlatformName = "trauma_pge";
				io.BackendRendererName = "trauma_pge_draw";
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

				ImFontConfig fontConfig;
				fontConfig.SizePixels = 16.0f;
				fontConfig.OversampleH = 1;
				fontConfig.OversampleV = 1;
				fontConfig.PixelSnapH = true;
				io.Fonts->AddFontDefault(&fontConfig);

				unsigned char* pixels = nullptr;
				int width = 0;
				int height = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
				if (pixels == nullptr || width <= 0 || height <= 0)
					return false;

				fontAtlas = std::make_unique<olc::Image>();
				if (!window.CreateImage(*fontAtlas, { width, height }))
					return false;

				std::vector<olc::Pixel>& target = fontAtlas->GetPixels();
				target.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
				for (int y = 0; y < height; ++y) {
					for (int x = 0; x < width; ++x) {
						const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
						const unsigned char* source = pixels + index * 4u;
						target[index] = olc::Pixel(source[0], source[1], source[2], source[3]);
					}
				}

				fontAtlas->BindCPU();

				io.Fonts->SetTexID(static_cast<ImTextureID>(1));
				ready = true;
				return true;
			}
		}

		void BeginFrame(olc::PGEWindow& window, float elapsedTime) {
			if (!ready && !Initialise(window))
				return;

			ImGuiIO& io = ImGui::GetIO();
			const olc::vi2d screen = window.ScreenSize();
			io.DisplaySize = ImVec2(static_cast<float>(screen.x), static_cast<float>(screen.y));
			io.DeltaTime = elapsedTime > 0.0f ? elapsedTime : 1.0f / 60.0f;

			auto& mouse = window.GetMouse();
			const olc::vf2d pointer = mouse.GetPosition();
			io.AddMousePosEvent(pointer.x, pointer.y);
			for (int button = 0; button < 3; ++button)
				io.AddMouseButtonEvent(button, mouse.GetButton(button).bHeld);
			io.AddMouseWheelEvent(0.0f, static_cast<float>(mouse.GetWheel()));

			auto& keyboard = window.GetKeyboard();
			for (const KeyBinding& binding : KeyBindings)
				io.AddKeyEvent(binding.imgui, keyboard.GetKey(binding.engine).bHeld);

			const bool shift = keyboard.GetKey(olc::Key::SHIFT).bHeld;
			const bool ctrl = keyboard.GetKey(olc::Key::CTRL).bHeld;
			const bool alt = keyboard.GetKey(olc::Key::ALT).bHeld;
			io.AddKeyEvent(ImGuiMod_Shift, shift);
			io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
			io.AddKeyEvent(ImGuiMod_Alt, alt);

			for (olc::Key key : keyboard.GetKeyCache()) {
				const std::string glyph = keyboard.GetKeyGlyph(key, shift, ctrl, alt);
				if (glyph.size() == 1 && static_cast<unsigned char>(glyph[0]) >= 32)
					io.AddInputCharactersUTF8(glyph.c_str());
			}

			ImGui::NewFrame();
		}

		void EndFrame(olc::PGEWindow& window) {
			if (!ready)
				return;

			ImGui::Render();

			const ImDrawData* drawData = ImGui::GetDrawData();
			if (drawData == nullptr)
				return;

			const olc::vi2d screen = window.ScreenSize();
			if (screen.x <= 0 || screen.y <= 0)
				return;

			const olc::vf2d atlasSize = {
				static_cast<float>(fontAtlas->Size().x),
				static_cast<float>(fontAtlas->Size().y)
			};
			if (atlasSize.x <= 0.0f || atlasSize.y <= 0.0f)
				return;

			olc::Draw& draw = window.GetDraw();
			draw.WorldReset();
			draw.SetViewport({ 0, 0 }, screen);
			draw.SetCullMode(olc::CullMode::None);
			draw.SetBlendMode(olc::BlendMode::Alpha);

			std::vector<olc::vf2d> points;
			std::vector<olc::Pixel> colours;
			std::vector<olc::vf2d> texCoords;
			points.reserve(6144);
			colours.reserve(6144);
			texCoords.reserve(6144);

			std::vector<UiVertex> polygon;
			std::vector<UiVertex> scratch;
			polygon.reserve(8);
			scratch.reserve(8);

			auto emit = [&](const UiVertex& vertex) {
				points.push_back({ vertex.position.x, vertex.position.y });
				colours.push_back(vertex.colour);
				texCoords.push_back({ vertex.uv.x, vertex.uv.y });
			};

			for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
				const ImDrawList* list = drawData->CmdLists[listIndex];
				for (const ImDrawCmd& command : list->CmdBuffer) {
					if (command.UserCallback != nullptr || command.ElemCount == 0)
						continue;

					const float left = std::max(command.ClipRect.x, 0.0f);
					const float top = std::max(command.ClipRect.y, 0.0f);
					const float right = std::min(command.ClipRect.z, static_cast<float>(screen.x));
					const float bottom = std::min(command.ClipRect.w, static_cast<float>(screen.y));
					if (right <= left || bottom <= top)
						continue;

					const unsigned int end = command.IdxOffset + command.ElemCount;
					for (unsigned int index = command.IdxOffset; index + 2 < end; index += 3) {
						polygon.clear();
						for (unsigned int corner = 0; corner < 3; ++corner) {
							const ImDrawVert& vertex =
								list->VtxBuffer.Data[command.VtxOffset + list->IdxBuffer.Data[index + corner]];
							polygon.push_back({ vertex.pos, vertex.uv, FromImGuiColour(vertex.col) });
						}

						ClipEdge(polygon, scratch, 0, left);
						ClipEdge(polygon, scratch, 1, right);
						ClipEdge(polygon, scratch, 2, top);
						ClipEdge(polygon, scratch, 3, bottom);

						for (size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
							emit(polygon[0]);
							emit(polygon[corner]);
							emit(polygon[corner + 1]);
						}
					}
				}
			}

			if (points.empty())
				return;

			olc::TextureBatch batch = draw.CreateTextureBatch(*fontAtlas);
			draw.TexturedPolygon(batch, olc::Structure::List, points, colours, texCoords);
			draw.Batch(batch);
			draw.ProcessGPUTasks();
		}

		bool WantsMouse() {
			return ready && ImGui::GetIO().WantCaptureMouse;
		}

		bool WantsKeyboard() {
			return ready && ImGui::GetIO().WantCaptureKeyboard;
		}
	}
}
