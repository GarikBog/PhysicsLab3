#include "Scene.h"
#include "SimulationControls.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

void Expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

void TestSceneSnapshots()
{
    const auto directory = std::filesystem::current_path() / "build" / "scene-tests";
    Scene scene(640, 360, 16, directory);
    Expect(Game::staticObjects.size() == 4, "New scene must own four boundary walls");
    scene.ToggleWall({100, 100});
    scene.UpdateWall({20, 50});
    Expect(scene.IsBuildingWall() && Game::staticObjects.size() == 4,
           "Wall preview must not affect physics before confirmation");
    scene.ToggleWall({20, 50});
    PhyObject* wall = Game::staticObjects.back();
    Expect(!scene.IsBuildingWall() && Game::staticObjects.size() == 5 &&
           wall->GetPosition() == std::pair<float, float>{20.f, 50.f} &&
           wall->GetSize() == std::pair<int, int>{80, 50},
           "Wall must normalize both corners, including reverse drags");
    scene.ToggleWall({200, 150});
    scene.CancelWall();
    Expect(Game::staticObjects.size() == 5, "Cancel must not create a physical wall");
    scene.ToggleWall({200, 500});
    Expect(!scene.IsBuildingWall(), "Wall creation must ignore the control panel");

    scene.AddSquare({200, 200}, 32);
    PhyObject* body = Game::slowObjects.back();
    body->SetMass(2.5f);
    body->SetVelocity({1200.f, -40.f});
    body->SetForce({10.f, -20.f});
    body->SetVisible(false);
    const SceneSettings saved{1.f / 120.f, GameState::CCD, true, 0.5f};
    std::string error;
    Expect(scene.SaveSnapshot("roundtrip", saved, error), "Saving a scene must succeed");
    Expect(scene.SaveSnapshot("roundtrip", saved, error), "Saving again must replace the existing file");
    const std::string original = ReadFile(directory / "roundtrip.txt");
    Expect(original.find("velocity 1200 -40") != std::string::npos,
           "Snapshot must expose editable, labeled velocity fields");

    body->SetVelocity({0.f, 0.f});
    body->SetPosition(300.f, 250.f);
    scene.AddSquare({400, 200}, 32);
    SceneSettings loaded;
    Expect(scene.LoadSnapshot("roundtrip", loaded, error), "Loading a saved scene must succeed");
    Expect(loaded.timeStep == saved.timeStep && loaded.mode == saved.mode && loaded.paused &&
           loaded.playbackSpeed == saved.playbackSpeed,
           "Snapshot must restore time step, mode, pause and playback speed");
    Expect(Game::GetPhyObjectsSnapshot().size() == 6 && Game::staticObjects.size() == 5 &&
           Game::fastObjects.size() == 1, "Loading must replace all old bodies and classify restored velocities");
    body = Game::fastObjects.front();
    Expect(body->GetPosition() == std::pair<float, float>{184.f, 184.f} &&
           body->GetSize() == std::pair<int, int>{32, 32} && body->GetMass() == 2.5f &&
           body->GetVelocity() == sf::Vector2f{1200.f, -40.f} &&
           body->GetForce() == sf::Vector2f{10.f, -20.f} && !body->IsVisible(),
           "All physical fields and visibility must survive a round trip");
    Expect(scene.SaveSnapshot("after-load", loaded, error) && ReadFile(directory / "after-load.txt") == original,
           "Snapshot round trip must preserve object/group ordering and float values exactly");

    const auto before = Game::GetPhyObjectsSnapshot();
    scene.ToggleWall({250, 150});
    Expect(!scene.LoadSnapshot("missing-file-never-created", loaded, error) && error.empty() &&
           Game::GetPhyObjectsSnapshot() == before && scene.IsBuildingWall(),
           "Missing snapshot must leave the scene, settings and wall preview unchanged");
    scene.CancelWall();

    for (const auto& replacement : {std::pair{"velocity 1200 -40", "velocity nan -40"},
                                    std::pair{"mass 2.5", "mass -1"},
                                    std::pair{"size 32 32", "size 0 32"},
                                    std::pair{"playback_speed 0.5", "playback_speed 0"},
                                    std::pair{"playback_speed 0.5", "playback_speed 1000000"}})
    {
        std::string invalid = original;
        invalid.replace(invalid.find(replacement.first), std::string(replacement.first).size(), replacement.second);
        WriteFile(directory / "invalid.txt", invalid);
        Expect(!scene.LoadSnapshot("invalid", loaded, error) && !error.empty() &&
               Game::GetPhyObjectsSnapshot() == before,
               "Malformed files must never partially destroy or replace the scene");
    }
    std::string edited = original;
    edited.replace(edited.find("velocity 1200 -40"), std::string("velocity 1200 -40").size(), "velocity -300 0");
    WriteFile(directory / "edited.txt", edited);
    Expect(scene.LoadSnapshot("edited", loaded, error) && Game::mediumObjects.size() == 1 &&
           Game::mediumObjects.front()->GetVelocity() == sf::Vector2f{-300.f, 0.f},
           "Hand-edited velocity must restore its direction and move the body into the proper speed group");
    WriteFile(directory / "notepad.txt", std::string("\xEF\xBB\xBF") + original);
    Expect(scene.LoadSnapshot("notepad", loaded, error), "UTF-8 BOM from Notepad must be accepted");
    std::string legacy = original;
    const auto speedLine = legacy.find("playback_speed ");
    legacy.erase(speedLine, legacy.find('\n', speedLine) - speedLine + 1);
    WriteFile(directory / "legacy.txt", legacy);
    Expect(scene.LoadSnapshot("legacy", loaded, error) && loaded.playbackSpeed == 1.f,
           "Old snapshots without playback_speed must load at normal speed");
    const sf::String unicodeName(U"\u0421\u0446\u0435\u043d\u0430");
    Expect(scene.SaveSnapshot(unicodeName, saved, error) && scene.LoadSnapshot(unicodeName, loaded, error),
           "Unicode snapshot filenames must work");
    Expect(!scene.SaveSnapshot("../outside", saved, error), "A snapshot name must stay a filename");
}

void TestSnapshotControls()
{
    SimulationControls controls(360.f);
    Expect(Game::buttons.size() == 6, "Save and Load must use the existing Button registration");
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {150, 547}});
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {150, 547}});
    sf::Event::KeyPressed selectAll;
    selectAll.scancode = sf::Keyboard::Scancode::A;
    selectAll.control = true;
    Expect(controls.HandleEvent(selectAll), "Focused input must consume A instead of spawning a square");
    controls.HandleEvent(sf::Event::TextEntered{U'W'});
    controls.HandleEvent(sf::Event::TextEntered{U'\u044f'});
    Expect(controls.GetSnapshotName() == sf::String(U"W\u044f"), "Input must accept Unicode and replace selection");
    sf::Event::KeyPressed left;
    left.scancode = sf::Keyboard::Scancode::Left;
    controls.HandleEvent(left);
    controls.HandleEvent(sf::Event::TextEntered{U'a'});
    Expect(controls.GetSnapshotName() == sf::String(U"Wa\u044f"), "Input must insert at the caret");
    sf::Event::KeyPressed backspace;
    backspace.scancode = sf::Keyboard::Scancode::Backspace;
    controls.HandleEvent(backspace);
    Expect(controls.GetSnapshotName() == sf::String(U"W\u044f"), "Backspace must edit one Unicode codepoint");
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {440, 547}});
    Expect(controls.TakeSnapshotAction() == SimulationControls::SnapshotAction::Save &&
           controls.TakeSnapshotAction() == SimulationControls::SnapshotAction::None, "Save must enqueue one action");
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {440, 547}});
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {550, 547}});
    Expect(controls.TakeSnapshotAction() == SimulationControls::SnapshotAction::Load, "Load must enqueue its action");
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {550, 547}});
    controls.ApplySettings(1.f / 1000.f, true);
    Expect(controls.IsPaused() && controls.GetTimeStep() == 1.f / 1000.f,
           "Edited positive time steps outside the slider range must still restore exactly");
}

void TestIndependentPlaybackAndFrequency()
{
    SimulationControls controls(360.f);
    const float originalDt = controls.GetTimeStep();
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {220, 441}});
    Expect(controls.GetPlaybackSpeed() == 0.1f && controls.GetTimeStep() == originalDt,
           "Slow playback must not change physics frequency");
    controls.HandleEvent(sf::Event::MouseMoved{{480, 441}});
    Expect(controls.GetPlaybackSpeed() == 4.f && controls.GetTimeStep() == originalDt,
           "Fast playback must not change physics frequency");
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {480, 441}});
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {220, 389}});
    Expect(controls.GetTimeStep() == 1.f / 15.f && controls.GetPlaybackSpeed() == 4.f,
           "Physics frequency slider must be independent from playback speed");
    controls.HandleEvent(sf::Event::MouseMoved{{480, 389}});
    Expect(controls.GetTimeStep() == 1.f / 240.f, "Frequency slider must increase from 15 to 240 Hz");
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {480, 389}});
    controls.HandleEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {382, 441}});
    controls.HandleEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {382, 441}});
    Expect(controls.GetPlaybackSpeed() == 1.f, "Normal-speed tick must snap to exactly 1x");

    PhyObject body;
    body.SetVelocity({64.f, 0.f});
    Game::gameState = GameState::Primitive;
    for (float speed : {0.5f, 2.f})
    {
        body.SetPosition(0.f, 0.f);
        controls.ApplySettings(1.f / 64.f, false, speed);
        Game::TimeDelta = controls.GetTimeStep();
        double accumulator = 0.0;
        Game::BeginPhysicsFrame();
        for (int frame = 0; frame < 2; ++frame)
        {
            accumulator += controls.GetSimulationDelta(0.125f);
            while (accumulator >= controls.GetTimeStep())
            {
                Game::AdvancePhysics();
                accumulator -= controls.GetTimeStep();
            }
        }
        Expect(body.GetPosition().first == 16.f * speed && body.GetVelocity().x == 64.f &&
               Game::TimeDelta == 1.f / 64.f,
               "Playback must scale the number of fixed steps, preserving their dt and the body velocity");
    }
    controls.ApplySettings(1.f / 60.f, true, 4.f);
    Expect(controls.GetSimulationDelta(0.125f) == 0.f, "Pause must stop simulation at any playback speed");
    controls.ApplySettings(1.f / 60.f, false, 4.f);
    Expect(controls.GetSimulationDelta(10.f) == 1.f, "Long-frame catch-up must remain bounded before scaling");
}

int main()
{
    try
    {
        TestSceneSnapshots();
        Expect(Game::GetPhyObjectsSnapshot().empty(), "Scene destruction must unregister every body");
        TestSnapshotControls();
        TestIndependentPlaybackAndFrequency();
        Expect(Game::objects.empty() && Game::buttons.empty(), "Control destruction must unregister every button");
        std::cout << "All scene and control tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
