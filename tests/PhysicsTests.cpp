#include "Game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void Expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.0001f;
}

void TestIntegration()
{
    PhyObject body;
    body.SetMass(2.f);
    body.SetVelocity({1.f, 0.f});
    body.SetForce({2.f, 0.f});
    body.Tick(0.1f);
    Expect(Near(body.GetVelocity().x, 1.1f), "F/m must change velocity");
    Expect(Near(body.GetPosition().first, 0.11f), "Fractional movement must be preserved");
    body.Tick(0.f);
    Expect(Near(body.GetPosition().first, 0.11f), "Zero dt must not move a body");
}

void TestElasticCollision()
{
    PhyObject first, second;
    first.SetSize(10, 10);
    second.SetSize(10, 10);
    second.SetPosition(9.f, 0.f);
    first.SetMass(2.f);
    first.SetVelocity({3.f, 2.f});
    auto collision = first.CheckObjectCollided(second);
    Expect(collision && Near(collision->normal.x, 1.f), "Expected horizontal contact");
    first.BumpWith(second, *collision);
    Expect(Near(first.GetVelocity().x, 1.f) && Near(second.GetVelocity().x, 4.f),
           "Elastic collision must conserve momentum and kinetic energy");
    Expect(Near(first.GetVelocity().y, 2.f), "Tangential velocity must be preserved");
    Expect(!first.CheckObjectCollided(second), "Penetration must be corrected");
}

void TestStaticAndSeparatingBodies()
{
    PhyObject body, wall;
    body.SetSize(10, 10);
    wall.SetSize(10, 10);
    wall.SetMass(0.f);
    wall.SetPosition(0.f, 9.f);
    wall.SetVelocity({50.f, 50.f});
    wall.SetForce({10.f, 10.f});
    wall.Tick(1.f);
    body.SetVelocity({2.f, 3.f});
    Game::PrimitiveCalcAllPhysics();
    Expect(Near(body.GetVelocity().y, -3.f), "Static wall must reflect normal velocity");
    Expect(Near(body.GetVelocity().x, 2.f), "Wall must preserve tangential velocity");
    Expect(Near(wall.GetPosition().second, 9.f) && Near(wall.GetVelocity().y, 0.f),
           "Static wall must remain stationary");

    body.SetPosition(0.f, 0.f);
    Game::PrimitiveCalcAllPhysics();
    Expect(Near(body.GetVelocity().y, -3.f), "Separating bodies must not bounce again");
    Expect(!body.CheckObjectCollided(wall), "Separating overlap still needs correction");
}

void TestContactGeometry()
{
    PhyObject small, large;
    small.SetSize(2, 2);
    large.SetSize(10, 10);
    small.SetPosition(4.f, 4.f);
    const auto collision = small.CheckObjectCollided(large);
    Expect(collision && Near(collision->penetration, 6.f), "Containment needs full escape distance");
    small.BumpWith(large, *collision);
    Expect(!small.CheckObjectCollided(large), "Contained bodies must separate");
    small.SetSize(0, 2);
    Expect(!small.CheckObjectCollided(large), "Empty bounds must not collide");
    Expect(!large.CheckObjectCollided(large), "Body must not collide with itself");
}

void TestTunneling()
{
    PhyObject projectile, wall;
    projectile.SetSize(2, 2);
    projectile.SetVelocity({2000.f, 0.f});
    wall.SetSize(10, 20);
    wall.SetPosition(15.f, 0.f);
    wall.SetMass(0.f);
    Game::PrimitiveCalcAllPhysics();
    projectile.Tick(1.f / 60.f);
    Game::PrimitiveCalcAllPhysics();
    Expect(projectile.GetPosition().first > 25.f && Near(projectile.GetVelocity().x, 2000.f),
           "Discrete simulation should miss a wall crossed within a step");
}

void TestDragAndThrow()
{
    constexpr float dt = 1.f / 60.f;
    PhyObject body;
    body.SetSize(20, 20);
    body.SetPosition(10.f, 20.f);
    Expect(!body.Release({15, 25}), "An uncaptured body must not handle release");
    Expect(body.Press({15, 25}), "Mouse press should capture the body");
    Expect(!body.Press({15, 25}), "Repeated press must not restart capture");
    body.Tick(dt);
    Expect(Near(body.GetPosition().first, 10.f) && Near(body.GetPosition().second, 20.f),
           "Grabbing must preserve the offset without jumping");

    body.UpdateMousePosition({115, 75});
    Expect(Near(body.GetPosition().first, 10.f), "Mouse updates must not teleport the body");
    body.Tick(dt);
    Expect(body.GetPosition().first > 10.f && body.GetPosition().first < 110.f,
           "Dragging must move gradually towards the target");
    Expect(body.GetVelocity().x > 0.f && body.GetVelocity().y > 0.f,
           "Dragging must create velocity towards the mouse");
    const auto before = body.GetPosition();
    const auto throwVelocity = body.GetVelocity();
    Expect(body.Release({500, 500}), "Release outside the body must end capture");
    Expect(!body.Release({500, 500}), "Release must fire only once per capture");
    body.UpdateMousePosition({-100, -100});
    body.Tick(dt);
    Expect(Near(body.GetVelocity().x, throwVelocity.x) && Near(body.GetVelocity().y, throwVelocity.y),
           "Released body must retain its throwing velocity");
    Expect(Near(body.GetPosition().first, before.first + throwVelocity.x * dt),
           "Thrown body must continue moving by inertia");
}

void TestDragSettlesAndStaticBodyStays()
{
    PhyObject body;
    body.SetSize(20, 20);
    body.Press({5, 5});
    body.UpdateMousePosition({105, 5});
    for (int i = 0; i < 240; ++i)
    {
        body.Tick(1.f / 60.f);
        Expect(body.GetPosition().first <= 100.0001f, "Dragging must not overshoot the mouse target");
    }
    Expect(Near(body.GetPosition().first, 100.f), "Body should settle at the held mouse target");
    body.Release({105, 5});
    Expect(std::abs(body.GetVelocity().x) < 0.001f, "Stationary mouse should not produce a throw");

    body.SetMass(0.f);
    body.Press({105, 5});
    body.UpdateMousePosition({500, 500});
    body.Tick(1.f);
    Expect(Near(body.GetPosition().first, 100.f) && Near(body.GetPosition().second, 0.f),
           "Dragging must not move static bodies");
    body.Release({500, 500});
}

void TestCollisionBeforeMovement()
{
    PhyObject body, wall;
    body.SetSize(10, 10);
    wall.SetSize(10, 10);
    wall.SetMass(0.f);
    wall.SetPosition(9.f, 0.f);
    body.SetVelocity({60.f, 0.f});
    Game::PrimitiveCalcAllPhysics();
    const float correctedX = body.GetPosition().first;
    body.Tick(1.f / 60.f);
    Expect(Near(body.GetPosition().first, correctedX - 1.f),
           "Tick must move using velocity already reflected by collisions");
}

void TestSweepTimeAndRelativeMotion()
{
    PhyObject projectile, wall;
    projectile.SetSize(2, 2);
    projectile.SetVelocity({2000.f, 0.f});
    wall.SetSize(10, 20);
    wall.SetPosition(15.f, 0.f);
    wall.SetMass(0.f);

    const auto hit = projectile.SweepWith(wall, 1.f / 60.f);
    Expect(hit && Near(hit->time, 13.f / 2000.f) && hit->normal == sf::Vector2f{1.f, 0.f},
           "Sweep must find the first contact with a thin wall");
    Expect(hit->penetration == 0.f && projectile.GetPosition().first == 0.f &&
           projectile.GetVelocity().x == 2000.f, "Sweep must predict without moving or bouncing bodies");
    Expect(!projectile.SweepWith(wall, 0.005f), "Contact beyond dt must be ignored");
    Expect(projectile.SweepWith(wall, hit->time).has_value(), "Contact exactly at dt must be included");
    const auto reverse = wall.SweepWith(projectile, 1.f / 60.f);
    Expect(reverse && Near(reverse->time, hit->time) && reverse->normal == -hit->normal,
           "Swapping bodies must reverse the normal and preserve time");

    wall.SetMass(1.f);
    wall.SetVelocity({500.f, 0.f});
    const auto movingHit = projectile.SweepWith(wall, 1.f / 60.f);
    Expect(movingHit && Near(movingHit->time, 13.f / 1500.f), "Sweep must use relative velocity");
    wall.SetVelocity({2000.f, 0.f});
    Expect(!projectile.SweepWith(wall, 1.f), "Equal velocities cannot close a gap");

    wall.SetMass(0.f);
    wall.SetPosition(3.f, 0.f);
    projectile.SetVelocity({1.e-6f, 0.f});
    const auto slowHit = projectile.SweepWith(wall, 2.e6f);
    Expect(slowHit && std::abs(slowHit->time - 1.e6f) < 1.f,
           "Small nonzero velocities must not be treated as stationary");
    projectile.SetVelocity({2000.f, 0.f});
    wall.SetPosition(2.0001f, 0.f);
    const auto tinyHit = projectile.SweepWith(wall, 1.f / 60.f);
    Expect(tinyHit && tinyHit->time > 0.f && tinyHit->time < 1.e-6f,
           "Small positive contact times must not be discarded");
}

void TestSweepTouchingAndOverlap()
{
    PhyObject body, wall;
    body.SetSize(10, 10);
    wall.SetSize(10, 10);
    wall.SetPosition(10.f, 0.f);
    wall.SetMass(0.f);
    body.SetVelocity({10.f, 0.f});
    const auto touching = body.SweepWith(wall, 1.f);
    Expect(touching && touching->time == 0.f, "An incoming touching body must collide at time zero");
    body.BumpWith(wall, *touching);
    Expect(!body.SweepWith(wall, 1.f), "Reflected contact must not repeat at time zero");
    body.SetVelocity({0.f, 10.f});
    Expect(!body.SweepWith(wall, 1.f), "Sliding along a touching face must not snag");
    body.SetVelocity({0.f, 0.f});
    Expect(!body.SweepWith(wall, 1.f), "Stationary touching bodies need no impulse");

    wall.SetPosition(9.f, 0.f);
    body.SetVelocity({-10.f, 0.f});
    const auto overlap = body.SweepWith(wall, 1.f);
    Expect(overlap && overlap->time == 0.f && Near(overlap->penetration, 1.f),
           "Existing penetration must be reported even when bodies separate");
    body.BumpWith(wall, *overlap);
    Expect(!body.SweepWith(wall, 1.f), "Corrected outgoing overlap must not recur");
    Expect(!body.SweepWith(body, 1.f), "Self collisions must be rejected");
    Expect(!body.SweepWith(wall, -1.f) && !body.SweepWith(wall, std::numeric_limits<float>::infinity()),
           "Invalid time horizons must be rejected");
    body.SetMass(0.f);
    body.SetPosition(9.f, 0.f);
    Expect(!body.SweepWith(wall, 1.f), "Two static bodies must not generate repeated contacts");
    body.SetMass(1.f);
    body.SetSize(0, 10);
    Expect(!body.SweepWith(wall, 1.f), "Empty bounds must not collide");
}

void TestSweepDirectionsAndGrazing()
{
    PhyObject body, wall;
    body.SetSize(10, 10);
    wall.SetSize(10, 10);
    wall.SetMass(0.f);

    body.SetVelocity({0.f, 10.f});
    wall.SetPosition(0.f, 20.f);
    auto hit = body.SweepWith(wall, 1.f);
    Expect(hit && Near(hit->time, 1.f) && hit->normal == sf::Vector2f{0.f, 1.f},
           "Downward contact normal must point down");
    body.SetPosition(0.f, 40.f);
    body.SetVelocity({0.f, -10.f});
    hit = body.SweepWith(wall, 1.f);
    Expect(hit && Near(hit->time, 1.f) && hit->normal == sf::Vector2f{0.f, -1.f},
           "Upward contact normal must point up");
    body.SetPosition(40.f, 20.f);
    body.SetVelocity({-10.f, 0.f});
    wall.SetPosition(20.f, 20.f);
    hit = body.SweepWith(wall, 1.f);
    Expect(hit && Near(hit->time, 1.f) && hit->normal == sf::Vector2f{-1.f, 0.f},
           "Leftward contact normal must point left");

    body.SetPosition(0.f, 0.f);
    body.SetVelocity({10.f, 10.f});
    hit = body.SweepWith(wall, 1.f);
    Expect(hit && Near(hit->time, 1.f), "Incoming diagonal corner contact must be detected");
    wall.SetPosition(20.f, 0.f);
    Expect(!body.SweepWith(wall, 2.f), "An isolated corner graze must not bounce");
    wall.SetPosition(20.f, 20.f);
    body.SetVelocity({0.f, 10.f});
    Expect(!body.SweepWith(wall, 2.f), "A stationary separated axis prevents collision");
}

void TestCCDWallAndMultipleBounces()
{
    {
        PhyObject projectile, wall;
        projectile.SetSize(2, 2);
        projectile.SetVelocity({2000.f, 0.f});
        wall.SetSize(10, 20);
        wall.SetPosition(15.f, 0.f);
        wall.SetMass(0.f);
        Game::TimeDelta = 1.f / 60.f;
        Game::CCDCalcAllPhysics();
        const float expectedX = 13.f - 2000.f * (Game::TimeDelta - 13.f / 2000.f);
        Expect(Near(projectile.GetPosition().first, expectedX) && Near(projectile.GetVelocity().x, -2000.f),
               "CCD must reflect a projectile and integrate the remaining time");
        Expect(wall.GetPosition().first == 15.f, "CCD must not move the static wall");
    }
    {
        PhyObject body, leftWall, rightWall;
        body.SetSize(10, 10);
        body.SetPosition(30.f, 0.f);
        body.SetVelocity({200.f, 0.f});
        leftWall.SetSize(10, 20);
        leftWall.SetMass(0.f);
        rightWall.SetSize(10, 20);
        rightWall.SetMass(0.f);
        rightWall.SetPosition(100.f, 0.f);
        Game::TimeDelta = 1.f;
        Game::CCDCalcAllPhysics();
        Expect(Near(body.GetPosition().first, 70.f) && Near(body.GetVelocity().x, 200.f),
               "CCD must handle two bounces within the same step");
    }
}

void ExpectSpeedGroup(const PhyObject& body, SpeedState expected)
{
    const auto count = [&](const std::vector<PhyObject*>& group)
    {
        return std::count(group.begin(), group.end(), &body);
    };
    Expect(body.GetSpeedState() == expected, "Body speed classification must match its velocity/mass");
    Expect(count(Game::fastObjects) == (expected == SpeedState::fast ? 1 : 0) &&
           count(Game::mediumObjects) == (expected == SpeedState::medium ? 1 : 0) &&
           count(Game::slowObjects) == (expected == SpeedState::slow ? 1 : 0) &&
           count(Game::staticObjects) == (expected == SpeedState::stationary ? 1 : 0),
           "Body must occur in exactly one correct group");
}

void TestAutomaticSpeedGroups()
{
    PhyObject body;
    ExpectSpeedGroup(body, SpeedState::slow);
    body.SetVelocity({249.f, 0.f});
    ExpectSpeedGroup(body, SpeedState::slow);
    body.SetVelocity({-PhyObject::MediumSpeedThreshold, 0.f});
    ExpectSpeedGroup(body, SpeedState::medium);
    body.SetVelocity({180.f, 180.f});
    ExpectSpeedGroup(body, SpeedState::medium);
    body.SetVelocity({0.f, PhyObject::FastSpeedThreshold});
    ExpectSpeedGroup(body, SpeedState::fast);
    body.SetVelocity({0.f, 1500.f});
    ExpectSpeedGroup(body, SpeedState::fast);
    body.SetVelocity({0.f, 0.f});
    ExpectSpeedGroup(body, SpeedState::slow);
    body.SetForce({3000.f, 0.f});
    body.Tick(0.1f);
    ExpectSpeedGroup(body, SpeedState::medium);
    body.Tick(0.3f);
    ExpectSpeedGroup(body, SpeedState::fast);
    body.SetMass(0.f);
    ExpectSpeedGroup(body, SpeedState::stationary);
    body.SetVelocity({2000.f, 0.f});
    ExpectSpeedGroup(body, SpeedState::stationary);
    body.SetMass(1.f);
    ExpectSpeedGroup(body, SpeedState::slow);

    body.SetForce({0.f, 0.f});
    body.SetSize(20, 20);
    const auto position = body.GetPosition();
    const sf::Vector2i mouse{static_cast<int>(position.first) + 5, static_cast<int>(position.second) + 5};
    body.Press(mouse);
    body.UpdateMousePosition({mouse.x + 1000, mouse.y});
    body.Tick(1.f / 60.f);
    ExpectSpeedGroup(body, SpeedState::fast);
    body.Release({mouse.x + 1000, mouse.y});
    ExpectSpeedGroup(body, SpeedState::fast);
}

void TestPrimitivePairsAcrossGroups()
{
    const std::array<float, 3> speeds{100.f, 400.f, 1200.f};
    for (float firstSpeed : speeds)
    {
        for (float secondSpeed : speeds)
        {
            PhyObject first, second;
            first.SetSize(10, 10);
            second.SetSize(10, 10);
            second.SetPosition(9.f, 0.f);
            first.SetVelocity({firstSpeed, 0.f});
            second.SetVelocity({-secondSpeed, 0.f});
            Game::PrimitiveCalcAllPhysics();
            Expect(Near(first.GetVelocity().x, -secondSpeed) && Near(second.GetVelocity().x, firstSpeed),
                   "Each pair of speed groups must collide once and exchange velocities");
            const auto stateFor = [](float speed)
            {
                return speed >= PhyObject::FastSpeedThreshold ? SpeedState::fast :
                       speed >= PhyObject::MediumSpeedThreshold ? SpeedState::medium : SpeedState::slow;
            };
            ExpectSpeedGroup(first, stateFor(secondSpeed));
            ExpectSpeedGroup(second, stateFor(firstSpeed));
        }
    }
}

void TestMultiRateFrequencies()
{
    PhyObject slow, medium, fast, wall;
    slow.SetPosition(0.f, 0.f);
    medium.SetPosition(0.f, 100.f);
    fast.SetPosition(0.f, 200.f);
    wall.SetPosition(0.f, 300.f);
    medium.SetVelocity({300.f, 0.f});
    fast.SetVelocity({1200.f, 0.f});
    slow.SetForce({10.f, 0.f});
    medium.SetForce({10.f, 0.f});
    fast.SetForce({10.f, 0.f});
    wall.SetMass(0.f);
    wall.SetForce({1000.f, 0.f});
    Game::TimeDelta = 1.f;
    Game::MultyRateCalcPhysics();

    // Euler displacement under constant acceleration exposes the number of substeps.
    Expect(Near(slow.GetPosition().first, 10.f) && Near(slow.GetVelocity().x, 10.f),
           "Slow body must take one full step");
    Expect(Near(medium.GetPosition().first, 307.5f) && Near(medium.GetVelocity().x, 310.f),
           "Medium body must take two half steps");
    Expect(Near(fast.GetPosition().first, 1206.25f) && Near(fast.GetVelocity().x, 1210.f),
           "Fast body must take four quarter steps");
    Expect(wall.GetPosition().first == 0.f && wall.GetVelocity().x == 0.f,
           "Static group must never be integrated");
    Expect(Game::GetPhyObjectsSnapshot(false).size() == 3 && Game::staticObjects.size() == 1,
           "Moving-only snapshot must exclude static bodies");
}

void TestMultiRateMigrationDuringStep()
{
    PhyObject body;
    body.SetForce({1200.f, 0.f});
    Game::TimeDelta = 1.f;
    Game::MultyRateCalcPhysics();
    Expect(Near(body.GetPosition().first, 1200.f) && Near(body.GetVelocity().x, 1200.f),
           "Promotion during a slow step must not tick the body again");
    ExpectSpeedGroup(body, SpeedState::fast);
    Game::MultyRateCalcPhysics();
    Expect(Near(body.GetPosition().first, 3150.f) && Near(body.GetVelocity().x, 2400.f),
           "New fast frequency must apply on the next base step");

    body.SetPosition(0.f, 0.f);
    body.SetVelocity({1200.f, 0.f});
    body.SetForce({-4800.f, 0.f});
    Game::MultyRateCalcPhysics();
    Expect(Near(body.GetPosition().first, -1800.f) && Near(body.GetVelocity().x, -3600.f),
           "Demotion and promotion must not skip or duplicate scheduled substeps");
    ExpectSpeedGroup(body, SpeedState::fast);
}

void TestMultiRateThinWall()
{
    PhyObject projectile, wall;
    projectile.SetSize(2, 2);
    projectile.SetPosition(0.f, 9.f);
    projectile.SetVelocity({2000.f, 0.f});
    wall.SetSize(10, 20);
    wall.SetPosition(15.f, 0.f);
    wall.SetMass(0.f);
    Game::TimeDelta = 1.f / 60.f;
    Game::PrimitiveCalcAllPhysics();
    projectile.Tick(Game::TimeDelta);
    Game::PrimitiveCalcAllPhysics();
    Expect(projectile.GetPosition().first > 25.f && projectile.GetVelocity().x == 2000.f,
           "The identical projectile must tunnel at the base 60 Hz rate");
    projectile.SetPosition(0.f, 9.f);
    Game::MultyRateCalcPhysics();
    Expect(Near(projectile.GetVelocity().x, -2000.f) && projectile.GetPosition().first < 15.f,
           "Fast substeps must detect the wall missed by the 60 Hz discrete test");
    ExpectSpeedGroup(wall, SpeedState::stationary);
}

void TestCCDMigrationDuringBounce()
{
    PhyObject projectile, target;
    projectile.SetSize(2, 2);
    target.SetSize(10, 20);
    projectile.SetVelocity({2000.f, 0.f});
    target.SetPosition(15.f, 0.f);
    Game::TimeDelta = 1.f / 60.f;
    Game::CCDCalcAllPhysics();
    ExpectSpeedGroup(projectile, SpeedState::slow);
    ExpectSpeedGroup(target, SpeedState::fast);
    Expect(Near(projectile.GetPosition().first, 13.f) &&
           Near(target.GetPosition().first, 15.f + 2000.f * (Game::TimeDelta - 13.f / 2000.f)),
           "CCD must continue integrating both bodies after an impulse exchanges their groups");
}

void TestCCDWithForcesAndDragging()
{
    {
        PhyObject body, wall;
        body.SetSize(2, 2);
        body.SetPosition(0.f, 9.f);
        body.SetForce({1000.f, 0.f});
        wall.SetSize(10, 20);
        wall.SetPosition(8.f, 0.f);
        wall.SetMass(0.f);
        Game::TimeDelta = 0.1f;
        Game::BeginPhysicsFrame();
        Game::CCDCalcAllPhysics();
        Expect(Near(body.GetVelocity().x, -100.f) && Near(body.GetPosition().first, 2.f),
               "CCD must apply force once, predict with the updated velocity, then move without reapplying force");
        Expect(Game::physicsMetrics.ccdContacts == 1 && Game::physicsMetrics.ccdLimitHits == 0,
               "One wall contact should complete without exhausting the budget");
    }
    {
        PhyObject body, wall;
        body.SetSize(2, 2);
        body.SetPosition(0.f, 9.f);
        wall.SetSize(10, 20);
        wall.SetPosition(10.f, 0.f);
        wall.SetMass(0.f);
        body.Press({1, 10});
        body.UpdateMousePosition({200, 10});
        Game::TimeDelta = 1.f / 60.f;
        Game::CCDCalcAllPhysics();
        Expect(body.GetVelocity().x < 0.f && body.GetPosition().first < 8.f,
               "Drag steering must be applied before the CCD prediction, not during contact movement");
        body.Release({200, 10});
    }
}

void TestCCDSubpixelContactRoundoff()
{
    PhyObject small, large;
    small.SetSize(8, 8);
    small.SetMass(0.25f);
    small.SetPosition(522.817565918f, 221.549163818f);
    large.SetSize(24, 24);
    large.SetPosition(498.817596436f, 208.377655029f);
    small.SetVelocity({1494.67138672f, 30.f});
    large.SetVelocity({-248.984832764f, -34.f});
    const auto overlap = small.CheckObjectCollided(large);
    Expect(overlap && overlap->penetration > 0.f && overlap->penetration < 0.0001f,
           "Regression fixture must reproduce the float-sized penetration found in the many scene");
    Expect(!small.SweepWith(large, 1.f / 60.f),
           "Separating bodies with sub-ULP position corrections must not create a repeated zero-time event");
    const float originalX = small.GetPosition().first;
    Game::TimeDelta = 1.f / 60.f;
    Game::BeginPhysicsFrame();
    Game::CCDCalcAllPhysics();
    Expect(Game::physicsMetrics.ccdLimitHits == 0 && Game::physicsMetrics.ccdContacts == 0 &&
           small.GetPosition().first > originalX + 20.f,
           "CCD must advance the full step instead of freezing on an outgoing roundoff overlap");

    small.SetPosition(522.817565918f, 221.549163818f);
    large.SetPosition(498.817596436f, 208.377655029f);
    small.SetVelocity({-1000.f, 0.f});
    large.SetVelocity({0.f, 0.f});
    const auto incoming = small.SweepWith(large, Game::TimeDelta);
    Expect(incoming && incoming->time == 0.f && incoming->penetration == 0.f,
           "An incoming tiny overlap still needs its collision impulse");
    Game::BeginPhysicsFrame();
    Game::CCDCalcAllPhysics();
    Expect(Game::physicsMetrics.ccdLimitHits == 0 && small.GetVelocity().x > 0.f && large.GetVelocity().x < 0.f,
           "After an incoming contact, the outgoing tiny overlap must not stall the next sweep");
}

void TestCCDContactBudget()
{
    PhyObject body, left, right;
    body.SetSize(10, 10);
    left.SetSize(10, 20);
    left.SetMass(0.f);
    left.SetPosition(-10.f, 0.f);
    right.SetSize(10, 20);
    right.SetMass(0.f);
    right.SetPosition(8.f, 0.f);
    // A ten-pixel body cannot fit in this eight-pixel corridor.
    Game::TimeDelta = 1.f / 60.f;
    Game::BeginPhysicsFrame();
    Game::CCDCalcAllPhysics();
    Expect(Game::physicsMetrics.ccdLimitHits == 1 && Game::physicsMetrics.ccdContacts > 0,
           "Impossible overlapping constraints must hit a finite budget instead of hanging");
    Expect(std::isfinite(body.GetPosition().first), "Budget exhaustion must preserve finite positions");
}

void TestPhysicsMeasurements()
{
    PhyObject slow, medium, fast;
    medium.SetVelocity({400.f, 0.f});
    fast.SetVelocity({1200.f, 0.f});
    Game::TimeDelta = 1.f / 60.f;
    Game::gameState = GameState::MultyRate;
    Game::BeginPhysicsFrame();
    Game::AdvancePhysics();
    Expect(Game::physicsMetrics.steps == 1 && Game::physicsMetrics.slowSubsteps == 1 &&
           Game::physicsMetrics.mediumSubsteps == 2 && Game::physicsMetrics.fastSubsteps == 4,
           "Metrics must count actual group substeps within a render frame");
    Game::AdvancePhysics();
    Expect(Game::physicsMetrics.steps == 2 && Game::physicsMetrics.fastSubsteps == 8 &&
           Game::physicsMetrics.frameMilliseconds >= Game::physicsMetrics.lastStepMilliseconds &&
           Game::physicsMetrics.lastStepMilliseconds >= 0.0,
           "Frame timing and counters must accumulate multiple base steps");
    const double lastStep = Game::physicsMetrics.lastStepMilliseconds;
    Game::BeginPhysicsFrame();
    Expect(Game::physicsMetrics.steps == 0 && Game::physicsMetrics.fastSubsteps == 0 &&
           Game::physicsMetrics.frameMilliseconds == 0.0 && Game::physicsMetrics.lastStepMilliseconds == lastStep,
           "An idle/paused frame must clear frame metrics and retain the latest step timing");
    Game::gameState = GameState::Primitive;
}

int main()
{
    try
    {
        TestIntegration();
        TestElasticCollision();
        TestStaticAndSeparatingBodies();
        TestContactGeometry();
        TestTunneling();
        TestDragAndThrow();
        TestDragSettlesAndStaticBodyStays();
        TestCollisionBeforeMovement();
        TestSweepTimeAndRelativeMotion();
        TestSweepTouchingAndOverlap();
        TestSweepDirectionsAndGrazing();
        TestCCDWallAndMultipleBounces();
        TestAutomaticSpeedGroups();
        TestPrimitivePairsAcrossGroups();
        TestMultiRateFrequencies();
        TestMultiRateMigrationDuringStep();
        TestMultiRateThinWall();
        TestCCDMigrationDuringBounce();
        TestCCDWithForcesAndDragging();
        TestCCDSubpixelContactRoundoff();
        TestCCDContactBudget();
        TestPhysicsMeasurements();
        Expect(Game::GetPhyObjectsSnapshot().empty() && Game::objects.empty() && Game::buttons.empty(),
               "Destruction must unregister bodies from all registries");
        std::cout << "All physics tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
