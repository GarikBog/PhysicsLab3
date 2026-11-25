#include "Game.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>

namespace
{
    std::vector<PhyObject*>& SpeedGroup(SpeedState state)
    {
        switch (state)
        {
        case SpeedState::fast: return Game::fastObjects;
        case SpeedState::medium: return Game::mediumObjects;
        case SpeedState::slow: return Game::slowObjects;
        case SpeedState::stationary: return Game::staticObjects;
        }
        return Game::slowObjects;
    }

    std::size_t MovingObjectCount()
    {
        return Game::fastObjects.size() + Game::mediumObjects.size() + Game::slowObjects.size();
    }
}

namespace Game
{
    PhysicsMetrics physicsMetrics;
    GameState gameState = GameState::Primitive;
    float TimeDelta;
    std::vector<Button*> buttons;
    std::vector<Object*> objects;
    std::vector<PhyObject*> fastObjects;
    std::vector<PhyObject*> mediumObjects;
    std::vector<PhyObject*> slowObjects;
    std::vector<PhyObject*> staticObjects;

    void AddMeToButtons(Button* button)
    {
        buttons.push_back(button);
    }

    void RemoveMeFromButtons(Button* button)
    {
        buttons.erase(std::remove(buttons.begin(), buttons.end(), button), buttons.end());
    }

    void AddMeToObjectsObjects(Object* object)
    {
        objects.push_back(object);
    }

    void RemoveMeFromObjectsObjects(Object* object)
    {
        objects.erase(std::remove(objects.begin(), objects.end(), object), objects.end());
    }

    void AddMeToPhyObjects(PhyObject* phyObject)
    {
        SpeedGroup(phyObject->GetSpeedState()).push_back(phyObject);
    }

    void RemoveMeFromPhyObjects(PhyObject* phyObject)
    {
        auto& group = SpeedGroup(phyObject->GetSpeedState());
        group.erase(std::remove(group.begin(), group.end(), phyObject), group.end());
    }

    void MovePhyObjectToSpeedGroup(PhyObject* phyObject, SpeedState previousState)
    {
        auto& previousGroup = SpeedGroup(previousState);
        previousGroup.erase(std::remove(previousGroup.begin(), previousGroup.end(), phyObject), previousGroup.end());
        AddMeToPhyObjects(phyObject);
    }

    std::vector<PhyObject*> GetPhyObjectsSnapshot(bool includeStatic)
    {
        std::vector<PhyObject*> snapshot;
        snapshot.reserve(MovingObjectCount() + (includeStatic ? staticObjects.size() : 0));
        snapshot.insert(snapshot.end(), fastObjects.begin(), fastObjects.end());
        snapshot.insert(snapshot.end(), mediumObjects.begin(), mediumObjects.end());
        snapshot.insert(snapshot.end(), slowObjects.begin(), slowObjects.end());
        if (includeStatic)
            snapshot.insert(snapshot.end(), staticObjects.begin(), staticObjects.end());
        return snapshot;
    }

    void PrimitiveCalcAllPhysics()
    {
        const auto snapshot = GetPhyObjectsSnapshot();
        const std::size_t movingCount = MovingObjectCount();
        for (std::size_t i = 0; i < movingCount; ++i)
        {
            for (std::size_t j = i + 1; j < snapshot.size(); ++j)
            {
                PhyObject& first = *snapshot[i];
                PhyObject& second = *snapshot[j];
                if (const auto collision = first.CheckObjectCollided(second))
                    first.BumpWith(second, *collision);
            }
        }
    }
    void CCDCalcAllPhysics()
    {
        if (!std::isfinite(TimeDelta) || TimeDelta <= 0.f)
            return;
        const auto snapshot = GetPhyObjectsSnapshot();
        const std::size_t movingCount = MovingObjectCount();
        float remaining = TimeDelta;
        // Predict contacts using the same velocities that Move will use.
        for (std::size_t i = 0; i < movingCount; ++i)
            snapshot[i]->UpdateVelocity(TimeDelta);

        constexpr unsigned int maxContactsPerStep = 128;
        for (unsigned int contact = 0; contact < maxContactsPerStep && remaining > 0.f; ++contact)
        {
            std::pair<PhyObject*, PhyObject*> earliestPair{nullptr, nullptr};
            CollisionInfo earliestCollision{};
            float earliestTime = std::numeric_limits<float>::infinity();

            for (std::size_t i = 0; i < movingCount; ++i)
            {
                for (std::size_t j = i + 1; j < snapshot.size(); ++j)
                {
                    PhyObject& first = *snapshot[i];
                    PhyObject& second = *snapshot[j];
                    if (const auto collision = first.SweepWith(second, remaining))
                        if (collision->time < earliestTime)
                        {
                            earliestTime = collision->time;
                            earliestPair = {&first, &second};
                            earliestCollision = *collision;
                        }
                }
            }

            if (!earliestPair.first)
            {
                for (std::size_t i = 0; i < movingCount; ++i)
                    snapshot[i]->Move(remaining);
                return;
            }

            for (std::size_t i = 0; i < movingCount; ++i)
                snapshot[i]->Move(earliestTime);
            earliestPair.first->BumpWith(*earliestPair.second, earliestCollision);
            remaining = std::max(0.f, remaining - earliestTime);
            ++physicsMetrics.ccdContacts;
        }
        // Freeze the unprocessed remainder instead of tunneling through an unresolved contact.
        if (remaining > 0.f)
            ++physicsMetrics.ccdLimitHits;
    }

    void MultyRateCalcPhysics()
    {
        if (!std::isfinite(TimeDelta) || TimeDelta <= 0.f)
            return;

        const auto snapshot = GetPhyObjectsSnapshot();
        const std::size_t movingCount = MovingObjectCount();
        constexpr int substeps = 4;
        std::vector<int> intervals(movingCount);
        for (std::size_t i = 0; i < movingCount; ++i)
        {
            switch (snapshot[i]->GetSpeedState())
            {
            case SpeedState::fast: intervals[i] = 1; break;
            case SpeedState::medium: intervals[i] = 2; break;
            default: intervals[i] = 4; break;
            }
        }

        // Keep each body's schedule for this base step even if an impulse changes its group.
        // Lower-rate positions stay fixed between their updates (a discrete approximation).
        std::vector<bool> active(movingCount);
        for (int step = 0; step < substeps; ++step)
        {
            bool slowActive = false, mediumActive = false, fastActive = false;
            for (std::size_t i = 0; i < movingCount; ++i)
            {
                active[i] = (step + 1) % intervals[i] == 0;
                if (active[i])
                {
                    slowActive = slowActive || intervals[i] == 4;
                    mediumActive = mediumActive || intervals[i] == 2;
                    fastActive = fastActive || intervals[i] == 1;
                }
            }
            physicsMetrics.slowSubsteps += slowActive ? 1u : 0u;
            physicsMetrics.mediumSubsteps += mediumActive ? 1u : 0u;
            physicsMetrics.fastSubsteps += fastActive ? 1u : 0u;

            for (std::size_t i = 0; i < movingCount; ++i)
            {
                for (std::size_t j = i + 1; j < snapshot.size(); ++j)
                {
                    if (!active[i] && !(j < movingCount && active[j]))
                        continue;

                    if (const auto collision = snapshot[i]->CheckObjectCollided(*snapshot[j]))
                        snapshot[i]->BumpWith(*snapshot[j], *collision);
                }
            }

            for (std::size_t i = 0; i < movingCount; ++i)
                if (active[i])
                    snapshot[i]->Tick(TimeDelta * static_cast<float>(intervals[i]) / substeps);
        }
    }

    void BeginPhysicsFrame()
    {
        const double lastStep = physicsMetrics.lastStepMilliseconds;
        physicsMetrics = {};
        physicsMetrics.lastStepMilliseconds = lastStep;
    }

    void AdvancePhysics()
    {
        if (!std::isfinite(TimeDelta) || TimeDelta <= 0.f)
            return;
        const auto start = std::chrono::steady_clock::now();
        switch (gameState)
        {
        case GameState::Primitive:
            PrimitiveCalcAllPhysics();
            for (PhyObject* body : GetPhyObjectsSnapshot(false))
                body->Tick(TimeDelta);
            break;
        case GameState::CCD: CCDCalcAllPhysics(); break;
        case GameState::MultyRate: MultyRateCalcPhysics(); break;
        }
        physicsMetrics.lastStepMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        physicsMetrics.frameMilliseconds += physicsMetrics.lastStepMilliseconds;
        ++physicsMetrics.steps;
    }
}
