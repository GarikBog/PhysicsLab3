#include "Object.h"
#include "Game.h"

#include <algorithm>
#include <cmath>

int Object::idCounter = 0;

Object::Object()
    : id(idCounter++), position{0.f, 0.f}, bIsVisible(true)
{
    Game::AddMeToObjectsObjects(this);
}

Object::~Object()
{
    Game::RemoveMeFromObjectsObjects(this);
}

void Object::SetPosition(float x, float y)
{
    position = {x, y};
    if (sprite)
        sprite->setPosition({static_cast<float>(x), static_cast<float>(y)});
}

std::pair<float, float> Object::GetPosition() const
{
    return position;
}

int Object::GetId() const
{
    return id;
}

void Object::Draw(sf::RenderWindow& window)
{
    if (IsVisible() && sprite)
        window.draw(*sprite);
}

void Object::SetTexture(const sf::Texture& texture, sf::IntRect rect)
{
    sprite.emplace(texture, rect);
    SetPosition(position.first, position.second);
    UpdateSpriteScale();
}

void Object::SetSize(int width, int height)
{
    size.emplace(std::max(0, width), std::max(0, height));
    UpdateSpriteScale();
}

std::pair<int, int> Object::GetSize() const
{
    if (size)
        return *size;
    const auto bounds = GetBounds();
    return {static_cast<int>(bounds.size.x), static_cast<int>(bounds.size.y)};
}

void Object::SetVisible(bool visible)
{
    bIsVisible = visible;
}

bool Object::IsVisible() const
{
    return bIsVisible;
}

sf::FloatRect Object::GetBounds() const
{
    const sf::Vector2f origin{static_cast<float>(position.first), static_cast<float>(position.second)};

    if (size)
        return {origin, {static_cast<float>(size->first), static_cast<float>(size->second)}};

    return sprite ? sprite->getGlobalBounds() : sf::FloatRect{origin, {0.f, 0.f}};
}

void Object::UpdateSpriteScale()
{
    if (!sprite || !size)
        return;

    const auto rectSize = sprite->getTextureRect().size;
    const float width = std::abs(static_cast<float>(rectSize.x));
    const float height = std::abs(static_cast<float>(rectSize.y));
    sprite->setScale({
        width > 0.f ? static_cast<float>(size->first) / width : 0.f,
        height > 0.f ? static_cast<float>(size->second) / height : 0.f
    });
}
