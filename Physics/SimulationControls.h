#ifndef SIMULATION_CONTROLS_H
#define SIMULATION_CONTROLS_H

#include "Button.h"

#include <array>
#include <functional>

class PanelButton : public Button
{
public:
    PanelButton(const sf::String& label, sf::FloatRect bounds, sf::Color backgroundColor,
                sf::Color textColor, std::function<void()> action = {});

    void SetLabel(const sf::String& label);
    void SetBackgroundColor(sf::Color color);
    void Draw(sf::RenderWindow& window, const sf::Font& font, bool selected = false) const;

protected:
    void OnPressed() override;

private:
    sf::String label;
    sf::Color backgroundColor;
    sf::Color textColor;
    std::function<void()> action;
};

class SimulationControls
{
public:
    enum class SnapshotAction { None, Save, Load };
    static constexpr int Height = 304;
    explicit SimulationControls(float top);

    bool HandleEvent(const sf::Event& event);
    void Draw(sf::RenderWindow& window) const;
    float GetTimeStep() const;
    float GetPlaybackSpeed() const;
    float GetSimulationDelta(float realFrameTime) const;
    bool IsPaused() const;
    void ApplySettings(float timeStep, bool paused, float playbackSpeed = 1.f);
    const sf::String& GetSnapshotName() const;
    SnapshotAction TakeSnapshotAction();
    void SetSnapshotStatus(const sf::String& status);

private:
    void UpdateSlider(float mouseX);
    void UpdatePlaybackSlider(float mouseX);
    sf::FloatRect SnapshotInputBounds() const;

    sf::Font font;
    float top;
    float dt = 1.f / 60.f;
    float playbackSpeed = 1.f;
    bool paused = false;
    bool draggingSlider = false;
    bool draggingPlaybackSlider = false;
    PanelButton pauseButton;
    PanelButton primitiveButton;
    PanelButton sweepButton;
    PanelButton multiRateButton;
    PanelButton saveButton;
    PanelButton loadButton;
    std::array<PanelButton*, 6> buttons;
    sf::String snapshotName = "scene";
    sf::String snapshotStatus = "Snapshots: snapshots/<name>.txt";
    std::size_t nameCursor = 5;
    bool nameFocused = false;
    bool nameSelected = false;
    SnapshotAction snapshotAction = SnapshotAction::None;
};

#endif
