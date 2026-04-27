#pragma once

#include <zephyr/kernel.h>

#define VIERA_EFF_SOLID 0
#define VIERA_EFF_BREATHE 1
#define VIERA_EFF_SPECTRUM 2
#define VIERA_EFF_SWIRL 3
#define VIERA_EFF_MIRROR_FILL 4

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_VIERA_MIRROR_FILL)
#define VIERA_EFF_NUMBER 5
#else
#define VIERA_EFF_NUMBER 4
#endif
