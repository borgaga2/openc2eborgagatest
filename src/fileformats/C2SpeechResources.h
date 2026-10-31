#pragma once
#include <array>
#include <span>
#include <string>
#include <vector>
#include <cstdint>

struct C2SpeechResources {
	std::array<std::array<std::string, 6>, 12> babble;
	std::vector<std::pair<std::string, std::string>> pronunciation_rules;
};
// Reads the verified Steam C2 executable layout; rejects unknown layouts.
// Returned strings are UTF-8. The original executable remains a user asset.
C2SpeechResources readC2SpeechResources(std::span<const uint8_t> executable);
