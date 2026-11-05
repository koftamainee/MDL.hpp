// Simple throughput benchmark on generated config-like files.
// Build with -DMDL_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release.

#include <MDL/MDL.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

namespace {

using namespace MDL; // NOLINT(google-build-using-namespace)

enum class Blend : u8 {
    Opaque,
    AlphaBlend,
    Additive,
};

struct Material {
    std::string              Name;
    std::string              Shader;
    Blend                    BlendMode = Blend::Opaque;
    bool                     DoubleSided = false;
    f32                      Roughness = 0;
    f32                      Metallic = 0;
    std::array<f32, 4>       Color {};
    std::vector<std::string> Textures;
};

struct MaterialLibrary {
    std::string           Version;
    std::vector<Material> Materials;
};

// ~300 lines, the upper end of a typical config.
std::string MakeConfig() {
    std::string text = "# generated\nversion \"1.0\"\nmaterials [\n";
    for (int i = 0; i < 30; ++i) {
        text += std::format(
            "  {{\n"
            "    name material_{}\n"
            "    shader shaders/pbr.slang\n"
            "    blend_mode {}\n"
            "    double_sided {}\n"
            "    roughness 0.{}\n"
            "    metallic {}.5\n"
            "    color [1.0 0.5 0.25 1]\n"
            "    textures [textures/albedo_{}.png `textures\\normal_{}.png`]\n"
            "  }}\n",
            i, i % 2 == 0 ? "opaque" : "alpha_blend", i % 3 == 0 ? "true" : "false", i % 10, i % 2, i, i);
    }
    text += "]\n";
    return text;
}

template <class Fn>
void Run(const char *name, usize bytes, Fn &&fn) {
    using Clock = std::chrono::steady_clock;

    // warm up and pick an iteration count for ~0.5s
    usize iterations = 1;
    while (true) {
        auto start = Clock::now();
        for (usize i = 0; i < iterations; ++i) {
            fn();
        }
        auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        if (elapsed > 0.5) {
            double perIter = elapsed / static_cast<double>(iterations);
            double mbps = static_cast<double>(bytes) / perIter / (1024.0 * 1024.0);
            std::printf("%-28s %10.2f us/op %10.1f MB/s\n", name, perIter * 1e6, mbps); // NOLINT
            return;
        }
        iterations *= 2;
    }
}

volatile usize g_Sink = 0;

} // namespace

int main() {
    std::string source = MakeConfig();
    std::printf("input: %zu bytes, %zu lines\n\n", source.size(), // NOLINT
                static_cast<usize>(std::count(source.begin(), source.end(), '\n')));

    Run("Parse", source.size(), [&] {
        auto doc = Parse(source);
        g_Sink = g_Sink + doc->Root().Size();
    });

    Run("Parse (zero copy)", source.size(), [&] {
        ParseOptions options;
        options.CopySource = false;
        auto doc = Parse(source, options);
        g_Sink = g_Sink + doc->Root().Size();
    });

    alignas(std::max_align_t) static std::array<std::byte, 256 * 1024> buffer {};
    Run("Parse (stack buffer)", source.size(), [&] {
        ParseOptions options;
        options.Buffer = buffer;
        options.CopySource = false;
        auto doc = Parse(source, options);
        g_Sink = g_Sink + doc->Root().Size();
    });

    Run("Parse + Read<T>", source.size(), [&] {
        auto lib = Load<MaterialLibrary>(source);
        g_Sink = g_Sink + lib->Materials.size();
    });

    auto        lib = Load<MaterialLibrary>(source);
    std::string out;
    out.reserve(source.size() * 2);
    Run("Write<T>", source.size(), [&] {
        out.clear();
        (void)Write(*lib, Output::ToString(out));
        g_Sink = g_Sink + out.size();
    });

    return 0;
}
