#pragma once
#include "Params.h"

namespace stsample
{
    /** Factory kits built from the built-in sounds (presets with a KIT tree). Index 1 ("ST Kit") is what a new
        instance loads; "Init" (index 0) is the empty kit. */
    void registerFactoryKits (st::PresetManager&);
}
