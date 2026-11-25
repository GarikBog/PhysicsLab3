#include "Scene.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace
{
    struct BodyState
    {
        sf::Vector2f position;
        sf::Vector2i size;
        float mass = 1.f;
        sf::Vector2f velocity;
        sf::Vector2f force;
        bool visible = true;
    };

    template<class... Values>
    bool ReadValues(std::istringstream& input, Values&... values)
    {
        return static_cast<bool>((input >> ... >> values)) && (input >> std::ws).eof();
    }

    bool Finite(sf::Vector2f value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    const char* ModeName(GameState mode)
    {
        switch (mode)
        {
        case GameState::Primitive: return "Primitive";
        case GameState::CCD: return "CCD";
        case GameState::MultyRate: return "MultyRate";
        }
        throw std::runtime_error("Unknown simulation mode");
    }

    void ParseSnapshot(std::istream& file, SceneSettings& settings, std::vector<BodyState>& states)
    {
        bool headerRead = false, inBody = false;
        unsigned int settingsFields = 0, bodyFields = 0;
        std::unordered_set<unsigned int> labels;
        BodyState body;
        std::string line;
        unsigned int lineNumber = 0;
        while (std::getline(file, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
                line.erase(0, 3); // Accept UTF-8 BOM from text editors such as Notepad.
            line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
            std::istringstream input(line);
            input.imbue(std::locale::classic());
            input >> std::boolalpha;
            std::string key;
            if (!(input >> key))
                continue;
            bool valid = false;
            unsigned int field = 0;
            if (!headerRead)
            {
                int version = 0;
                valid = key == "PHYSICSLAB3_SNAPSHOT" && ReadValues(input, version) && version == 1;
                headerRead = valid;
            }
            else if (key == "object" && !inBody)
            {
                unsigned int label = 0;
                valid = ReadValues(input, label) && labels.insert(label).second && states.size() < 10000;
                body = {};
                bodyFields = 0;
                inBody = true;
            }
            else if (key == "end" && inBody)
            {
                valid = (input >> std::ws).eof() && bodyFields == 63 &&
                        Finite(body.position) && Finite(body.velocity) && Finite(body.force) &&
                        std::isfinite(body.mass) && body.mass >= 0.f && body.size.x > 0 && body.size.y > 0 &&
                        (body.mass > 0.f || body.velocity == sf::Vector2f{0.f, 0.f});
                if (valid)
                    states.push_back(body);
                inBody = false;
            }
            else if (inBody)
            {
                if (key == "position") { field = 1; valid = ReadValues(input, body.position.x, body.position.y); }
                else if (key == "size") { field = 2; valid = ReadValues(input, body.size.x, body.size.y); }
                else if (key == "mass") { field = 4; valid = ReadValues(input, body.mass); }
                else if (key == "velocity") { field = 8; valid = ReadValues(input, body.velocity.x, body.velocity.y); }
                else if (key == "force") { field = 16; valid = ReadValues(input, body.force.x, body.force.y); }
                else if (key == "visible") { field = 32; valid = ReadValues(input, body.visible); }
                valid = valid && !(bodyFields & field);
                bodyFields |= field;
            }
            else
            {
                if (key == "time_step") { field = 1; valid = ReadValues(input, settings.timeStep); }
                else if (key == "paused") { field = 2; valid = ReadValues(input, settings.paused); }
                else if (key == "playback_speed") { field = 8; valid = ReadValues(input, settings.playbackSpeed); }
                else if (key == "mode")
                {
                    field = 4;
                    std::string mode;
                    valid = ReadValues(input, mode);
                    if (mode == "Primitive") settings.mode = GameState::Primitive;
                    else if (mode == "CCD") settings.mode = GameState::CCD;
                    else if (mode == "MultyRate") settings.mode = GameState::MultyRate;
                    else valid = false;
                }
                valid = valid && !(settingsFields & field);
                settingsFields |= field;
            }
            if (!valid)
                throw std::runtime_error("Invalid snapshot field at line " + std::to_string(lineNumber));
        }
        if (file.bad() || !headerRead || inBody || (settingsFields & 7) != 7 ||
            !std::isfinite(settings.timeStep) || settings.timeStep <= 0.f ||
            !std::isfinite(settings.playbackSpeed) || settings.playbackSpeed < SceneSettings::MinPlaybackSpeed ||
            settings.playbackSpeed > SceneSettings::MaxPlaybackSpeed)
            throw std::runtime_error("Incomplete or invalid snapshot");
    }
}

Scene::Scene(int sceneWidth, int sceneHeight, int sceneBorder, std::filesystem::path directory)
    : width(sceneWidth), height(sceneHeight), border(sceneBorder), snapshotDirectory(std::move(directory)),
      wallTexture(sf::Image({1, 1}, sf::Color(110, 125, 150))),
      squareTexture(sf::Image({1, 1}, sf::Color(70, 180, 240)))
{
    bodies.push_back(MakeBody({0.f, 0.f}, {width, border}, 0.f));
    bodies.push_back(MakeBody({0.f, static_cast<float>(height - border)}, {width, border}, 0.f));
    bodies.push_back(MakeBody({0.f, static_cast<float>(border)}, {border, height - 2 * border}, 0.f));
    bodies.push_back(MakeBody({static_cast<float>(width - border), static_cast<float>(border)},
                             {border, height - 2 * border}, 0.f));
}

std::filesystem::path Scene::DefaultSnapshotDirectory()
{
    std::array<wchar_t, 32768> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    const auto folder = length > 0 ? std::filesystem::path(executable.data()).parent_path() : std::filesystem::current_path();
    for (auto candidate = folder; !candidate.empty(); candidate = candidate.parent_path())
    {
        if (std::filesystem::exists(candidate / "Physics.sln"))
            return candidate / "snapshots";
        if (candidate == candidate.parent_path())
            break;
    }
    return folder / "snapshots";
}

std::unique_ptr<PhyObject> Scene::MakeBody(sf::Vector2f position, sf::Vector2i size, float mass) const
{
    auto body = std::make_unique<PhyObject>();
    body->SetPosition(position.x, position.y);
    body->SetSize(size.x, size.y);
    body->SetMass(mass);
    body->SetTexture(mass == 0.f ? wallTexture : squareTexture, {{0, 0}, {1, 1}});
    return body;
}

sf::Vector2i Scene::ClampPoint(sf::Vector2i point) const
{
    return {std::clamp(point.x, border, width - border), std::clamp(point.y, border, height - border)};
}

void Scene::AddSquare(sf::Vector2i mouse, int size)
{
    const int x = std::clamp(mouse.x - size / 2, border, width - border - size);
    const int y = std::clamp(mouse.y - size / 2, border, height - border - size);
    bodies.push_back(MakeBody({static_cast<float>(x), static_cast<float>(y)}, {size, size}, 1.f));
}

sf::IntRect Scene::WallRectangle() const
{
    const int left = std::min(std::min(wallAnchor.x, wallEnd.x), width - border - 1);
    const int top = std::min(std::min(wallAnchor.y, wallEnd.y), height - border - 1);
    return {{left, top}, {std::max(1, std::abs(wallAnchor.x - wallEnd.x)),
                         std::max(1, std::abs(wallAnchor.y - wallEnd.y))}};
}

void Scene::ToggleWall(sf::Vector2i mouse)
{
    if (!buildingWall)
    {
        if (mouse.x < border || mouse.x > width - border || mouse.y < border || mouse.y > height - border)
            return;
        wallAnchor = wallEnd = ClampPoint(mouse);
        buildingWall = true;
        return;
    }
    UpdateWall(mouse);
    const auto bounds = WallRectangle();
    bodies.push_back(MakeBody(sf::Vector2f(bounds.position), bounds.size, 0.f));
    buildingWall = false;
}

void Scene::UpdateWall(sf::Vector2i mouse)
{
    if (buildingWall)
        wallEnd = ClampPoint(mouse);
}

void Scene::CancelWall() { buildingWall = false; }
bool Scene::IsBuildingWall() const { return buildingWall; }

void Scene::DrawWallPreview(sf::RenderWindow& window) const
{
    if (!buildingWall)
        return;
    const auto bounds = WallRectangle();
    sf::RectangleShape preview(sf::Vector2f(bounds.size));
    preview.setPosition(sf::Vector2f(bounds.position));
    preview.setFillColor(sf::Color(110, 165, 190, 100));
    preview.setOutlineColor(sf::Color(100, 205, 230));
    preview.setOutlineThickness(-1.f);
    window.draw(preview);
}

std::filesystem::path Scene::SnapshotPath(const sf::String& name) const
{
    if (name.isEmpty() || name.getSize() > 80)
        throw std::runtime_error("Enter a snapshot name (1-80 characters)");
    for (char32_t character : name)
        if (character < U' ' || sf::String(U"<>:\"/\\|?*").find(sf::String(character)) != sf::String::InvalidPos)
            throw std::runtime_error("Snapshot name contains an invalid filename character");
    if (name[name.getSize() - 1] == U'.' || name[name.getSize() - 1] == U' ')
        throw std::runtime_error("Snapshot name cannot end with a dot or space");
    const auto encoded = name.toUtf8();
    std::string filename(encoded.begin(), encoded.end());
    if (filename.size() < 4 || (filename.substr(filename.size() - 4) != ".txt" &&
                                filename.substr(filename.size() - 4) != ".TXT"))
        filename += ".txt";
    return snapshotDirectory / std::filesystem::u8path(filename);
}

bool Scene::SaveSnapshot(const sf::String& name, const SceneSettings& settings, std::string& error) const
{
    error.clear();
    try
    {
        if (!std::isfinite(settings.timeStep) || settings.timeStep <= 0.f ||
            !std::isfinite(settings.playbackSpeed) || settings.playbackSpeed < SceneSettings::MinPlaybackSpeed ||
            settings.playbackSpeed > SceneSettings::MaxPlaybackSpeed)
            throw std::runtime_error("Invalid time step or playback speed");
        const auto path = SnapshotPath(name);
        std::filesystem::create_directories(snapshotDirectory);
        auto temporaryPath = path;
        temporaryPath += ".tmp";
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        file.imbue(std::locale::classic());
        file << std::setprecision(std::numeric_limits<float>::max_digits10) << std::boolalpha;
        file << "PHYSICSLAB3_SNAPSHOT 1\n# Editable scene. Units: px, seconds, px/s. Mass 0 = fixed wall.\n"
             << "time_step " << settings.timeStep << "\nmode " << ModeName(settings.mode)
             << "\npaused " << settings.paused << "\nplayback_speed " << settings.playbackSpeed << '\n';
        std::unordered_set<const PhyObject*> owned;
        for (const auto& body : bodies)
            owned.insert(body.get());
        unsigned int index = 0;
        // Preserve the current per-group order as well as the physical state.
        for (const PhyObject* body : Game::GetPhyObjectsSnapshot())
        {
            if (!owned.count(body))
                continue;
            const auto position = body->GetPosition();
            const auto size = body->GetSize();
            const auto velocity = body->GetVelocity();
            const auto force = body->GetForce();
            file << "\nobject " << index++ << "\nposition " << position.first << ' ' << position.second
                 << "\nsize " << size.first << ' ' << size.second << "\nmass " << body->GetMass()
                 << "\nvelocity " << velocity.x << ' ' << velocity.y << "\nforce " << force.x << ' ' << force.y
                 << "\nvisible " << body->IsVisible() << "\nend\n";
        }
        file.flush();
        if (!file)
            throw std::runtime_error("Cannot write snapshot file");
        file.close();
        if (!file)
            throw std::runtime_error("Cannot finish writing snapshot file");
        if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace snapshot file");
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool Scene::LoadSnapshot(const sf::String& name, SceneSettings& settings, std::string& error)
{
    error.clear();
    try
    {
        const auto path = SnapshotPath(name);
        if (!std::filesystem::exists(path))
            return false;
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot read snapshot file");
        SceneSettings loadedSettings;
        std::vector<BodyState> states;
        ParseSnapshot(file, loadedSettings, states);
        std::vector<std::unique_ptr<PhyObject>> loadedBodies;
        loadedBodies.reserve(states.size());
        for (const auto& state : states)
        {
            auto body = MakeBody(state.position, state.size, state.mass);
            body->SetVelocity(state.velocity);
            body->SetForce(state.force);
            body->SetVisible(state.visible);
            loadedBodies.push_back(std::move(body));
        }
        bodies.swap(loadedBodies);
        settings = loadedSettings;
        buildingWall = false;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
