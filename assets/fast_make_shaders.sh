#!/bin/bash

function compile {
  glslangValidator -V $1 -o ../bin/$1.spv
}

compile green_gui_sdf.frag
compile green_gui_sdf.vert
