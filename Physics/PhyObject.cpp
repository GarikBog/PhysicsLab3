#include "PhyObject.h"
#include "Game.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>

PhyObject::PhyObject()
    : mass(1.f), force{0.f, 0.f}, velocity{0.f, 0.f}
{
    Game::AddMeToPhyObjects(this);
}

PhyObject::~PhyObject()
{
    Game::RemoveMeFromPhyObjects(this);
}

void PhyObject::SetMass(float value)
{
    if (!std::isfinite(value) || value < 0.f)
        throw std::invalid_argument("Mass must be finite and non-negative");

    mass = value;
    if (mass == 0.f)
    {
        velocity = {0.f, 0.f};
        dragging = false;
    }
    UpdateSpeedState();
}

float PhyObject::GetMass() const
{
    return mass;
}

void PhyObject::SetVelocity(sf::Vector2f value)
{
    velocity = mass > 0.f ? value : sf::Vector2f{0.f, 0.f};
    UpdateSpeedState();
}

sf::Vector2f PhyObject::GetVelocity() const
{
    return velocity;
}

SpeedState PhyObject::GetSpeedState() const
{
    return speedState;
}

void PhyObject::UpdateSpeedState()
{
    const float speed = std::hypot(velocity.x, velocity.y);
    const SpeedState nextState = mass == 0.f ? SpeedState::stationary :
                                 speed >= FastSpeedThreshold ? SpeedState::fast :
                                 speed >= MediumSpeedThreshold ? SpeedState::medium : SpeedState::slow;
    if (nextState == speedState)
        return;

    const SpeedState previousState = speedState;
    speedState = nextState;
    Game::MovePhyObjectToSpeedGroup(this, previousState);
}

void PhyObject::SetForce(sf::Vector2f value)
{
    force = value;
}

void PhyObject::Tick(float TimeDelta)
{
    UpdateVelocity(TimeDelta);
    Move(TimeDelta);
}

sf::Vector2f PhyObject::GetForce() const
{
    return force;
}

void PhyObject::UpdateVelocity(float TimeDelta)
{
    if (mass == 0.f || !std::isfinite(TimeDelta) || TimeDelta <= 0.f)
        return;

    // Semi-implicit Euler. Force persists until SetForce changes it.
    velocity += (force / mass) * TimeDelta;
    const auto currentPosition = GetPosition();
    if (dragging)
    {
        const sf::Vector2f target = GetMousePosition() - grabOffset;
        const sf::Vector2f delta = target - sf::Vector2f{currentPosition.first, currentPosition.second};
        constexpr float followRate = 12.f;
        constexpr float response = 24.f;
        const sf::Vector2f desiredVelocity = delta * std::min(followRate, 1.f / TimeDelta);
        // Gradually steer velocity towards the mouse instead of teleporting the body.
        velocity += (desiredVelocity - velocity) * (1.f - std::exp(-response * TimeDelta));

        // A single step must not travel farther than the remaining mouse delta.
        const float distance = std::hypot(delta.x, delta.y);
        const float speed = std::hypot(velocity.x, velocity.y);
        const float maxSpeed = distance / TimeDelta;
        if (speed > maxSpeed)
            velocity *= maxSpeed / speed;
    }
    UpdateSpeedState();
}

void PhyObject::Move(float TimeDelta)
{
    if (mass == 0.f || !std::isfinite(TimeDelta) || TimeDelta <= 0.f)
        return;
    const auto currentPosition = GetPosition();
    SetPosition(currentPosition.first + velocity.x * TimeDelta,
                currentPosition.second + velocity.y * TimeDelta);
}

std::optional<CollisionInfo> PhyObject::CheckObjectCollided(const PhyObject& other) const
{
    if (this == &other)
        return std::nullopt;

    const auto bounds = GetBounds();
    const auto otherBounds = other.GetBounds();
    if (bounds.size.x <= 0.f || bounds.size.y <= 0.f ||
        otherBounds.size.x <= 0.f || otherBounds.size.y <= 0.f)
        return std::nullopt;

    const sf::Vector2f center = bounds.position + bounds.size / 2.f;
    const sf::Vector2f otherCenter = otherBounds.position + otherBounds.size / 2.f;
    const sf::Vector2f delta = otherCenter - center;
    const sf::Vector2f halfSizes = (bounds.size + otherBounds.size) / 2.f;
    const float overlapX = halfSizes.x - std::abs(delta.x);
    const float overlapY = halfSizes.y - std::abs(delta.y);
    if (overlapX <= 0.f || overlapY <= 0.f)
        return std::nullopt;

    // Normal points from this body towards the other body.
    if (overlapX < overlapY)
        return CollisionInfo{{delta.x >= 0.f ? 1.f : -1.f, 0.f}, overlapX};

    return CollisionInfo{{0.f, delta.y >= 0.f ? 1.f : -1.f}, overlapY};
}

std::optional<CollisionInfo> PhyObject::SweepWith(const PhyObject& other, float dt) const
{
    if (this == &other || !std::isfinite(dt) || dt < 0.f ||
        (mass == 0.f && other.mass == 0.f))
        return std::nullopt;

    const auto bounds = GetBounds();
    const auto otherBounds = other.GetBounds();
    if (bounds.size.x <= 0.f || bounds.size.y <= 0.f ||
        otherBounds.size.x <= 0.f || otherBounds.size.y <= 0.f)
        return std::nullopt;

    // Keep the other body stationary and move this body at the relative velocity.
    const sf::Vector2f relativeVelocity = velocity - other.velocity;
    if (!std::isfinite(relativeVelocity.x) || !std::isfinite(relativeVelocity.y))
        return std::nullopt;

    // Existing penetration needs correction, except for outgoing contact within float roundoff.
    if (auto overlap = CheckObjectCollided(other))
    {
        const float coordinateScale = std::max({1.f,
            std::abs(bounds.position.x), std::abs(bounds.position.y),
            std::abs(bounds.position.x + bounds.size.x), std::abs(bounds.position.y + bounds.size.y),
            std::abs(otherBounds.position.x), std::abs(otherBounds.position.y),
            std::abs(otherBounds.position.x + otherBounds.size.x),
            std::abs(otherBounds.position.y + otherBounds.size.y)});
        const float contactSlop = 8.f * std::numeric_limits<float>::epsilon() * coordinateScale;
        if (overlap->penetration <= contactSlop)
        {
            const float approachSpeed = relativeVelocity.x * overlap->normal.x +
                                        relativeVelocity.y * overlap->normal.y;
            if (approachSpeed <= 0.f)
                return std::nullopt;

            // An incoming contact still needs its impulse, but a sub-pixel position correction
            // can round to zero for both bodies and must not be iterated indefinitely.
            overlap->penetration = 0.f;
        }
        overlap->time = 0.f;
        return overlap;
    }

    const auto axisTimes = [](float minA, float maxA, float minB, float maxB, float speed)
        -> std::optional<std::pair<double, double>>
    {
        if (speed == 0.f)
        {
            // A gap cannot close on this axis. Mere tangential touching is not a hit.
            if (maxA <= minB || maxB <= minA)
                return std::nullopt;

            const double infinity = std::numeric_limits<double>::infinity();
            return std::pair{-infinity, infinity};
        }

        // Double intermediates reduce rounding errors when comparing entry times.
        const double first = (static_cast<double>(minB) - maxA) / speed;
        const double second = (static_cast<double>(maxB) - minA) / speed;
        return std::pair{std::min(first, second), std::max(first, second)};
    };

    const auto xTimes = axisTimes(bounds.position.x, bounds.position.x + bounds.size.x,
                                 otherBounds.position.x, otherBounds.position.x + otherBounds.size.x,
                                 relativeVelocity.x);
    const auto yTimes = axisTimes(bounds.position.y, bounds.position.y + bounds.size.y,
                                 otherBounds.position.y, otherBounds.position.y + otherBounds.size.y,
                                 relativeVelocity.y);
    if (!xTimes || !yTimes)
        return std::nullopt;

    const double enterTime = std::max(xTimes->first, yTimes->first);
    const double exitTime = std::min(xTimes->second, yTimes->second);
    // Equal entry/exit times describe a corner graze with no penetration afterwards.
    if (!std::isfinite(enterTime) || enterTime < 0.0 || enterTime > dt || enterTime >= exitTime)
        return std::nullopt;

    sf::Vector2f normal;
    if (xTimes->first > yTimes->first)
        normal = {relativeVelocity.x > 0.f ? 1.f : -1.f, 0.f};
    else
        normal = {0.f, relativeVelocity.y > 0.f ? 1.f : -1.f};

    // Same normal convention as BumpWith: this -> other. Do not repeat an outgoing contact.
    const sf::Vector2f separationVelocity = other.velocity - velocity;
    const float normalVelocity = separationVelocity.x * normal.x + separationVelocity.y * normal.y;
    if (normalVelocity >= 0.f)
        return std::nullopt;

    return CollisionInfo{normal, 0.f, static_cast<float>(enterTime)};
}

void PhyObject::BumpWith(PhyObject& other, const CollisionInfo& collision)
{
    if (this == &other)
        return;

    const float inverseMass = mass > 0.f ? 1.f / mass : 0.f;
    const float otherInverseMass = other.mass > 0.f ? 1.f / other.mass : 0.f;
    const float inverseMassSum = inverseMass + otherInverseMass;
    if (inverseMassSum == 0.f)
        return;

    const sf::Vector2f relativeVelocity = other.velocity - velocity;
    const float normalVelocity = relativeVelocity.x * collision.normal.x +
                                 relativeVelocity.y * collision.normal.y;
    if (normalVelocity < 0.f)
    {
        constexpr float restitution = 1.f; // Perfectly elastic collision, no friction.
        const float impulseMagnitude = -(1.f + restitution) * normalVelocity / inverseMassSum;
        const sf::Vector2f impulse = collision.normal * impulseMagnitude;
        velocity -= impulse * inverseMass;
        other.velocity += impulse * otherInverseMass;
        UpdateSpeedState();
        other.UpdateSpeedState();
    }

    // Separate overlapping bodies even if their velocities already point apart.
    if (collision.penetration > 0.f)
    {
        const sf::Vector2f correction = collision.normal * (collision.penetration / inverseMassSum);
        const auto currentPosition = GetPosition();
        const auto otherPosition = other.GetPosition();
        SetPosition(currentPosition.first - correction.x * inverseMass,
                    currentPosition.second - correction.y * inverseMass);
        other.SetPosition(otherPosition.first + correction.x * otherInverseMass,
                          otherPosition.second + correction.y * otherInverseMass);
    }
}

void PhyObject::OnPressed()
{
    if (mass == 0.f)
        return;

    const auto currentPosition = GetPosition();
    grabOffset = GetMousePosition() - sf::Vector2f{currentPosition.first, currentPosition.second};
    dragging = true;
}

void PhyObject::OnReleased()
{
    // Preserve velocity so the body continues moving after the throw.
    dragging = false;
}
