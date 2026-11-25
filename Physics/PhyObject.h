#ifndef PHYSOBJECT
#define PHYSOBJECT

#include "Button.h"

enum class SpeedState
{
    fast,
    medium,
    slow,
    stationary
};

struct CollisionInfo
{
    sf::Vector2f normal;
    float penetration;
    float time;
};

class PhyObject :
    public Button
{
    public:
    PhyObject();
	virtual ~PhyObject();


    // Zero mass represents a static body.
    void SetMass(float mass);
    float GetMass() const;

    void SetVelocity(sf::Vector2f velocity);
    sf::Vector2f GetVelocity() const;
    SpeedState GetSpeedState() const;
    static constexpr float MediumSpeedThreshold = 250.f; // px/s
    static constexpr float FastSpeedThreshold = 1000.f;  // px/s
    void SetForce(sf::Vector2f force);
    sf::Vector2f GetForce() const;

    void Tick(float TimeDelta);
    void UpdateVelocity(float TimeDelta);
    void Move(float TimeDelta);

    std::optional<CollisionInfo> CheckObjectCollided(const PhyObject& other) const;
    std::optional<CollisionInfo>
        SweepWith(const PhyObject& other, float dt) const;

    void BumpWith(PhyObject& other, const CollisionInfo& collision);
protected:
    void OnPressed() override;
    void OnReleased() override;

private:
    void UpdateSpeedState();

    float mass;
    sf::Vector2f force;
    sf::Vector2f velocity;
    SpeedState speedState = SpeedState::slow;
    bool dragging = false;
    sf::Vector2f grabOffset{0.f, 0.f};


};


#endif // !PHYSOBJECT
