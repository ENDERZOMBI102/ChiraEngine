#include "InputManager.h"

#include <config/ConEntry.h>

using namespace chira;

[[maybe_unused]]
ConVar input_invert_x_axis{"input_invert_x_axis", false, "Invert the X axis for a mouse or controller.", CON_FLAG_CACHE}; // NOLINT(cert-err58-cpp)
[[maybe_unused]]
ConVar input_invert_y_axis{"input_invert_y_axis", false, "Invert the Y axis for a mouse or controller.", CON_FLAG_CACHE}; // NOLINT(cert-err58-cpp)
