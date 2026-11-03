#pragma once
#include "olcPixelGameEngine3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Trauma {
	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float Gravity = 1600.0f;
	constexpr float PlayerRadius = 10.0f;
	constexpr float TwoPi = 6.2831853f;

	constexpr float ContactFriction = 3.0f;
	constexpr float RestitutionThreshold = 70.0f;
	constexpr float Restitution = 0.15f;
	constexpr float ContactSlop = 0.05f;
	constexpr float ContactCorrection = 0.5f;
	constexpr float ContactMargin = ContactSlop;
	constexpr float AngularDamping = 1.5f;
	constexpr float GroundedSpinDamping = 8.0f;
	constexpr float AxisSnap = 0.03f;
	constexpr float MaxAngularSpeed = 25.0f;
	constexpr float RestSpeed = 4.0f;
	constexpr float RestDamping = 0.5f;
	constexpr float SleepSpeed = 3.0f;
	constexpr float SleepDelay = 0.6f;
	constexpr float WakeSpeed = 6.0f;

	enum class Shape : uint8_t {
		Circle = 0,
		Polygon = 1
	};

	constexpr int MaxPolygonVertices = 8;

	struct Polygon {
		std::array<olc::vf2d, MaxPolygonVertices> vertices{};
		int count = 0;
	};

	inline olc::vf2d Rotate(const olc::vf2d& v, float angle) {
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		return { v.x * c - v.y * s, v.x * s + v.y * c };
	}

	inline float PolygonSignedArea(const olc::vf2d* vertices, int count) {
		float area = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = vertices[i];
			const olc::vf2d& b = vertices[(i + 1) % count];
			area += a.x * b.y - b.x * a.y;
		}
		return area * 0.5f;
	}

	inline float PolygonInverseInertia(float inverseMass, const olc::vf2d* vertices, int count) {
		if (inverseMass <= 0.0f || count < 3)
			return 0.0f;
		float numerator = 0.0f;
		float denominator = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = vertices[i];
			const olc::vf2d& b = vertices[(i + 1) % count];
			const float cross = a.x * b.y - b.x * a.y;
			numerator += cross * (a.dot(a) + a.dot(b) + b.dot(b));
			denominator += cross;
		}
		if (std::abs(numerator) < 0.0001f || std::abs(denominator) < 0.0001f)
			return 0.0f;
		return 6.0f * inverseMass * denominator / numerator;
	}

	inline float CircleInverseInertia(float inverseMass, float radius) {
		if (inverseMass <= 0.0f || radius <= 0.0f)
			return 0.0f;
		return 2.0f * inverseMass / (radius * radius);
	}

	struct ObjectState {
		olc::vf2d position;
		olc::vf2d velocity;
		float radius = PlayerRadius;
		float inverseMass = 1.0f;
		bool grounded = false;
		bool supported = false;
		uint32_t lastInput = 0;
		Shape shape = Shape::Circle;
		olc::vf2d halfExtents = { 0.0f, 0.0f };
		uint8_t vertexCount = 0;
		std::array<olc::vf2d, MaxPolygonVertices> localVertices{};
		float angle = 0.0f;
		float angularVelocity = 0.0f;
		float inverseInertia = 0.0f;
		float restTime = 0.0f;
		bool sleeping = false;
	};

	inline ObjectState CreatePolygon(const olc::vf2d& position, std::vector<olc::vf2d> outline,
		float inverseMass, float angle = 0.0f) {
		ObjectState body;
		body.shape = Shape::Polygon;
		body.position = position;
		body.inverseMass = inverseMass;
		body.angle = angle;

		const int count = static_cast<int>(std::min<size_t>(outline.size(), MaxPolygonVertices));
		if (count < 3) {
			body.shape = Shape::Circle;
			return body;
		}
		outline.resize(static_cast<size_t>(count));
		if (PolygonSignedArea(outline.data(), count) < 0.0f)
			std::reverse(outline.begin(), outline.end());

		olc::vf2d centroid{ 0.0f, 0.0f };
		float area = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d& a = outline[i];
			const olc::vf2d& b = outline[(i + 1) % count];
			const float cross = a.x * b.y - b.x * a.y;
			area += cross;
			centroid += (a + b) * cross;
		}
		if (std::abs(area) > 0.0001f)
			centroid /= 3.0f * area;

		body.vertexCount = static_cast<uint8_t>(count);
		float extentX = 0.0f, extentY = 0.0f, radius = 0.0f;
		for (int i = 0; i < count; ++i) {
			const olc::vf2d vertex = outline[i] - centroid;
			body.localVertices[i] = vertex;
			extentX = std::max(extentX, std::abs(vertex.x));
			extentY = std::max(extentY, std::abs(vertex.y));
			radius = std::max(radius, vertex.mag());
		}
		body.halfExtents = { extentX, extentY };
		body.radius = radius;
		body.inverseInertia = PolygonInverseInertia(inverseMass, body.localVertices.data(), count);
		return body;
	}

	inline ObjectState CreateBox(const olc::vf2d& position, const olc::vf2d& halfExtents,
		float inverseMass, float angle = 0.0f) {
		return CreatePolygon(position, {
			{ -halfExtents.x, -halfExtents.y }, { halfExtents.x, -halfExtents.y },
			{ halfExtents.x, halfExtents.y }, { -halfExtents.x, halfExtents.y } }, inverseMass, angle);
	}

	inline ObjectState CreateCircle(const olc::vf2d& position, float radius, float inverseMass) {
		ObjectState body;
		body.shape = Shape::Circle;
		body.position = position;
		body.radius = radius;
		body.inverseMass = inverseMass;
		body.inverseInertia = CircleInverseInertia(inverseMass, radius);
		return body;
	}

	struct Contact {
		olc::vf2d point;
		olc::vf2d normal;
		float depth = 0.0f;
	};

	constexpr int MaxContacts = 4;

	inline Polygon WorldVertices(const ObjectState& body) {
		Polygon result;
		if (body.shape != Shape::Polygon)
			return result;
		result.count = body.vertexCount;
		for (int i = 0; i < result.count; ++i)
			result.vertices[i] = body.position + Rotate(body.localVertices[i], body.angle);
		return result;
	}

	inline void ProjectVertices(const Polygon& polygon, const olc::vf2d& axis,
		float& minimum, float& maximum) {
		minimum = maximum = polygon.vertices[0].dot(axis);
		for (int i = 1; i < polygon.count; ++i) {
			const float value = polygon.vertices[i].dot(axis);
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}
	}

	inline bool ContainsPoint(const Polygon& polygon, const olc::vf2d& point, float margin = 0.0f) {
		for (int i = 0; i < polygon.count; ++i) {
			const olc::vf2d& a = polygon.vertices[i];
			const olc::vf2d& b = polygon.vertices[(i + 1) % polygon.count];
			const olc::vf2d edge = b - a;
			const float length = edge.mag();
			if (length < 0.0001f)
				continue;
			const float distance = (edge.x * (point.y - a.y) - edge.y * (point.x - a.x)) / length;
			if (distance < -margin)
				return false;
		}
		return true;
	}


	inline olc::vf2d SnapContactNormal(const olc::vf2d& normal) {
		if (std::abs(normal.x) < AxisSnap)
			return { 0.0f, normal.y > 0.0f ? 1.0f : -1.0f };
		if (std::abs(normal.y) < AxisSnap)
			return { normal.x > 0.0f ? 1.0f : -1.0f, 0.0f };
		return normal;
	}

	inline void Wake(ObjectState& body) {
		body.sleeping = false;
		body.restTime = 0.0f;
	}

	inline void UpdateSleep(ObjectState& body, float step) {
		if (body.inverseMass <= 0.0f)
			return;
		const bool resting = body.grounded && body.velocity.mag() < SleepSpeed
			&& std::abs(body.angularVelocity) < SleepSpeed;
		body.restTime = resting ? body.restTime + step : 0.0f;
		if (body.restTime < SleepDelay)
			return;
		body.sleeping = true;
		body.velocity = { 0.0f, 0.0f };
		body.angularVelocity = 0.0f;
	}

	inline bool PolygonPenetration(const ObjectState& first, const ObjectState& second,
		olc::vf2d& normal, float& depth) {
		const Polygon firstVertices = WorldVertices(first);
		const Polygon secondVertices = WorldVertices(second);
		if (firstVertices.count < 3 || secondVertices.count < 3)
			return false;

		float shallowest = std::numeric_limits<float>::max();
		olc::vf2d axis = { 1.0f, 0.0f };
		bool separated = false;

		const auto testOutline = [&](const Polygon& polygon, float bias) {
			for (int i = 0; i < polygon.count; ++i) {
				const olc::vf2d edge = polygon.vertices[(i + 1) % polygon.count] - polygon.vertices[i];
				const float length = edge.mag();
				if (length < 0.0001f)
					continue;
				const olc::vf2d candidate = edge.perp() / length;
				float firstMinimum = 0.0f, firstMaximum = 0.0f, secondMinimum = 0.0f, secondMaximum = 0.0f;
				ProjectVertices(firstVertices, candidate, firstMinimum, firstMaximum);
				ProjectVertices(secondVertices, candidate, secondMinimum, secondMaximum);
				if (firstMaximum < secondMinimum - ContactMargin
					|| secondMaximum < firstMinimum - ContactMargin) {
					separated = true;
					return;
				}
				const float overlap = std::min(firstMaximum, secondMaximum) - std::max(firstMinimum, secondMinimum);
				if (overlap < shallowest - bias) {
					shallowest = overlap;
					axis = candidate;
				}
			}
		};
		testOutline(firstVertices, 0.5f);
		if (!separated)
			testOutline(secondVertices, 0.0f);
		if (separated)
			return false;

		if ((second.position - first.position).dot(axis) < 0.0f)
			axis = -axis;
		axis = SnapContactNormal(axis);
		normal = axis;
		depth = shallowest;
		return true;
	}

	inline bool CirclePolygonContact(const ObjectState& circle, const ObjectState& polygon, Contact& contact) {
		const Polygon outline = WorldVertices(polygon);
		if (outline.count < 3)
			return false;

		float closestDistance = std::numeric_limits<float>::max();
		olc::vf2d closestPoint = polygon.position;
		olc::vf2d closestOutward = { 0.0f, -1.0f };
		for (int i = 0; i < outline.count; ++i) {
			const olc::vf2d& a = outline.vertices[i];
			const olc::vf2d& b = outline.vertices[(i + 1) % outline.count];
			const olc::vf2d edge = b - a;
			const float squared = edge.dot(edge);
			if (squared < 0.0001f)
				continue;
			const float t = std::clamp((circle.position - a).dot(edge) / squared, 0.0f, 1.0f);
			const olc::vf2d point = a + edge * t;
			const float distance = (circle.position - point).mag();
			if (distance < closestDistance) {
				closestDistance = distance;
				closestPoint = point;
				closestOutward = edge.perp() / std::sqrt(squared);
			}
		}

		if (!ContainsPoint(outline, circle.position)) {
			if (closestDistance >= circle.radius + ContactMargin || closestDistance < 0.0001f)
				return false;
			contact.normal = (closestPoint - circle.position) / closestDistance;
			contact.depth = circle.radius - closestDistance;
		}
		else {
			contact.normal = closestOutward;
			contact.depth = circle.radius + closestDistance;
		}
		contact.point = closestPoint;
		return true;
	}

	inline bool CircleCircleContact(const ObjectState& first, const ObjectState& second, Contact& contact) {
		const olc::vf2d offset = second.position - first.position;
		const float distance = offset.mag();
		if (distance >= first.radius + second.radius + ContactMargin)
			return false;
		contact.normal = distance > 0.0001f ? offset / distance : olc::vf2d{ 1.0f, 0.0f };
		contact.depth = first.radius + second.radius - distance;
		contact.point = first.position + contact.normal * first.radius;
		return true;
	}

	inline int PolygonContacts(const ObjectState& first, const ObjectState& second,
		const olc::vf2d& normal, float depth, Contact* contacts) {
		const Polygon firstOutline = WorldVertices(first);
		const Polygon secondOutline = WorldVertices(second);
		int count = 0;
		for (int i = 0; i < secondOutline.count && count < MaxContacts; ++i)
			if (ContainsPoint(firstOutline, secondOutline.vertices[i], 0.05f))
				contacts[count++] = { secondOutline.vertices[i], normal, depth };
		for (int i = 0; i < firstOutline.count && count < MaxContacts; ++i)
			if (ContainsPoint(secondOutline, firstOutline.vertices[i], 0.05f))
				contacts[count++] = { firstOutline.vertices[i], normal, depth };
		if (count > 0)
			return count;

		olc::vf2d firstSupport = first.position;
		float firstDeepest = -std::numeric_limits<float>::max();
		for (int i = 0; i < firstOutline.count; ++i) {
			const float projection = firstOutline.vertices[i].dot(normal);
			if (projection > firstDeepest) {
				firstDeepest = projection;
				firstSupport = firstOutline.vertices[i];
			}
		}
		olc::vf2d secondSupport = second.position;
		float secondDeepest = std::numeric_limits<float>::max();
		for (int i = 0; i < secondOutline.count; ++i) {
			const float projection = secondOutline.vertices[i].dot(normal);
			if (projection < secondDeepest) {
				secondDeepest = projection;
				secondSupport = secondOutline.vertices[i];
			}
		}
		contacts[0] = { (firstSupport + secondSupport) * 0.5f, normal, depth };
		return 1;
	}


	inline void ApplyContact(ObjectState& first, ObjectState& second, const Contact& contact) {
		const olc::vf2d armFirst = contact.point - first.position;
		const olc::vf2d armSecond = contact.point - second.position;

		const olc::vf2d firstVelocity = first.velocity + armFirst.perp() * first.angularVelocity;
		const olc::vf2d secondVelocity = second.velocity + armSecond.perp() * second.angularVelocity;
		const float normalSpeed = (secondVelocity - firstVelocity).dot(contact.normal);
		if (normalSpeed > 0.0f)
			return;

		const float firstLever = armFirst.cross(contact.normal);
		const float secondLever = armSecond.cross(contact.normal);
		const float normalMass = first.inverseMass + second.inverseMass
			+ first.inverseInertia * firstLever * firstLever + second.inverseInertia * secondLever * secondLever;
		if (normalMass <= 0.0f)
			return;

		const float restitution = normalSpeed < -RestitutionThreshold ? Restitution : 0.0f;
		const float impulse = -(1.0f + restitution) * normalSpeed / normalMass;
		const olc::vf2d push = contact.normal * impulse;
		first.velocity -= push * first.inverseMass;
		second.velocity += push * second.inverseMass;
		first.angularVelocity -= first.inverseInertia * armFirst.cross(push);
		second.angularVelocity += second.inverseInertia * armSecond.cross(push);

		const olc::vf2d tangent = contact.normal.perp();
		const olc::vf2d firstTangent = first.velocity + armFirst.perp() * first.angularVelocity;
		const olc::vf2d secondTangent = second.velocity + armSecond.perp() * second.angularVelocity;
		const float tangentSpeed = (secondTangent - firstTangent).dot(tangent);
		const float firstFrictionLever = armFirst.cross(tangent);
		const float secondFrictionLever = armSecond.cross(tangent);
		const float tangentMass = first.inverseMass + second.inverseMass
			+ first.inverseInertia * firstFrictionLever * firstFrictionLever
			+ second.inverseInertia * secondFrictionLever * secondFrictionLever;
		if (tangentMass <= 0.0f)
			return;

		const float limit = ContactFriction * impulse;
		const float frictionImpulse = std::clamp(-tangentSpeed / tangentMass, -limit, limit);
		const olc::vf2d scrape = tangent * frictionImpulse;
		first.velocity -= scrape * first.inverseMass;
		second.velocity += scrape * second.inverseMass;
		first.angularVelocity -= first.inverseInertia * armFirst.cross(scrape);
		second.angularVelocity += second.inverseInertia * armSecond.cross(scrape);
	}

	inline void CorrectContact(ObjectState& first, ObjectState& second, const Contact& contact) {
		const float inverseMass = first.inverseMass + second.inverseMass;
		if (inverseMass <= 0.0f)
			return;
		const float correction = std::max(contact.depth - ContactSlop, 0.0f) * ContactCorrection / inverseMass;
		const olc::vf2d normal = SnapContactNormal(contact.normal);
		first.position -= normal * (correction * first.inverseMass);
		second.position += normal * (correction * second.inverseMass);
	}


	inline void ResolveShapePair(ObjectState& first, ObjectState& second) {
		Contact contacts[MaxContacts];
		int count = 0;

		if (first.shape == Shape::Circle && second.shape == Shape::Circle) {
			if (!CircleCircleContact(first, second, contacts[0]))
				return;
			count = 1;
		}
		else if (first.shape == Shape::Polygon && second.shape == Shape::Polygon) {
			olc::vf2d normal;
			float depth = 0.0f;
			if (!PolygonPenetration(first, second, normal, depth))
				return;
			count = PolygonContacts(first, second, normal, depth, contacts);
		}
		else if (first.shape == Shape::Circle) {
			if (!CirclePolygonContact(first, second, contacts[0]))
				return;
			count = 1;
		}
		else {
			if (!CirclePolygonContact(second, first, contacts[0]))
				return;
			contacts[0].normal = -contacts[0].normal;
			count = 1;
		}
		if (count <= 0)
			return;

		if (first.sleeping && !second.sleeping && second.velocity.mag() > WakeSpeed)
			Wake(first);
		if (second.sleeping && !first.sleeping && first.velocity.mag() > WakeSpeed)
			Wake(second);

		for (int pass = 0; pass < 2; ++pass)
			for (int index = 0; index < count; ++index)
				ApplyContact(first, second, contacts[index]);

		int deepest = 0;
		for (int index = 1; index < count; ++index)
			if (contacts[index].depth > contacts[deepest].depth)
				deepest = index;
		CorrectContact(first, second, contacts[deepest]);

		const olc::vf2d normal = contacts[deepest].normal;
		if (normal.y > 0.5f && (second.inverseMass <= 0.0f || second.supported))
			first.supported = true;
		if (normal.y < -0.5f && (first.inverseMass <= 0.0f || first.supported))
			second.supported = true;
	}


	inline void IntegrateRotation(ObjectState& body, float step) {
		if (body.inverseInertia <= 0.0f)
			return;
		const float damping = body.grounded ? AngularDamping + GroundedSpinDamping : AngularDamping;
		body.angularVelocity -= body.angularVelocity * std::min(1.0f, damping * step);
		body.angularVelocity = std::clamp(body.angularVelocity, -MaxAngularSpeed, MaxAngularSpeed);
		body.angle = std::remainder(body.angle + body.angularVelocity * step, TwoPi);
	}


}
