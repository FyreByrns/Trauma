#include "IK.h"
#include <algorithm>

namespace Trauma
{
	TwoBoneIkResult SolveTwoBoneIk(
		const olc::vf2d& root,
		const olc::vf2d& target,
		float upperLength,
		float lowerLength,
		float bendSign)
	{
		TwoBoneIkResult result;
		result.joint = root;
		result.end = root;

		const float upper = std::max(0.0001f, upperLength);
		const float lower = std::max(0.0001f, lowerLength);
		const olc::vf2d offset = target - root;
		const float desired = offset.mag();
		const float minimumReach = std::abs(upper - lower) + 0.0001f;
		const float maximumReach = upper + lower - 0.0001f;
		const float reach = std::clamp(desired, minimumReach, maximumReach);
		const olc::vf2d direction = desired > 0.0001f ? offset / desired : olc::vf2d{ 1.0f, 0.0f };
		const float cosine = std::clamp(
			(upper * upper + reach * reach - lower * lower) / (2.0f * upper * reach),
			-1.0f,
			1.0f);
		const float sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
		const olc::vf2d bend = direction.perp() * bendSign;

		result.joint = root + direction * (cosine * upper) + bend * (sine * upper);
		result.end = root + direction * reach;
		result.reached = std::abs(desired - reach) < 0.01f;
		return result;
	}

	void SolveFabrik(
		std::vector<olc::vf2d>& points,
		const olc::vf2d& target,
		int iterations,
		float tolerance,
		bool pinRoot)
	{
		if (points.size() < 2)
			return;

		std::vector<float> lengths(points.size() - 1);
		float totalLength = 0.0f;
		for (size_t index = 0; index + 1 < points.size(); ++index)
		{
			lengths[index] = (points[index] - points[index + 1]).mag();
			totalLength += lengths[index];
		}

		if (totalLength <= 0.0001f)
			return;

		const olc::vf2d root = points.front();
		if ((root - target).mag() >= totalLength)
		{
			const olc::vf2d direction = (target - root).norm();
			for (size_t index = 1; index < points.size(); ++index)
				points[index] = points[index - 1] + direction * lengths[index - 1];
			return;
		}

		const int count = std::max(1, iterations);
		for (int iteration = 0; iteration < count; ++iteration)
		{
			points.back() = target;

			for (size_t index = points.size() - 1; index > 0; --index)
			{
				const olc::vf2d direction = (points[index - 1] - points[index]).norm();
				points[index - 1] = points[index] + direction * lengths[index - 1];
			}

			if (pinRoot)
				points.front() = root;

			for (size_t index = 1; index < points.size(); ++index)
			{
				const olc::vf2d direction = (points[index] - points[index - 1]).norm();
				points[index] = points[index - 1] + direction * lengths[index - 1];
			}

			if ((points.back() - target).mag() <= tolerance)
				break;
		}
	}
}
