/////////////////////////////////  MIT LICENSE  ////////////////////////////////

//  Copyright (C) 2026 Edward Jeffrey
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to
//  deal in the Software without restriction, including without limitation the
//  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
//  sell copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//  IN THE SOFTWARE.

#include "ReShade.fxh"

uniform bool PS1DitherEnabled <
    ui_category = "PS1 RGB555 Dither";
    ui_label = "Enable Dithering";
    ui_tooltip = "When disabled, the image is still quantized to RGB555.";
> = true;

uniform float PS1DitherResolutionX <
    ui_category = "PS1 RGB555 Dither";
    ui_label = "Dither Grid Width";
    ui_tooltip = "Number of simulated source pixels across the content area.";
    ui_type = "drag";
    ui_min = 1.0;
    ui_max = BUFFER_WIDTH;
    ui_step = 1.0;
> = BUFFER_WIDTH;

uniform float PS1DitherResolutionY <
    ui_category = "PS1 RGB555 Dither";
    ui_label = "Dither Grid Height";
    ui_tooltip = "Number of simulated source pixels down the content area.";
    ui_type = "drag";
    ui_min = 1.0;
    ui_max = BUFFER_HEIGHT;
    ui_step = 1.0;
> = BUFFER_HEIGHT;

uniform float PS1DitherContentWidth <
    ui_category = "Advanced Grid Alignment";
    ui_label = "Content Width";
    ui_tooltip = "Width of the game image in backbuffer pixels.";
    ui_type = "drag";
    ui_min = 1.0;
    ui_max = BUFFER_WIDTH;
    ui_step = 1.0;
> = BUFFER_WIDTH;

uniform float PS1DitherContentHeight <
    ui_category = "Advanced Grid Alignment";
    ui_label = "Content Height";
    ui_tooltip = "Height of the game image in backbuffer pixels.";
    ui_type = "drag";
    ui_min = 1.0;
    ui_max = BUFFER_HEIGHT;
    ui_step = 1.0;
> = BUFFER_HEIGHT;

uniform float PS1DitherContentOffsetX <
    ui_category = "Advanced Grid Alignment";
    ui_label = "Content Offset X";
    ui_tooltip = "Distance from the left edge of the backbuffer to the content.";
    ui_type = "drag";
    ui_min = 0.0;
    ui_max = BUFFER_WIDTH;
    ui_step = 1.0;
> = 0.0;

uniform float PS1DitherContentOffsetY <
    ui_category = "Advanced Grid Alignment";
    ui_label = "Content Offset Y";
    ui_tooltip = "Distance from the top edge of the backbuffer to the content.";
    ui_type = "drag";
    ui_min = 0.0;
    ui_max = BUFFER_HEIGHT;
    ui_step = 1.0;
> = 0.0;

float GetPS1DitherOffset(int2 position)
{
    position &= 3;

    if (position.y == 0) {
        if (position.x == 0) return -4.0;
        if (position.x == 1) return  0.0;
        if (position.x == 2) return -3.0;
        return 1.0;
    }
    if (position.y == 1) {
        if (position.x == 0) return  2.0;
        if (position.x == 1) return -2.0;
        if (position.x == 2) return  3.0;
        return -1.0;
    }
    if (position.y == 2) {
        if (position.x == 0) return -3.0;
        if (position.x == 1) return  1.0;
        if (position.x == 2) return -4.0;
        return 0.0;
    }

    if (position.x == 0) return  3.0;
    if (position.x == 1) return -1.0;
    if (position.x == 2) return  2.0;
    return -2.0;
}

float4 PS1DitherPS(float4 position : SV_Position, float2 texcoord : TEXCOORD)
    : SV_Target
{
    float4 color = tex2D(ReShade::BackBuffer, texcoord);
    float offset = 0.0;

    if (PS1DitherEnabled) {
        float2 contentSize = max(
            float2(PS1DitherContentWidth, PS1DitherContentHeight), 1.0);
        float2 gridSize = max(
            float2(PS1DitherResolutionX, PS1DitherResolutionY), 1.0);
        float2 contentOffset = float2(
            PS1DitherContentOffsetX, PS1DitherContentOffsetY);

        // PostProcessVS supplies top-left-origin texture coordinates. Mapping
        // through the content and grid sizes makes every group of upscaled
        // output pixels select the same matrix entry as its source pixel.
        float2 backbufferPosition = texcoord * BUFFER_SCREEN_SIZE;
        float2 contentPosition = clamp(
            backbufferPosition - contentOffset, 0.0,
            max(contentSize - 0.0001, 0.0));
        int2 gridPosition = int2(floor(
            contentPosition * gridSize / contentSize));
        offset = GetPS1DitherOffset(gridPosition);
    }

    // The PlayStation adds the signed matrix value in 8-bit channel space,
    // then truncates each channel to its five-bit framebuffer representation.
    float3 channel = clamp(color.rgb * 255.0 + offset, 0.0, 255.0);
    color.rgb = floor(channel / 8.0) / 31.0;
    return color;
}

technique PS1Dither <
    ui_label = "PS1 RGB555 Dither";
    ui_tooltip = "PlayStation-style 4x4 ordered dithering and RGB555 quantization.";
>
{
    pass
    {
        VertexShader = PostProcessVS;
        PixelShader = PS1DitherPS;
    }
}
