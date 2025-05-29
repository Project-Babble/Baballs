#pragma once

// Target size in meters
#define TARGET_SIZE_METERS 0.1f

// Target distance from HMD in meters
#define TARGET_DISTANCE_METERS 2.0f

// Target movement speed in degrees
#define TARGET_MOVEMENT_SPEED 1.0f

// Target opacity (0.0 to 1.0)
#define TARGET_OPACITY 1.0f

// Target color components (0.0 to 1.0)
#define TARGET_COLOR_R 0.3254901960784314f
#define TARGET_COLOR_G 0.6196078431372549f
#define TARGET_COLOR_B 0.5411764705882353f

// Target color (ARGB format)
#define TARGET_COLOR 0xFF539E8A        // Babble Sea Green
#define BORDER_COLOR 0x00000000        // invisible

// Target line thickness in pixels
#define TARGET_LINE_THICKNESS 16

#define BORDER_LINE_THICKNESS 4
#define BORDER_SIZE_RATIO 48.0f         // Border is 10% larger than the target
#define BORDER_OPACITY 0.8f            // Slightly more transparent than the target

#define FLOAT_TO_INT_CONSTANT 3355443  // floor(int32(Signed).max_value / 640)

#define M_PI 3.14159f

#define BUILD_TYPE "Release"
#define PREFIX "/usr/local"

#ifdef __APPLE__
#define PLATFORM_MACOS 1
#elif __linux__
#define PLATFORM_LINUX 1
#endif

#define ENABLE_OVERLAY 1
#define ENABLE_TRAINER 1
