#include "Scene.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>

using State = std::vector<std::tuple<float, float, float, float, float>>;

void CheckBodies(const char* sceneName)
{
    for (const auto* body : Game::GetPhyObjectsSnapshot(false))
    {
        const auto p = body->GetPosition();
        const auto v = body->GetVelocity();
        const auto size = body->GetSize();
        if (!std::isfinite(p.first) || !std::isfinite(p.second) || !std::isfinite(v.x) || !std::isfinite(v.y))
            throw std::runtime_error(std::string(sceneName) + ": non-finite body state");
        if (p.first < -0.01f || p.second < -0.01f || p.first + size.first > 640.01f || p.second + size.second > 360.01f)
            throw std::runtime_error(std::string(sceneName) + ": a body escaped the enclosure");
    }
}

State Capture()
{
    State state;
    for (const auto* body : Game::GetPhyObjectsSnapshot())
    {
        const auto p = body->GetPosition();
        const auto v = body->GetVelocity();
        if (!std::isfinite(p.first) || !std::isfinite(p.second) || !std::isfinite(v.x) || !std::isfinite(v.y))
            throw std::runtime_error("Non-finite state during the CCD endurance test");
        state.emplace_back(p.first, p.second, v.x, v.y, body->GetMass());
    }
    return state;
}

int main()
{
    try
    {
        Scene scene(640, 360, 16, std::filesystem::current_path() / "snapshots");
        for (const char* name : {"lab02_many_ccd", "lab03_stack_ccd", "lab04_timing_ccd"})
        {
            State firstRun;
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                SceneSettings settings;
                std::string error;
                if (!scene.LoadSnapshot(name, settings, error))
                    throw std::runtime_error(std::string(name) + ": " + error);
                Game::TimeDelta = settings.timeStep;
                Game::gameState = GameState::CCD;
                Game::BeginPhysicsFrame();
                // 50 seconds of simulation, independent of render rate and playback speed.
                for (int step = 0; step < 3000; ++step)
                {
                    Game::AdvancePhysics();
                    if (Game::physicsMetrics.ccdLimitHits)
                        throw std::runtime_error(std::string(name) + ": CCD limit at step " + std::to_string(step + 1));
                    CheckBodies(name);
                }
                const auto state = Capture();
                if (repeat == 0)
                    firstRun = state;
                else if (state != firstRun)
                    throw std::runtime_error(std::string(name) + ": repeated run diverged");
                std::cout << name << ": 3000 steps, 0 LIMIT, 0 escaped; "
                          << Game::physicsMetrics.ccdContacts << " contacts; "
                          << Game::physicsMetrics.frameMilliseconds / 3000.0 << " ms/step\n";
            }
        }
        std::cout << "CCD scene regression tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
