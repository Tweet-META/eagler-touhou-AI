#pragma once
#include <cstdint>
namespace Th06Ai {
std::uint16_t filter(std::uint16_t input);
void configureSession();
void resetPracticeResources();
void commitLocal(std::uint16_t humanInput);
}
