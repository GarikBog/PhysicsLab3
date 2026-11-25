#include "Benchmark.h"
#include "Scene.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <intrin.h>
#pragma comment(lib, "Advapi32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
    struct Options
    {
        unsigned int seconds = 50;
        unsigned int repeats = 3;
        unsigned int frameHz = 120;
        unsigned int warmupSeconds = 5;
        std::string scene = "lab04_timing_primitive";
        std::filesystem::path output;
    };

    struct Variant
    {
        const char* name;
        GameState mode;
        unsigned int baseHz;
    };

    constexpr std::array<Variant, 4> variants{{
        {"Primitive60", GameState::Primitive, 60},
        {"Primitive240", GameState::Primitive, 240},
        {"CCD60", GameState::CCD, 60},
        {"MultiRate60_120_240", GameState::MultyRate, 60}
    }};

    // A body's slot is captured immediately after loading, before groups can change.
    using FinalState = std::vector<std::tuple<float, float, float, float, SpeedState>>;

    struct Run
    {
        std::size_t variant = 0;
        unsigned int repeat = 0;
        unsigned int order = 0;
        std::uint64_t frames = 0;
        std::uint64_t steps = 0;
        std::uint64_t fastSubsteps = 0;
        std::uint64_t contacts = 0;
        std::uint64_t limitHits = 0;
        std::size_t escapedBodies = 0;
        double physicsMs = 0.0;
        FinalState state;

        double FrameMs() const { return physicsMs / static_cast<double>(frames); }
        double StepMs() const { return physicsMs / static_cast<double>(steps); }
        double FastPerFrame() const { return static_cast<double>(fastSubsteps) / frames; }
    };

    struct Summary
    {
        double frameMs = 0.0;
        double frameMsSd = 0.0;
        double stepMs = 0.0;
        double fastPerFrame = 0.0;
        std::uint64_t limitHits = 0;
        std::size_t maxEscaped = 0;
        bool repeatable = true;
    };

    unsigned int ParseNumber(const char* text, unsigned int minimum, unsigned int maximum)
    {
        const std::string value(text);
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Expected an integer, got: " + value);
        const auto number = std::stoull(value);
        if (number < minimum || number > maximum)
            throw std::runtime_error("Number out of range: " + value);
        return static_cast<unsigned int>(number);
    }

    Options ParseOptions(int argc, char* argv[])
    {
        Options options;
        if (std::string(argv[1]) != "--benchmark")
            throw std::runtime_error("Unknown argument. Use --help or --benchmark.");
        for (int i = 2; i < argc; ++i)
        {
            const std::string key(argv[i]);
            if (i + 1 >= argc)
                throw std::runtime_error("Missing value for " + key);
            const char* value = argv[++i];
            if (key == "--seconds") options.seconds = ParseNumber(value, 1, 3600);
            else if (key == "--repeats") options.repeats = ParseNumber(value, 1, 100);
            else if (key == "--frame-hz") options.frameHz = ParseNumber(value, 1, 1000);
            else if (key == "--warmup-seconds") options.warmupSeconds = ParseNumber(value, 0, 600);
            else if (key == "--scene") options.scene = value;
            else if (key == "--output-dir") options.output = std::filesystem::u8path(value);
            else throw std::runtime_error("Unknown option: " + key);
        }
        // Scene names are also checked by Scene::LoadSnapshot. This keeps the copied input local.
        if (options.scene.empty() || options.scene.find_first_of("/\\:<>") != std::string::npos ||
            options.scene == "." || options.scene == "..")
            throw std::runtime_error("--scene must be a snapshot name without a directory.");
        return options;
    }

    std::string UtcTimestamp()
    {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        std::ostringstream value;
        value << std::setfill('0') << std::setw(4) << time.wYear << '-' << std::setw(2) << time.wMonth
              << '-' << std::setw(2) << time.wDay << '_' << std::setw(2) << time.wHour << '-'
              << std::setw(2) << time.wMinute << '-' << std::setw(2) << time.wSecond << "_UTC";
        return value.str();
    }

    std::filesystem::path OutputDirectory(const Options& options, const std::filesystem::path& root)
    {
        if (!options.output.empty())
        {
            const auto path = std::filesystem::absolute(options.output);
            if (std::filesystem::exists(path) && !std::filesystem::is_empty(path))
                throw std::runtime_error("Output directory must be empty: " + path.u8string());
            std::filesystem::create_directories(path);
            return path;
        }
        const auto base = root / "output" / "benchmarks";
        std::filesystem::create_directories(base);
        const auto stamp = UtcTimestamp();
        for (unsigned int suffix = 0; ; ++suffix)
        {
            const auto path = base / (stamp + (suffix ? "_" + std::to_string(suffix) : ""));
            if (std::filesystem::create_directory(path))
                return path;
        }
    }

    std::string CpuName()
    {
        int registers[4]{};
        __cpuid(registers, static_cast<int>(0x80000000u));
        if (static_cast<unsigned int>(registers[0]) < 0x80000004u)
            return "unknown";
        char brand[49]{};
        for (unsigned int i = 0; i < 3; ++i)
        {
            __cpuid(registers, static_cast<int>(0x80000002u + i));
            std::memcpy(brand + i * 16, registers, 16);
        }
        const std::string name(brand);
        const auto first = name.find_first_not_of(' ');
        return first == std::string::npos ? "unknown" : name.substr(first, name.find_last_not_of(' ') - first + 1);
    }

    std::string WindowsBuild()
    {
        char build[64]{};
        DWORD size = sizeof(build);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                        "CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &size) != ERROR_SUCCESS)
            return "unknown";
        return build;
    }

    void Load(Scene& scene, const Options& options, const Variant& variant)
    {
        SceneSettings settings;
        std::string error;
        const auto name = sf::String::fromUtf8(options.scene.begin(), options.scene.end());
        if (!scene.LoadSnapshot(name, settings, error))
            throw std::runtime_error("Cannot load " + options.scene + ": " + (error.empty() ? "snapshot not found" : error));
        Game::gameState = variant.mode;
        Game::TimeDelta = 1.f / variant.baseHz;
        Game::physicsMetrics = {};
    }

    Run Simulate(const Options& options, const Variant& variant, unsigned int seconds, bool collect)
    {
        Run result;
        result.frames = static_cast<std::uint64_t>(seconds) * options.frameHz;
        const auto bodies = Game::GetPhyObjectsSnapshot();
        std::vector<bool> escaped(bodies.size(), false);
        unsigned int phase = 0;
        for (std::uint64_t frame = 0; frame < result.frames; ++frame)
        {
            Game::BeginPhysicsFrame();
            // Exact rational scheduling: e.g. 60/120 => 0,1 steps; 240/120 => 2,2.
            // Unlike subtracting float dt, this gives exactly seconds*baseHz steps.
            phase += variant.baseHz;
            while (phase >= options.frameHz)
            {
                Game::AdvancePhysics();
                phase -= options.frameHz;
                if (collect)
                {
                    // Validation is outside Game's measured physics interval.
                    for (std::size_t i = 0; i < bodies.size(); ++i)
                    {
                        const auto p = bodies[i]->GetPosition();
                        const auto v = bodies[i]->GetVelocity();
                        if (!std::isfinite(p.first) || !std::isfinite(p.second) ||
                            !std::isfinite(v.x) || !std::isfinite(v.y))
                            throw std::runtime_error("Non-finite physics state during benchmark");
                        const auto size = bodies[i]->GetSize();
                        if (bodies[i]->GetMass() > 0.f &&
                            (p.first + size.first < -0.01f || p.second + size.second < -0.01f ||
                             p.first > 640.01f || p.second > 360.01f))
                            escaped[i] = true;
                    }
                }
            }
            result.physicsMs += Game::physicsMetrics.frameMilliseconds;
            result.steps += Game::physicsMetrics.steps;
            result.fastSubsteps += Game::physicsMetrics.fastSubsteps;
            result.contacts += Game::physicsMetrics.ccdContacts;
            result.limitHits += Game::physicsMetrics.ccdLimitHits;
        }
        if (collect)
        {
            result.escapedBodies = static_cast<std::size_t>(std::count(escaped.begin(), escaped.end(), true));
            for (const auto* body : bodies)
            {
                const auto p = body->GetPosition();
                const auto v = body->GetVelocity();
                result.state.emplace_back(p.first, p.second, v.x, v.y, body->GetSpeedState());
            }
        }
        return result;
    }

    std::ofstream File(const std::filesystem::path& path)
    {
        std::ofstream file;
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.open(path, std::ios::binary | std::ios::trunc);
        file.imbue(std::locale::classic());
        file << std::fixed << std::setprecision(9);
        return file;
    }

    std::array<Summary, 4> Summarize(const std::vector<Run>& runs)
    {
        std::array<Summary, 4> summaries{};
        for (std::size_t v = 0; v < variants.size(); ++v)
        {
            auto& summary = summaries[v];
            std::vector<const Run*> selected;
            for (const auto& run : runs)
                if (run.variant == v) selected.push_back(&run);
            const double count = static_cast<double>(selected.size());
            for (const auto* run : selected)
            {
                summary.frameMs += run->FrameMs() / count;
                summary.stepMs += run->StepMs() / count;
                summary.fastPerFrame += run->FastPerFrame() / count;
                summary.limitHits += run->limitHits;
                summary.maxEscaped = std::max(summary.maxEscaped, run->escapedBodies);
                summary.repeatable = summary.repeatable && run->state == selected.front()->state;
            }
            if (selected.size() > 1)
            {
                double variance = 0.0;
                for (const auto* run : selected)
                    variance += std::pow(run->FrameMs() - summary.frameMs, 2);
                summary.frameMsSd = std::sqrt(variance / (count - 1));
            }
        }
        return summaries;
    }

    void Export(const std::filesystem::path& directory, const Options& options, const std::vector<Run>& runs,
                const std::array<Summary, 4>& summaries, std::size_t moving, std::size_t fixed)
    {
        auto csv = File(directory / "runs.csv");
        csv << "variant,repeat,execution_order,base_hz,frame_hz,simulation_seconds,frames,base_steps,physics_total_ms,"
               "mean_ms_frame,mean_ms_step,fast_group_substeps,fast_group_substeps_frame,ccd_contacts,ccd_limit_hits,escaped_bodies\n";
        for (const auto& run : runs)
            csv << variants[run.variant].name << ',' << run.repeat << ',' << run.order << ','
                << variants[run.variant].baseHz << ',' << options.frameHz << ',' << options.seconds << ','
                << run.frames << ',' << run.steps << ',' << run.physicsMs << ',' << run.FrameMs() << ','
                << run.StepMs() << ',' << run.fastSubsteps << ',' << run.FastPerFrame() << ','
                << run.contacts << ',' << run.limitHits << ',' << run.escapedBodies << '\n';

        auto summaryCsv = File(directory / "summary.csv");
        summaryCsv << "variant,repeats,base_hz,frame_hz,mean_ms_frame,sd_ms_frame,mean_ms_step,"
                      "fast_group_substeps_frame,ccd_limit_hits_total,max_escaped_bodies,repeatable\n";
        auto report = File(directory / "report.md");
        report << "# Замеры производительности физики\n\n"
               << "Сцена: `" << options.scene << "`; подвижных тел: " << moving << ", неподвижных: " << fixed
               << ". Исходный файл сохранён рядом как `input-scene.txt`.\n\n"
               << "На вариант: " << options.repeats << " повторения по " << options.seconds
               << " с виртуального времени. Перед каждым повторением: прогрев " << options.warmupSeconds
               << " с и повторная загрузка начальной сцены. Playback = 1x.\n\n"
               << "Расписание: " << options.frameHz << " виртуальных кадров/с; рендеринг и ожидание кадров отключены. "
                  "`ms/frame` — средняя стоимость физики при этом расписании, включая кадры без шага. "
                  "Это не время отрисовки и не измеренный FPS. Прогон выполняется без ожидания реального времени.\n\n"
               << "| Вариант | Базовая частота | ms/frame, среднее ± SD | ms/step | fast-подшагов/кадр | LIMIT, всего | Вылетевших тел, макс. | Повторяемость |\n"
                  "|---|---:|---:|---:|---:|---:|---:|---|\n" << std::setprecision(6);
        for (std::size_t v = 0; v < variants.size(); ++v)
        {
            const auto& s = summaries[v];
            summaryCsv << variants[v].name << ',' << options.repeats << ',' << variants[v].baseHz << ','
                       << options.frameHz << ',' << s.frameMs << ',' << s.frameMsSd << ',' << s.stepMs << ','
                       << s.fastPerFrame << ',' << s.limitHits << ',' << s.maxEscaped << ','
                       << (options.repeats < 2 ? "not_checked" : s.repeatable ? "true" : "false") << '\n';
            report << "| " << variants[v].name << " | " << variants[v].baseHz << " | " << s.frameMs << " ± "
                   << s.frameMsSd << " | " << s.stepMs << " | " << s.fastPerFrame << " | " << s.limitHits
                   << " | " << s.maxEscaped << " | "
                   << (options.repeats < 2 ? "Не проверялась" : s.repeatable ? "Совпала" : "Различается") << " |\n";
        }
        report << "\nSD — выборочное стандартное отклонение средних по повторениям (при одном повторении — 0). "
                  "Порядок вариантов циклически сдвигается между повторениями.\n\n"
                  "## Как считалось время\n\n"
                  "Используется тот же `Game::AdvancePhysics`, что и в интерактивной сцене, "
                  "с `std::chrono::steady_clock`. Измерение включает проверку столкновений, "
                  "реакцию и интегрирование. Загрузка, прогрев, проверка состояния, экспорт и графика исключены.\n\n"
                  "- `ms/frame = сумма измеренного времени физики / число виртуальных кадров`.\n"
                  "- `ms/step = сумма измеренного времени физики / число базовых шагов`.\n"
                  "- Fast-подшаг — один проход активной быстрой группы, независимо от количества тел в ней. "
                  "Частоты Multi-rate при базе 60 Гц: slow 60, medium 120, fast 240 Гц. "
                  "При 120 кадрах/с непрерывно активная fast-группа даёт в среднем 2 подшага/кадр.\n"
                  "- Повторяемость: точное совпадение конечных позиций, скоростей и групп тел "
                  "внутри одного процесса и одной сборки. Это не межплатформенная гарантия.\n"
                  "- Вылет: тело хотя бы на одном базовом шаге оказалось целиком за полем 640x360 "
                  "с допуском 0.01 px. Частичное пересечение граничной стены сюда не входит.\n\n"
                  "Результаты относятся к этой сцене и машине; другие траектории и нагрузка дают другие времена. "
                  "Для выводов об устойчивости и туннелировании дополнительно используются демонстрационные сцены.\n\n"
                  "## Окружение\n\n"
               << "- Дата: " << UtcTimestamp() << ".\n"
               << "- CPU: " << CpuName() << "; логических потоков: " << std::thread::hardware_concurrency() << ".\n"
               << "- OS: Windows x64, build " << WindowsBuild() << ".\n";
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory))
            report << "- RAM: " << std::setprecision(1) << memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0) << " GiB.\n";
#ifdef NDEBUG
        report << "- Сборка: Release, /O2, /fp:precise.\n";
#else
        report << "- Сборка: Debug. Для итоговых чисел пересоберите Release.\n";
#endif
        report << "- Компилятор: MSVC " << _MSC_FULL_VER << "; C++17; SFML "
               << SFML_VERSION_MAJOR << '.' << SFML_VERSION_MINOR << '.' << SFML_VERSION_PATCH << ".\n";
        csv.close();
        summaryCsv.close();
        report.close();
    }
}

int RunPhysicsBenchmarks(int argc, char* argv[])
{
    try
    {
        if (argc < 2 || std::string(argv[1]) == "--help" ||
            (argc == 3 && std::string(argv[1]) == "--benchmark" && std::string(argv[2]) == "--help"))
        {
            std::cout << "Physics.exe: interactive simulation\n"
                         "Physics.exe --benchmark [--seconds 50] [--repeats 3] [--frame-hz 120]\n"
                         "    [--warmup-seconds 5] [--scene lab04_timing_primitive] [--output-dir EMPTY_DIRECTORY]\n"
                         "Benchmarks four variants without rendering or real-time waiting.\n";
            return 0;
        }
        const auto options = ParseOptions(argc, argv);
        const auto snapshots = Scene::DefaultSnapshotDirectory();
        std::size_t moving = 0, fixed = 0;
        std::filesystem::path directory;
        {
            Scene input(640, 360, 16, snapshots);
            Load(input, options, variants.front()); // Check input before creating any output.
            moving = Game::GetPhyObjectsSnapshot(false).size();
            fixed = Game::staticObjects.size();
            if (moving == 0)
                throw std::runtime_error("The benchmark scene has no moving bodies.");
            directory = OutputDirectory(options, snapshots.parent_path());
            auto source = snapshots / std::filesystem::u8path(options.scene);
            if (source.extension() != ".txt" && source.extension() != ".TXT") source += ".txt";
            std::filesystem::copy_file(source, directory / "input-scene.txt");
        }
        // Every pass reloads the saved input, even if the original is edited during a run.
        Scene scene(640, 360, 16, directory);
        auto inputOptions = options;
        inputOptions.scene = "input-scene";
        std::vector<Run> runs;
        for (unsigned int repeat = 0; repeat < options.repeats; ++repeat)
        {
            for (std::size_t offset = 0; offset < variants.size(); ++offset)
            {
                const auto v = (repeat + offset) % variants.size();
                Load(scene, inputOptions, variants[v]);
                if (options.warmupSeconds)
                    (void)Simulate(options, variants[v], options.warmupSeconds, false);
                Load(scene, inputOptions, variants[v]);
                auto run = Simulate(options, variants[v], options.seconds, true);
                run.variant = v;
                run.repeat = repeat + 1;
                run.order = static_cast<unsigned int>(runs.size()) + 1;
                std::cout << variants[v].name << " repeat " << run.repeat << ": "
                          << std::fixed << std::setprecision(6) << run.FrameMs() << " ms/frame, "
                          << run.StepMs() << " ms/step, LIMIT=" << run.limitHits
                          << ", escaped=" << run.escapedBodies << '\n';
                runs.push_back(std::move(run));
            }
        }
        Export(directory, options, runs, Summarize(runs), moving, fixed);
        std::cout << "Saved: " << directory.u8string() << "\nFiles: report.md, summary.csv, runs.csv, input-scene.txt\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
