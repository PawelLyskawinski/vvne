#!/bin/bash

function compile {
  glslangValidator -V $1 -o ../bin/$(./hasher.py $1)
}

compile tesselated_ground.frag
compile tesselated_ground.vert
compile tesselated_ground.tesc
compile tesselated_ground.tese
