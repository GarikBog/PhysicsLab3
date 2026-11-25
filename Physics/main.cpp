#include <SFML/Graphics.hpp>
#include "Game.h"
#include "Scene.h"
#include "SimulationControls.h"
#include "Benchmark.h"
#include <algorithm>

int main(int argc, char* argv[])
{
    if (argc > 1)
        return RunPhysicsBenchmarks(argc, argv);

    constexpr int windowWidth = 640;
    constexpr int windowHeight = 360;
    constexpr int wallThickness = 16;
    constexpr int squareSize = 32;
    sf::RenderWindow window(sf::VideoMode({windowWidth, windowHeight + SimulationControls::Height}),
                            "PhysicsLab3 | A: square | W: wall", sf::Style::Titlebar | sf::Style::Close);
    window.setKeyRepeatEnabled(false);
    window.setFramerateLimit(120);
    Scene scene(windowWidth, windowHeight, wallThickness, Scene::DefaultSnapshotDirectory());
    SimulationControls controls(static_cast<float>(windowHeight));
    sf::Clock clock;
    double accumulator = 0.0;

    while (window.isOpen())
    {
        Game::BeginPhysicsFrame();
        const float frameTime = clock.restart().asSeconds();
        accumulator = controls.IsPaused() ? 0.0 : accumulator + controls.GetSimulationDelta(frameTime);
        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();
            const bool wasPaused = controls.IsPaused();
            const float previousDt = controls.GetTimeStep();
            const bool controlsHandled = controls.HandleEvent(*event);
            if (wasPaused != controls.IsPaused() || previousDt != controls.GetTimeStep())
                accumulator = 0.0;

            const auto snapshotAction = controls.TakeSnapshotAction();
            if (snapshotAction != SimulationControls::SnapshotAction::None)
            {
                std::string error;
                SceneSettings settings{controls.GetTimeStep(), Game::gameState, controls.IsPaused(), controls.GetPlaybackSpeed()};
                if (snapshotAction == SimulationControls::SnapshotAction::Save)
                {
                    if (scene.SaveSnapshot(controls.GetSnapshotName(), settings, error))
                        controls.SetSnapshotStatus("Snapshot saved: " + controls.GetSnapshotName());
                }
                else if (scene.LoadSnapshot(controls.GetSnapshotName(), settings, error))
                {
                    controls.ApplySettings(settings.timeStep, settings.paused, settings.playbackSpeed);
                    Game::TimeDelta = settings.timeStep;
                    Game::gameState = settings.mode;
                    Game::physicsMetrics = {};
                    accumulator = 0.0;
                    (void)clock.restart();
                    controls.SetSnapshotStatus("Snapshot loaded: " + controls.GetSnapshotName());
                }
                if (!error.empty())
                    controls.SetSnapshotStatus(sf::String::fromUtf8(error.begin(), error.end()));
            }

            // Releases must reach the captured body even when the mouse is over the panel.
            if (controlsHandled && !event->is<sf::Event::MouseButtonReleased>())
                continue;
            if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                const auto mouse = sf::Mouse::getPosition(window);
                if (key->scancode == sf::Keyboard::Scancode::A)
                    scene.AddSquare(mouse, squareSize);
                else if (key->scancode == sf::Keyboard::Scancode::W)
                    scene.ToggleWall(mouse);
                else if (key->scancode == sf::Keyboard::Scancode::Escape)
                    scene.CancelWall();
            }
            if (const auto* press = event->getIf<sf::Event::MouseButtonPressed>())
            {
                if (press->button == sf::Mouse::Button::Left && !controls.IsPaused() && !scene.IsBuildingWall())
                {
                    for (auto it = Game::buttons.rbegin(); it != Game::buttons.rend(); ++it)
                        if ((*it)->Press(press->position))
                            break;
                }
            }
            if (const auto* release = event->getIf<sf::Event::MouseButtonReleased>())
            {
                if (release->button == sf::Mouse::Button::Left)
                    for (Button* button : Game::buttons)
                        button->Release(release->position);
            }
            if (event->is<sf::Event::FocusLost>())
            {
                for (Button* button : Game::buttons)
                    button->Release(sf::Mouse::getPosition(window));
            }
        }

        if (window.isOpen())
        {
            scene.UpdateWall(sf::Mouse::getPosition(window));
            Game::TimeDelta = controls.GetTimeStep();
            while (!controls.IsPaused() && accumulator >= Game::TimeDelta)
            {
                for (Button* button : Game::buttons)
                    button->UpdateMousePosition(sf::Mouse::getPosition(window));
                Game::AdvancePhysics();
                accumulator -= Game::TimeDelta;
            }

            window.clear(sf::Color(23, 29, 41));
            for (Object* object : Game::objects)
                object->Draw(window);
            scene.DrawWallPreview(window);
            controls.Draw(window);
            window.display();
        }
    }
}
