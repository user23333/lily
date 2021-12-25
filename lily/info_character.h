#pragma once
#include "encrypt_string.hpp"
#include "compiletime.hpp"
#include "hash_switch.hpp"
#include "global.h"

inline bool IsPlayerCharacter(unsigned Hash) {
	if (Hash == "PlayerFemale_A_C"h || Hash == "PlayerMale_A_C"h)
		return true;
	return false;
}

inline bool IsAICharacter(unsigned Hash) {
	if (Hash == "AIPawn_Base_Female_C"h || Hash == "AIPawn_Base_Male_C"h || Hash == "UltAIPawn_Base_Female_C"h || Hash == "UltAIPawn_Base_Male_C"h)
		return true;
	return false;
}