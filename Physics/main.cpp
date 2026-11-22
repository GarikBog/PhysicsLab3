#include <SFML/Graphics.hpp>

int main()
{
    sf::RenderWindow window(sf::VideoMode({640, 360}), "PhysicsLab3 | SFML");
    window.setFramerateLimit(60);

    sf::CircleShape marker(32.f);
    marker.setPosition({288.f, 148.f});
    marker.setFillColor(sf::Color(73, 172, 242));

    while (window.isOpen())
    {
        while (const auto event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
                window.close();
        }

        if (window.isOpen())
        {
            window.clear(sf::Color(23, 29, 41));
            window.draw(marker);
            window.display();
        }
    }
}
