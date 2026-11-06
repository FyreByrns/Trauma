#pragma once
#include "third_party/imgui/imgui.h"
#include <string>

namespace olc {
	class PGEWindow;
}

namespace Trauma {
	namespace Ui {
		void SetDataDirectory(std::string directory);
		void BeginFrame(olc::PGEWindow& window, float elapsedTime);
		bool SaveStyle();
		void ResetStyle();
		void EndFrame(olc::PGEWindow& window);
		bool WantsMouse();
		bool WantsKeyboard();
	}
}
