#include "SimulationControls.h"
#include "Game.h"
#include "Scene.h"
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace
{
    constexpr float minDt = 1.f / 240.f;
    constexpr float maxDt = 1.f / 15.f;
    constexpr float sliderLeft = 220.f;
    constexpr float sliderWidth = 260.f;

    std::filesystem::path FontPath()
    {
        char* rawDirectory = nullptr;
        std::size_t length = 0;
        _dupenv_s(&rawDirectory, &length, "WINDIR");
        const std::unique_ptr<char, decltype(&std::free)> windowsDirectory(rawDirectory, &std::free);
        return std::filesystem::path(windowsDirectory ? windowsDirectory.get() : "C:/Windows") /
               "Fonts" / "segoeui.ttf";
    }
}

PanelButton::PanelButton(const sf::String& buttonLabel, sf::FloatRect bounds, sf::Color background,
                         sf::Color foreground, std::function<void()> onPress)
    : label(buttonLabel), backgroundColor(background), textColor(foreground), action(std::move(onPress))
{
    SetPosition(bounds.position.x, bounds.position.y);
    SetSize(static_cast<int>(bounds.size.x), static_cast<int>(bounds.size.y));
}

void PanelButton::SetLabel(const sf::String& buttonLabel)
{
    label = buttonLabel;
}

void PanelButton::SetBackgroundColor(sf::Color color)
{
    backgroundColor = color;
}

void PanelButton::OnPressed()
{
    if (action)
        action();
}

void PanelButton::Draw(sf::RenderWindow& window, const sf::Font& buttonFont, bool selected) const
{
    if (!IsVisible())
        return;

    const auto bounds = GetBounds();
    sf::RectangleShape shape(bounds.size);
    shape.setPosition(bounds.position);
    shape.setFillColor(selected ? sf::Color(42, 89, 130) : backgroundColor);
    if (selected)
    {
        shape.setOutlineColor(sf::Color(70, 180, 240));
        shape.setOutlineThickness(-2.f);
    }
    window.draw(shape);

    sf::Text caption(buttonFont, label, 15);
    const auto local = caption.getLocalBounds();
    caption.setOrigin(local.position + local.size / 2.f);
    caption.setPosition(bounds.position + bounds.size / 2.f);
    caption.setFillColor(selected ? sf::Color(225, 232, 244) : textColor);
    window.draw(caption);
}

SimulationControls::SimulationControls(float panelTop)
    : font(FontPath()), top(panelTop),
      pauseButton("Pause", {{512.f, panelTop + 12.f}, {112.f, 36.f}},
                  sf::Color(58, 77, 103), sf::Color(225, 232, 244), [this]
                  {
                      paused = !paused;
                      pauseButton.SetLabel(paused ? "Resume" : "Pause");
                      pauseButton.SetBackgroundColor(paused ? sf::Color(45, 100, 90) : sf::Color(58, 77, 103));
                  }),
      primitiveButton("Primitive", {{104.f, panelTop + 120.f}, {160.f, 36.f}},
                      sf::Color(45, 57, 76), sf::Color(155, 170, 190),
                      [] { Game::gameState = GameState::Primitive; }),
      sweepButton("Sweep (CCD)", {{280.f, panelTop + 120.f}, {160.f, 36.f}},
                  sf::Color(45, 57, 76), sf::Color(155, 170, 190),
                  [] { Game::gameState = GameState::CCD; }),
      multiRateButton("Multi-rate", {{456.f, panelTop + 120.f}, {168.f, 36.f}},
                      sf::Color(45, 57, 76), sf::Color(155, 170, 190),
                      [] { Game::gameState = GameState::MultyRate; }),
      saveButton("Save", {{384.f, panelTop + 172.f}, {112.f, 36.f}},
                 sf::Color(58, 77, 103), sf::Color(225, 232, 244),
                 [this] { snapshotAction = SnapshotAction::Save; }),
      loadButton("Load", {{512.f, panelTop + 172.f}, {112.f, 36.f}},
                 sf::Color(58, 77, 103), sf::Color(225, 232, 244),
                 [this] { snapshotAction = SnapshotAction::Load; }),
      buttons{&pauseButton, &primitiveButton, &sweepButton, &multiRateButton, &saveButton, &loadButton}
{
}

float SimulationControls::GetTimeStep() const
{
    return dt;
}

bool SimulationControls::IsPaused() const
{
    return paused;
}

float SimulationControls::GetPlaybackSpeed() const
{
    return playbackSpeed;
}

float SimulationControls::GetSimulationDelta(float realFrameTime) const
{
    if (paused || !std::isfinite(realFrameTime))
        return 0.f;
    // Playback changes elapsed simulation time, never the fixed physics step or body velocities.
    return std::clamp(realFrameTime, 0.f, 0.25f) * playbackSpeed;
}

void SimulationControls::ApplySettings(float timeStep, bool isPaused, float speed)
{
    dt = timeStep;
    paused = isPaused;
    playbackSpeed = std::clamp(speed, SceneSettings::MinPlaybackSpeed, SceneSettings::MaxPlaybackSpeed);
    draggingSlider = false;
    draggingPlaybackSlider = false;
    pauseButton.SetLabel(paused ? "Resume" : "Pause");
    pauseButton.SetBackgroundColor(paused ? sf::Color(45, 100, 90) : sf::Color(58, 77, 103));
}

const sf::String& SimulationControls::GetSnapshotName() const { return snapshotName; }

SimulationControls::SnapshotAction SimulationControls::TakeSnapshotAction()
{
    const auto action = snapshotAction;
    snapshotAction = SnapshotAction::None;
    return action;
}

void SimulationControls::SetSnapshotStatus(const sf::String& status) { snapshotStatus = status; }

sf::FloatRect SimulationControls::SnapshotInputBounds() const
{
    return {{104.f, top + 172.f}, {260.f, 36.f}};
}

void SimulationControls::UpdateSlider(float mouseX)
{
    const float fraction = std::clamp((mouseX - sliderLeft) / sliderWidth, 0.f, 1.f);
    const float frequency = 1.f / maxDt + fraction * (1.f / minDt - 1.f / maxDt);
    dt = 1.f / frequency;
}

void SimulationControls::UpdatePlaybackSlider(float mouseX)
{
    const float fraction = std::clamp((mouseX - sliderLeft) / sliderWidth, 0.f, 1.f);
    const float ratio = SceneSettings::MaxPlaybackSpeed / SceneSettings::MinPlaybackSpeed;
    playbackSpeed = std::clamp(std::round(SceneSettings::MinPlaybackSpeed * std::pow(ratio, fraction) * 100.f) / 100.f,
                               SceneSettings::MinPlaybackSpeed, SceneSettings::MaxPlaybackSpeed);
    if (std::abs(playbackSpeed - 1.f) < 0.03f)
        playbackSpeed = 1.f;
}

bool SimulationControls::HandleEvent(const sf::Event& event)
{
    if (event.is<sf::Event::FocusLost>())
    {
        draggingSlider = false;
        draggingPlaybackSlider = false;
        nameFocused = false;
        nameSelected = false;
        for (Button* button : buttons)
            button->Release({0, 0});
    }

    if (const auto* entered = event.getIf<sf::Event::TextEntered>())
    {
        if (nameFocused)
        {
            const char32_t character = entered->unicode;
            if (character >= U' ' && character != 127 &&
                sf::String(U"<>:\"/\\|?*").find(sf::String(character)) == sf::String::InvalidPos &&
                (snapshotName.getSize() < 80 || nameSelected))
            {
                if (nameSelected)
                {
                    snapshotName.clear();
                    nameCursor = 0;
                }
                snapshotName.insert(nameCursor++, sf::String(character));
                nameSelected = false;
            }
            return true;
        }
    }
    if (const auto* key = event.getIf<sf::Event::KeyPressed>())
    {
        if (nameFocused)
        {
            if (key->control && key->scancode == sf::Keyboard::Scancode::A)
                nameSelected = true;
            else if (key->control && key->scancode == sf::Keyboard::Scancode::V)
            {
                for (const char32_t character : sf::Clipboard::getString())
                    (void)HandleEvent(sf::Event::TextEntered{character});
            }
            else if (key->scancode == sf::Keyboard::Scancode::Backspace ||
                     key->scancode == sf::Keyboard::Scancode::Delete)
            {
                if (nameSelected)
                {
                    snapshotName.clear();
                    nameCursor = 0;
                }
                else if (key->scancode == sf::Keyboard::Scancode::Backspace && nameCursor > 0)
                    snapshotName.erase(--nameCursor, 1);
                else if (key->scancode == sf::Keyboard::Scancode::Delete && nameCursor < snapshotName.getSize())
                    snapshotName.erase(nameCursor, 1);
                nameSelected = false;
            }
            else if (key->scancode == sf::Keyboard::Scancode::Left)
            {
                if (nameCursor > 0) --nameCursor;
                nameSelected = false;
            }
            else if (key->scancode == sf::Keyboard::Scancode::Right)
            {
                if (nameCursor < snapshotName.getSize()) ++nameCursor;
                nameSelected = false;
            }
            else if (key->scancode == sf::Keyboard::Scancode::Home)
            {
                nameCursor = 0;
                nameSelected = false;
            }
            else if (key->scancode == sf::Keyboard::Scancode::End)
            {
                nameCursor = snapshotName.getSize();
                nameSelected = false;
            }
            else if (key->scancode == sf::Keyboard::Scancode::Enter || key->scancode == sf::Keyboard::Scancode::Escape)
            {
                nameFocused = false;
                nameSelected = false;
            }
            // Typing A/W in the name must never spawn scene objects.
            return true;
        }
    }

    if (const auto* press = event.getIf<sf::Event::MouseButtonPressed>())
    {
        const sf::Vector2f position(press->position);
        if (press->button == sf::Mouse::Button::Left)
        {
            nameFocused = SnapshotInputBounds().contains(position);
            nameSelected = false;
            if (nameFocused)
            {
                nameCursor = snapshotName.getSize();
                return true;
            }
            if (sf::FloatRect({sliderLeft - 10.f, top + 14.f}, {sliderWidth + 20.f, 30.f}).contains(position))
            {
                draggingSlider = true;
                UpdateSlider(position.x);
            }
            else if (sf::FloatRect({sliderLeft - 10.f, top + 66.f}, {sliderWidth + 20.f, 30.f}).contains(position))
            {
                draggingPlaybackSlider = true;
                UpdatePlaybackSlider(position.x);
            }
            else
            {
                for (Button* button : buttons)
                    if (button->Press(press->position))
                        break;
            }
        }
        // The entire panel consumes mouse presses.
        return position.y >= top;
    }
    if (const auto* move = event.getIf<sf::Event::MouseMoved>())
    {
        if (draggingSlider)
        {
            UpdateSlider(static_cast<float>(move->position.x));
            return true;
        }
        if (draggingPlaybackSlider)
        {
            UpdatePlaybackSlider(static_cast<float>(move->position.x));
            return true;
        }
        return static_cast<float>(move->position.y) >= top;
    }
    if (const auto* release = event.getIf<sf::Event::MouseButtonReleased>())
    {
        if (release->button == sf::Mouse::Button::Left)
        {
            for (Button* button : buttons)
                button->Release(release->position);
        }
        if (release->button == sf::Mouse::Button::Left && draggingSlider)
        {
            UpdateSlider(static_cast<float>(release->position.x));
            draggingSlider = false;
            return true;
        }
        if (release->button == sf::Mouse::Button::Left && draggingPlaybackSlider)
        {
            UpdatePlaybackSlider(static_cast<float>(release->position.x));
            draggingPlaybackSlider = false;
            return true;
        }
        return static_cast<float>(release->position.y) >= top;
    }
    return false;
}

void SimulationControls::Draw(sf::RenderWindow& window) const
{
    const auto rectangle = [&](sf::Vector2f position, sf::Vector2f size, sf::Color color)
    {
        sf::RectangleShape shape(size);
        shape.setPosition(position);
        shape.setFillColor(color);
        window.draw(shape);
    };
    const auto text = [&](const sf::String& label, sf::Vector2f position, unsigned int size, sf::Color color)
    {
        sf::Text caption(font, label, size);
        caption.setPosition(position);
        caption.setFillColor(color);
        window.draw(caption);
    };
    const sf::Color foreground(225, 232, 244);
    const sf::Color muted(155, 170, 190);

    rectangle({0.f, top}, {640.f, static_cast<float>(Height)}, sf::Color(31, 39, 54));
    std::ostringstream timeStep;
    timeStep << "Physics: " << std::fixed << std::setprecision(0) << 1.f / dt << " Hz";
    text(timeStep.str(), {16.f, top + 17.f}, 15, foreground);
    std::ostringstream stepSize;
    stepSize << "dt: " << std::fixed << std::setprecision(2) << dt * 1000.f << " ms";
    text(stepSize.str(), {16.f, top + 40.f}, 12, muted);

    rectangle({sliderLeft, top + 27.f}, {sliderWidth, 4.f}, sf::Color(78, 92, 114));
    const float fraction = std::clamp((1.f / dt - 1.f / maxDt) / (1.f / minDt - 1.f / maxDt), 0.f, 1.f);
    rectangle({sliderLeft, top + 27.f}, {sliderWidth * fraction, 4.f}, sf::Color(70, 180, 240));
    sf::CircleShape knob(7.f);
    knob.setOrigin({7.f, 7.f});
    knob.setPosition({sliderLeft + sliderWidth * fraction, top + 29.f});
    knob.setFillColor(foreground);
    window.draw(knob);
    text("15 Hz", {sliderLeft, top + 40.f}, 12, muted);
    text("240 Hz", {sliderLeft + sliderWidth - 42.f, top + 40.f}, 12, muted);

    std::ostringstream playback;
    playback << "Playback: " << std::fixed << std::setprecision(2) << playbackSpeed << 'x';
    text(playback.str(), {16.f, top + 69.f}, 15, foreground);
    rectangle({sliderLeft, top + 79.f}, {sliderWidth, 4.f}, sf::Color(78, 92, 114));
    const float speedFraction = std::clamp(std::log(playbackSpeed / SceneSettings::MinPlaybackSpeed) /
        std::log(SceneSettings::MaxPlaybackSpeed / SceneSettings::MinPlaybackSpeed), 0.f, 1.f);
    rectangle({sliderLeft, top + 79.f}, {sliderWidth * speedFraction, 4.f}, sf::Color(70, 180, 240));
    knob.setPosition({sliderLeft + sliderWidth * speedFraction, top + 81.f});
    window.draw(knob);
    const float normalSpeedX = sliderLeft + sliderWidth * std::log(1.f / SceneSettings::MinPlaybackSpeed) /
        std::log(SceneSettings::MaxPlaybackSpeed / SceneSettings::MinPlaybackSpeed);
    text("0.1x", {sliderLeft, top + 92.f}, 12, muted);
    text("1x", {normalSpeedX - 6.f, top + 92.f}, 12, muted);
    text("4x", {sliderLeft + sliderWidth - 14.f, top + 92.f}, 12, muted);
    text("Mode", {16.f, top + 127.f}, 15, foreground);
    pauseButton.Draw(window, font);
    primitiveButton.Draw(window, font, Game::gameState == GameState::Primitive);
    sweepButton.Draw(window, font, Game::gameState == GameState::CCD);
    multiRateButton.Draw(window, font, Game::gameState == GameState::MultyRate);

    text("Snapshot", {16.f, top + 181.f}, 15, foreground);
    const auto inputBounds = SnapshotInputBounds();
    sf::RectangleShape input(inputBounds.size);
    input.setPosition(inputBounds.position);
    input.setFillColor(sf::Color(23, 29, 41));
    input.setOutlineColor(nameFocused ? sf::Color(70, 180, 240) : sf::Color(78, 92, 114));
    input.setOutlineThickness(-1.f);
    window.draw(input);
    // Keep the caret and surrounding Unicode text inside the input rectangle.
    sf::Text inputText(font, "", 15);
    const float availableWidth = inputBounds.size.x - 20.f;
    std::size_t firstCharacter = 0;
    while (firstCharacter < nameCursor)
    {
        inputText.setString(snapshotName.substring(firstCharacter, nameCursor - firstCharacter));
        if (inputText.getLocalBounds().size.x <= availableWidth) break;
        ++firstCharacter;
    }
    std::size_t lastCharacter = nameCursor;
    while (lastCharacter < snapshotName.getSize())
    {
        inputText.setString(snapshotName.substring(firstCharacter, lastCharacter - firstCharacter + 1));
        if (inputText.getLocalBounds().size.x > availableWidth) break;
        ++lastCharacter;
    }
    inputText.setString(snapshotName.substring(firstCharacter, lastCharacter - firstCharacter));
    inputText.setPosition(inputBounds.position + sf::Vector2f{9.f, 8.f});
    inputText.setFillColor(foreground);
    if (nameFocused && nameSelected)
        rectangle(inputBounds.position + sf::Vector2f{7.f, 5.f},
                  {inputText.getLocalBounds().size.x + 5.f, 26.f}, sf::Color(42, 89, 130));
    window.draw(inputText);
    if (nameFocused)
    {
        float caretX = 0.f;
        for (const auto& glyph : inputText.getShapedGlyphs())
        {
            if (glyph.cluster >= nameCursor - firstCharacter)
            {
                caretX = glyph.position.x;
                break;
            }
            caretX = std::max(caretX, glyph.position.x + glyph.glyph.advance);
        }
        rectangle(inputText.getPosition() + sf::Vector2f{std::min(caretX, availableWidth), 1.f},
                  {1.f, 19.f}, foreground);
    }
    saveButton.Draw(window, font);
    loadButton.Draw(window, font);

    sf::Text status(font, snapshotStatus, 12);
    sf::String clippedStatus = snapshotStatus;
    while (status.getLocalBounds().size.x > 608.f && !clippedStatus.isEmpty())
    {
        clippedStatus.erase(clippedStatus.getSize() - 1, 1);
        status.setString(clippedStatus + "...");
    }
    status.setPosition({16.f, top + 217.f});
    status.setFillColor(muted);
    window.draw(status);
    text("A: square   W: start/finish wall   Esc: cancel wall", {16.f, top + 237.f}, 12, muted);

    const auto& metrics = Game::physicsMetrics;
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(3) << "Physics: " << metrics.frameMilliseconds
           << " ms/frame   Step: " << metrics.lastStepMilliseconds << " ms   Steps/frame: " << metrics.steps;
    text(timing.str(), {16.f, top + 259.f}, 12, foreground);
    std::ostringstream substeps;
    substeps << "Multi substeps/frame: slow " << metrics.slowSubsteps << "  medium " << metrics.mediumSubsteps
             << "  fast " << metrics.fastSubsteps << "    CCD contacts: " << metrics.ccdContacts;
    if (metrics.ccdLimitHits) substeps << "  LIMIT";
    text(substeps.str(), {16.f, top + 279.f}, 12, foreground);
}
