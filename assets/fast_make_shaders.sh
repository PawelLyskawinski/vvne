#!/bin/bash

function compile {
  glslangValidator -V $1 -o ../bin/$(./hasher.py $1)
}

compile triangle_push.frag
compile triangle_push.vert
