#!/bin/bash

function compile {
  glslangValidator -V $1 -o ../bin/$(./hasher.py $1)
}

compile fft_water_h0_k_pass.frag
compile fft_water_h0_k_pass.vert
