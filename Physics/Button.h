#ifndef BUTTON
#define BUTTON

#include "Object.h"

class Button :
    public Object
{
public:
    Button();
	virtual ~Button();

    
    bool Press(const sf::Vector2i& mouse_position);
    bool Release(const sf::Vector2i& mouse_position);
    void UpdateMousePosition(const sf::Vector2i& mouse_position);

protected:
    sf::Vector2f GetMousePosition() const;
    virtual void OnPressed();
    virtual void OnReleased();

private:
    bool pressed = false;
    sf::Vector2f mousePosition{0.f, 0.f};
};


#endif // !BUTTON
