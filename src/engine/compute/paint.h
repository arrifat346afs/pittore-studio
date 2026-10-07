#pragma once
// Brush kernel umbrella — backward-compatible entry point.
// New code may include the per-brush header it needs directly:
//   engine/compute/brushes/selection_mask/selection_mask.h
//   engine/compute/brushes/dab/dab.h
//   engine/compute/brushes/erase/erase.h
//   engine/compute/brushes/tone/tone.h
//   engine/compute/brushes/flood/flood.h
//   engine/compute/brushes/replace/replace.h
//   engine/compute/brushes/heal/heal.h
//   engine/compute/brushes/clone/clone.h
//   engine/compute/brushes/composite/composite.h
//   engine/compute/brushes/mip/mip.h
// The definitions live in brushes/*/*.cpp; this umbrella header only pulls
// their declarations in, so existing call sites keep one include.

#include "engine/compute/brushes/clone/clone.h"
#include "engine/compute/brushes/composite/composite.h"
#include "engine/compute/brushes/dab/dab.h"
#include "engine/compute/brushes/erase/erase.h"
#include "engine/compute/brushes/flood/flood.h"
#include "engine/compute/brushes/heal/heal.h"
#include "engine/compute/brushes/mip/mip.h"
#include "engine/compute/brushes/replace/replace.h"
#include "engine/compute/brushes/selection_mask/selection_mask.h"
#include "engine/compute/brushes/smudge/smudge.h"
#include "engine/compute/brushes/tone/tone.h"
