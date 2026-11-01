#include "Raycast.h"
#include <limits>

namespace Trauma
{
	Ray MakeRay(const Vec2& from, const Vec2& to)
	{
		return { from, Normalize(to - from) };
	}

	RayHit RaycastSegment(const Ray& ray, const Vec2& first, const Vec2& second)
	{
		RayHit result;
		const Vec2 direction = Normalize(ray.direction);
		const Vec2 edge = second - first;
		const float denominator = Cross(direction, edge);
		if (std::abs(denominator) < 0.00001f)
			return result;

		const Vec2 offset = ray.origin - first;
		const float distance = Cross(edge, offset) / denominator;
		const float along = -Cross(offset, direction) / denominator;
		if (distance < 0.0f || along < 0.0f || along > 1.0f)
			return result;

		result.hit = true;
		result.distance = distance;
		result.point = ray.origin + direction * distance;
		result.normal = Normalize(Perpendicular(edge));
		if (Dot(result.normal, direction) > 0.0f)
			result.normal = -result.normal;

		return result;
	}

	RayHit RaycastCircle(const Ray& ray, const Vec2& centre, float radius)
	{
		RayHit result;
		const Vec2 direction = Normalize(ray.direction);
		const Vec2 offset = ray.origin - centre;
		const float halfB = Dot(offset, direction);
		const float c = LengthSquared(offset) - radius * radius;
		const float discriminant = halfB * halfB - c;
		if (discriminant < 0.0f)
			return result;

		const float root = std::sqrt(discriminant);
		float distance = -halfB - root;
		if (distance < 0.0f)
			distance = -halfB + root;
		if (distance < 0.0f)
			return result;

		result.hit = true;
		result.distance = distance;
		result.point = ray.origin + direction * distance;
		result.normal = Normalize(result.point - centre);
		return result;
	}

	RayHit RaycastAabb(const Ray& ray, const Vec2& centre, const Vec2& halfExtents)
	{
		RayHit result;
		const Vec2 direction = Normalize(ray.direction);
		const Vec2 minimum = centre - halfExtents;
		const Vec2 maximum = centre + halfExtents;

		float nearDistance = -std::numeric_limits<float>::infinity();
		float farDistance = std::numeric_limits<float>::infinity();
		Vec2 nearNormal;

		for (int axis = 0; axis < 2; ++axis)
		{
			const float origin = axis == 0 ? ray.origin.x : ray.origin.y;
			const float slope = axis == 0 ? direction.x : direction.y;
			const float low = axis == 0 ? minimum.x : minimum.y;
			const float high = axis == 0 ? maximum.x : maximum.y;

			if (std::abs(slope) < 0.00001f)
			{
				if (origin < low || origin > high)
					return result;
				continue;
			}

			float first = (low - origin) / slope;
			float second = (high - origin) / slope;
			Vec2 normal = axis == 0 ? Vec2{ -1.0f, 0.0f } : Vec2{ 0.0f, -1.0f };
			if (first > second)
			{
				std::swap(first, second);
				normal = -normal;
			}

			if (first > nearDistance)
			{
				nearDistance = first;
				nearNormal = normal;
			}

			farDistance = std::min(farDistance, second);
			if (nearDistance > farDistance)
				return result;
		}

		if (farDistance < 0.0f)
			return result;

		if (nearDistance < 0.0f)
		{
			nearDistance = farDistance;
			nearNormal = -nearNormal;
		}

		result.hit = true;
		result.distance = nearDistance;
		result.point = ray.origin + direction * nearDistance;
		result.normal = nearNormal;
		return result;
	}

	Vec2 ClosestPointOnSegment(const Vec2& point, const Vec2& first, const Vec2& second)
	{
		const Vec2 edge = second - first;
		const float lengthSquared = LengthSquared(edge);
		if (lengthSquared <= 0.00001f)
			return first;

		const float amount = std::clamp(Dot(point - first, edge) / lengthSquared, 0.0f, 1.0f);
		return first + edge * amount;
	}

	float DistanceToSegment(const Vec2& point, const Vec2& first, const Vec2& second)
	{
		return Distance(point, ClosestPointOnSegment(point, first, second));
	}
}
