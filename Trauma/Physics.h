#pragma once
#include "box2d/box2d.h"
#include "olcPixelGameEngine3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Trauma {
	constexpr float FixedDeltaTime = 1.0f / 60.0f;
	constexpr float Gravity = 1600.0f;
	constexpr float PlayerRadius = 10.0f;
	constexpr float TwoPi = 6.2831853f;
	constexpr float RestDamping = 0.5f;
	constexpr float RestSpeed = 4.0f;
	constexpr float WakeSpeed = 6.0f;

	constexpr float BodyFriction = 0.6f;
	constexpr float BodyRestitution = 0.05f;
	constexpr float BodyAngularDamping = 1.5f;
	constexpr float RagdollLinearDamping = 0.15f;
	constexpr float GroundContactNormal = 0.6f;
	constexpr float MinimumRadius = 0.1f;
	constexpr int MaxPolygonVertices = 8;
	constexpr int MaxBodyContacts = 8;

	enum class Shape : uint8_t {
		Circle = 0,
		Polygon = 1
	};

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
		return body;
	}

	inline float ShapeArea(const ObjectState& body) {
		if (body.shape == Shape::Polygon && body.vertexCount >= 3)
			return std::abs(PolygonSignedArea(body.localVertices.data(), body.vertexCount));
		return 3.14159265f * std::max(body.radius, MinimumRadius) * std::max(body.radius, MinimumRadius);
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

	inline Polygon WorldVertices(const ObjectState& body) {
		Polygon result;
		if (body.shape != Shape::Polygon)
			return result;
		result.count = body.vertexCount;
		for (int i = 0; i < result.count; ++i)
			result.vertices[i] = body.position + Rotate(body.localVertices[i], body.angle);
		return result;
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

	inline void Wake(ObjectState& body) {
		body.sleeping = false;
		body.restTime = 0.0f;
	}

	inline uint32_t BodySignature(const ObjectState& object) {
		uint32_t hash = 2166136261u;
		hash = (hash ^ static_cast<uint32_t>(object.shape)) * 16777619u;
		hash = (hash ^ static_cast<uint32_t>(object.vertexCount)) * 16777619u;
		hash = (hash ^ static_cast<uint32_t>(std::abs(object.radius) * 16.0f)) * 16777619u;
		hash = (hash ^ static_cast<uint32_t>(object.inverseMass * 1024.0f)) * 16777619u;
		for (int i = 0; i < object.vertexCount; ++i) {
			hash = (hash ^ static_cast<uint32_t>(object.localVertices[i].x * 16.0f + 1024.0f)) * 16777619u;
			hash = (hash ^ static_cast<uint32_t>(object.localVertices[i].y * 16.0f + 1024.0f)) * 16777619u;
		}
		return hash;
	}

	struct PhysicsBody {
		const ObjectState* key = nullptr;
		b2BodyId body = b2_nullBodyId;
		uint32_t signature = 0;
		int group = 0;
		bool seen = false;
	};

	struct PhysicsRig {
		const void* key = nullptr;
		int group = 0;
		std::array<b2BodyId, 16> bodies{};
		std::array<b2JointId, 16> joints{};
		std::array<b2JointId, 16> pose{};
		std::array<b2JointId, 2> grips{};
		std::array<uint8_t, 2> gripTargets{};
		bool seen = false;
	};

	struct PhysicsWorld {
		b2WorldId world = b2_nullWorldId;
		std::vector<b2BodyId> terrain;
		std::vector<PhysicsBody> bodies;
		std::vector<PhysicsRig> rigs;
	};

	inline void ResetWorld(PhysicsWorld& physics) {
		for (PhysicsBody& entry : physics.bodies)
			if (b2Body_IsValid(entry.body))
				b2DestroyBody(entry.body);
		physics.bodies.clear();
		physics.rigs.clear();

		for (const b2BodyId body : physics.terrain)
			if (b2Body_IsValid(body))
				b2DestroyBody(body);
		physics.terrain.clear();

		if (b2World_IsValid(physics.world))
			b2DestroyWorld(physics.world);

		b2WorldDef def = b2DefaultWorldDef();
		def.gravity = { 0.0f, Gravity };
		def.enableSleep = true;
		def.enableContinuous = true;
		physics.world = b2CreateWorld(&def);
	}

	inline b2BodyId BuildBody(PhysicsWorld& physics, const ObjectState& object, int group = 0) {
		b2BodyDef def = b2DefaultBodyDef();
		def.type = object.inverseMass > 0.0f ? b2_dynamicBody : b2_staticBody;
		def.position = { object.position.x, object.position.y };
		def.rotation = b2MakeRot(object.angle);
		def.linearVelocity = { object.velocity.x, object.velocity.y };
		def.angularVelocity = object.angularVelocity;
		def.linearDamping = group < 0 ? RagdollLinearDamping : 0.0f;
		def.angularDamping = BodyAngularDamping;
		def.enableSleep = object.inverseMass > 0.0f && group >= 0;
		def.isAwake = true;

		const b2BodyId body = b2CreateBody(physics.world, &def);

		b2ShapeDef shapeDef = b2DefaultShapeDef();
		shapeDef.material.friction = BodyFriction;
		shapeDef.material.restitution = BodyRestitution;
		shapeDef.filter.groupIndex = group;
		shapeDef.density = object.inverseMass > 0.0f
			? std::max((1.0f / object.inverseMass) / std::max(ShapeArea(object), 0.0001f), 0.0001f)
			: 0.0f;

		if (object.shape == Shape::Polygon && object.vertexCount >= 3) {
			b2Vec2 points[MaxPolygonVertices];
			for (int i = 0; i < object.vertexCount; ++i)
				points[i] = { object.localVertices[i].x, object.localVertices[i].y };
			const b2Hull hull = b2ComputeHull(points, object.vertexCount);
			if (hull.count >= 3) {
				const b2Polygon polygon = b2MakePolygon(&hull, 0.0f);
				b2CreatePolygonShape(body, &shapeDef, &polygon);
			}
		}
		else {
			const b2Circle circle{ { 0.0f, 0.0f }, std::max(object.radius, MinimumRadius) };
			b2CreateCircleShape(body, &shapeDef, &circle);
		}

		return body;
	}

	inline b2BodyId EnsureBody(PhysicsWorld& physics, const ObjectState& object, int group = 0) {
		const uint32_t signature = BodySignature(object);
		for (PhysicsBody& entry : physics.bodies)
			if (entry.key == &object && entry.signature == signature && entry.group == group
				&& b2Body_IsValid(entry.body)) {
				entry.seen = true;
				return entry.body;
			}
		for (PhysicsBody& entry : physics.bodies)
			if (entry.key == &object) {
				if (b2Body_IsValid(entry.body))
					b2DestroyBody(entry.body);
				entry.body = BuildBody(physics, object, group);
				entry.signature = signature;
				entry.group = group;
				entry.seen = true;
				return entry.body;
			}

		PhysicsBody entry;
		entry.key = &object;
		entry.body = BuildBody(physics, object, group);
		entry.signature = signature;
		entry.group = group;
		entry.seen = true;
		physics.bodies.push_back(entry);
		return entry.body;
	}

	inline void SetBodyTransform(b2BodyId body, const ObjectState& object) {
		b2Body_SetTransform(body, { object.position.x, object.position.y }, b2MakeRot(object.angle));
		b2Body_SetLinearVelocity(body, { object.velocity.x, object.velocity.y });
		b2Body_SetAngularVelocity(body, object.angularVelocity);
		b2Body_SetAwake(body, true);
	}

	inline void SyncBody(ObjectState& object, b2BodyId body) {
		if (!b2Body_IsValid(body))
			return;

		const b2Transform transform = b2Body_GetTransform(body);
		const b2Vec2 velocity = b2Body_GetLinearVelocity(body);
		object.position = { transform.p.x, transform.p.y };
		object.angle = b2Rot_GetAngle(transform.q);
		object.velocity = { velocity.x, velocity.y };
		object.angularVelocity = b2Body_GetAngularVelocity(body);
		object.sleeping = !b2Body_IsAwake(body);

		b2ContactData contacts[MaxBodyContacts];
		const int count = b2Body_GetContactData(body, contacts, MaxBodyContacts);
		bool grounded = false;
		for (int i = 0; i < count && !grounded; ++i)
			if (contacts[i].manifold.pointCount > 0
				&& std::abs(contacts[i].manifold.normal.y) > GroundContactNormal)
				grounded = true;
		object.grounded = grounded;
		object.supported = grounded;
	}

	inline void PruneBodies(PhysicsWorld& physics) {
		std::vector<PhysicsBody> kept;
		kept.reserve(physics.bodies.size());
		for (PhysicsBody& entry : physics.bodies) {
			if (entry.seen) {
				entry.seen = false;
				kept.push_back(entry);
				continue;
			}
			if (b2Body_IsValid(entry.body))
				b2DestroyBody(entry.body);
		}
		physics.bodies.swap(kept);

		std::vector<PhysicsRig> keptRigs;
		keptRigs.reserve(physics.rigs.size());
		for (PhysicsRig& rig : physics.rigs) {
			if (rig.seen) {
				rig.seen = false;
				keptRigs.push_back(rig);
				continue;
			}
			for (b2JointId& joint : rig.joints)
				if (b2Joint_IsValid(joint))
					b2DestroyJoint(joint);
			for (b2JointId& pose : rig.pose)
				if (b2Joint_IsValid(pose))
					b2DestroyJoint(pose);
			for (b2JointId& grip : rig.grips)
				if (b2Joint_IsValid(grip))
					b2DestroyJoint(grip);
		}
		physics.rigs.swap(keptRigs);
	}
	inline int RagdollGroup(PhysicsWorld& physics, const void* key) {
		for (const PhysicsRig& rig : physics.rigs)
			if (rig.key == key)
				return rig.group;
		physics.rigs.push_back(PhysicsRig{});
		PhysicsRig& rig = physics.rigs.back();
		rig.key = key;
		rig.group = -static_cast<int>(physics.rigs.size());
		rig.bodies.fill(b2_nullBodyId);
		rig.joints.fill(b2_nullJointId);
		rig.pose.fill(b2_nullJointId);
		rig.grips.fill(b2_nullJointId);
		return rig.group;
	}
}
