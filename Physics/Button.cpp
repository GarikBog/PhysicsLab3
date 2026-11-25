#include "Button.h"
#include "Game.h"

Button::Button()
{
    Game::AddMeToButtons(this);
}

Button::~Button()
{
    Game::RemoveMeFromButtons(this);
}

bool Button::Press(const sf::Vector2i& mouse_position)
{
    if (pressed || !IsVisible() || !GetBounds().contains(sf::Vector2f(mouse_position)))
        return false;

    UpdateMousePosition(mouse_position);
    pressed = true;
    OnPressed();
    return true;
}

bool Button::Release(const sf::Vector2i& mouse_position)
{
    // Release a captured button even when the cursor has left its bounds.
    if (!pressed)
        return false;

    UpdateMousePosition(mouse_position);
    pressed = false;
    OnReleased();
    return true;
}

void Button::UpdateMousePosition(const sf::Vector2i& mouse_position)
{
    mousePosition = sf::Vector2f(mouse_position);
}

sf::Vector2f Button::GetMousePosition() const
{
    return mousePosition;
}

void Button::OnPressed()
{
}

void Button::OnReleased()
{
}
