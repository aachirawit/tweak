#pragma once
#include <vector>

namespace szk::kerning
{
struct data;

const data& get(const std::vector<unsigned char>& blob);

float layout_units(const data& d);

float pair(const data& d, unsigned int prev_codepoint, unsigned int codepoint);
} // namespace szk::kerning
