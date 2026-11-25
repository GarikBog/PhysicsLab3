#include "Scene.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <tuple>

using State = std::vector<std::tuple<float, float, float, float>>;

void Load(Scene& scene, const char* name)
{
    SceneSettings settings;
    std::string error;
    if (!scene.LoadSnapshot(name, settings, error)) throw std::runtime_error(error);
    Game::TimeDelta = settings.timeStep;
    Game::gameState = settings.mode;
    Game::physicsMetrics = {};
}

int main()
{
    try
    {
        std::filesystem::create_directories("output/report-data");
        std::ofstream file("output/report-data/validation.json");
        file << std::setprecision(12) << "{\n\"projectile\":[\n";
        Scene scene(640, 360, 16, std::filesystem::current_path() / "snapshots");
        const char* simple[] = {"lab01_primitive60", "lab01_ccd60", "lab01_multirate60", "lab01_primitive240"};
        for (int i = 0; i < 4; ++i)
        {
            Load(scene, simple[i]);
            auto* body = Game::GetPhyObjectsSnapshot(false).front();
            float hitTime = -1.f;
            for (const auto* wall : Game::staticObjects)
                if (wall->GetSize() == std::pair{10, 232})
                    if (auto hit = body->SweepWith(*wall, 1.f)) hitTime = hit->time;
            for (int step = 0; step < (i == 3 ? 32 : 8); ++step) Game::AdvancePhysics();
            file << (i ? ",\n" : "") << "{\"scene\":\"" << simple[i] << "\",\"x\":"
                 << body->GetPosition().first << ",\"vx\":" << body->GetVelocity().x
                 << ",\"sweep_hit_seconds\":" << hitTime << ",\"contacts\":"
                 << Game::physicsMetrics.ccdContacts << "}";
        }
        file << "\n],\n\"crowded\":[\n";
        bool first = true;
        for (const char* name : {"lab02_many_primitive", "lab02_many_ccd", "lab02_many_multirate",
                                 "lab03_stack_primitive", "lab03_stack_ccd", "lab03_stack_multirate"})
        {
            State reference;
            bool same = true;
            float maxPenetration = 0.f;
            std::size_t escapedCount = 0;
            unsigned int maxLimits = 0;
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                Load(scene, name);
                const auto bodies = Game::GetPhyObjectsSnapshot();
                std::vector<bool> escaped(bodies.size(), false);
                for (int step = 0; step < 3000; ++step)
                {
                    Game::AdvancePhysics();
                    for (std::size_t a = 0; a < bodies.size(); ++a)
                    {
                        const auto p = bodies[a]->GetPosition();
                        const auto v = bodies[a]->GetVelocity();
                        const auto size = bodies[a]->GetSize();
                        if (!std::isfinite(p.first) || !std::isfinite(p.second) || !std::isfinite(v.x) || !std::isfinite(v.y))
                            throw std::runtime_error("Nonfinite state");
                        if (bodies[a]->GetMass() > 0.f &&
                            (p.first + size.first < -0.01f || p.second + size.second < -0.01f ||
                             p.first > 640.01f || p.second > 360.01f)) escaped[a] = true;
                        for (std::size_t b = a + 1; b < bodies.size(); ++b)
                            if (bodies[a]->GetMass() > 0.f || bodies[b]->GetMass() > 0.f)
                                if (const auto collision = bodies[a]->CheckObjectCollided(*bodies[b]))
                                    maxPenetration = std::max(maxPenetration, collision->penetration);
                    }
                }
                State state;
                for (const auto* body : bodies)
                {
                    auto p = body->GetPosition(); auto v = body->GetVelocity();
                    state.emplace_back(p.first, p.second, v.x, v.y);
                }
                if (repeat == 0) reference = state;
                else same = state == reference;
                escapedCount = std::max(escapedCount, static_cast<std::size_t>(std::count(escaped.begin(), escaped.end(), true)));
                maxLimits = std::max(maxLimits, Game::physicsMetrics.ccdLimitHits);
            }
            file << (first ? "" : ",\n") << "{\"scene\":\"" << name << "\",\"steps\":3000,\"repeats\":2,\"repeatable\":"
                 << (same ? "true" : "false") << ",\"max_penetration_px\":" << maxPenetration
                 << ",\"fully_escaped_bodies\":" << escapedCount << ",\"ccd_limit_hits\":" << maxLimits << "}";
            std::cout << name << ": maxPen=" << maxPenetration << ", escaped=" << escapedCount << ", limits=" << maxLimits << ", same=" << same << '\n';
            first = false;
        }
        file << "\n]}\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what(); return 1; }
}
