#pragma once

//
// Based on 4-part tutorial by OREON_ENGINE
// https://www.youtube.com/watch?v=B3YOLg0sA2g
//

#include "engine.hh"

namespace fft_water {

void generate_h0_k_image(Engine& engine, VkDeviceSize offset_to_billboard_vertices, Texture& fft_water_h0_k_texture,
                         Texture& fft_water_h0_minus_k_texture);

} // namespace fft_water
