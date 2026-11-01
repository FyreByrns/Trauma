#pragma once
#include <vector>
#include "olcPixelGameEngine3.h"

namespace Trauma {
	struct TwoBoneIkResult {
		olc::vf2d joint;
		olc::vf2d end;
		bool reached = false;
	};

	TwoBoneIkResult SolveTwoBoneIk(
		const olc::vf2d& root,
		const olc::vf2d& target,
		float upperLength,
		float lowerLength,
		float bendSign = 1.0f);

	void SolveFabrik(
		std::vector<olc::vf2d>& points,
		const olc::vf2d& target,
		int iterations = 12,
		float tolerance = 0.5f,
		bool pinRoot = true);
}
