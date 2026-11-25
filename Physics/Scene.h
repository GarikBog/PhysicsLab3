#ifndef SCENE_H
#define SCENE_H

#include "Game.h"

#include <filesystem>
#include <memory>
#include <string>

struct SceneSettings
{
    static constexpr float MinPlaybackSpeed = 0.1f;
    static constexpr float MaxPlaybackSpeed = 4.f;
    float timeStep = 1.f / 60.f;
    GameState mode = GameState::Primitive;
    bool paused = false;
    float playbackSpeed = 1.f;
};

class Scene
{
public:
    Scene(int width, int height, int border, std::filesystem::path snapshotDirectory);
    static std::filesystem::path DefaultSnapshotDirectory();

    void AddSquare(sf::Vector2i mouse, int size);
    void ToggleWall(sf::Vector2i mouse);
    void UpdateWall(sf::Vector2i mouse);
    void CancelWall();
    bool IsBuildingWall() const;
    void DrawWallPreview(sf::RenderWindow& window) const;

    bool SaveSnapshot(const sf::String& name, const SceneSettings& settings, std::string& error) const;
    // A missing file returns false with an empty error and leaves the scene/settings untouched.
    bool LoadSnapshot(const sf::String& name, SceneSettings& settings, std::string& error);

private:
    std::unique_ptr<PhyObject> MakeBody(sf::Vector2f position, sf::Vector2i size, float mass) const;
    sf::Vector2i ClampPoint(sf::Vector2i point) const;
    sf::IntRect WallRectangle() const;
    std::filesystem::path SnapshotPath(const sf::String& name) const;

    int width;
    int height;
    int border;
    std::filesystem::path snapshotDirectory;
    sf::Texture wallTexture;
    sf::Texture squareTexture;
    std::vector<std::unique_ptr<PhyObject>> bodies;
    bool buildingWall = false;
    sf::Vector2i wallAnchor;
    sf::Vector2i wallEnd;
};

#endif
