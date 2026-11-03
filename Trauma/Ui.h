#pragma once
#include "third_party/imgui/imgui.h"

namespace olc {
	class PGEWindow;
}

namespace Trauma {
	namespace Ui {
		void BeginFrame(olc::PGEWindow& window, float elapsedTime);
		void EndFrame(olc::PGEWindow& window);
		bool WantsMouse();
		bool WantsKeyboard();
	}
}
