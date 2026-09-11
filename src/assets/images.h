#pragma once
#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace szk::images
{
struct texture
{
    ImTextureID id = ImTextureID_Invalid;
    int width = 0;
    int height = 0;
    std::string name;
};

struct options
{
    int max_edge = 512;
    float aspect = 416.f / 650.f;
    float radius_ratio = 0.f;
    float saturate = 1.f;
};

void load_folder(const std::filesystem::path& directory, const options& opts = options());

// Optional full-window background, loaded from a single file (the first image
// in assets/background). Separate from the slide store on purpose: a slide is
// cropped to a fixed portrait aspect, while the background has to cover a
// window whose aspect changes, so it is kept whole and cover-fitted at draw
// time. Returns false when the file is missing or undecodable; background()
// then stays null and the shell keeps its flat fill.
bool load_background(const std::filesystem::path& file, int max_edge = 1920);
const texture* background();

void update(ID3D11Device* device, ID3D11DeviceContext* context);

const std::vector<texture>& ready();

void shutdown();
} // namespace szk::images
