#ifndef OBJECT
#define OBJECT


#include <optional>
#include <utility>
#include <SFML/Graphics.hpp>

class Object
{
public:
	Object();
	virtual ~Object();

	Object(const Object&) = delete;
	Object& operator=(const Object&) = delete;


	void SetPosition(float x, float y);
	std::pair<float, float> GetPosition() const;

	int GetId() const;

	void Draw(sf::RenderWindow& window);

	void SetTexture(const sf::Texture& texture, sf::IntRect rect);

	void SetSize(int width, int height);
	std::pair<int, int> GetSize() const;

	void SetVisible(bool visible);
	bool IsVisible() const;

protected:
	sf::FloatRect GetBounds() const;

private:
	void UpdateSpriteScale();

	std::optional<sf::Sprite> sprite;


	int id;
	static int idCounter;

	std::pair<float, float> position;
	std::optional<std::pair<int, int>> size;

	bool bIsVisible;
};

#endif // !Object


