#pragma once

#include <memory>
#include <string>
#include "pattern.hpp"

namespace systems::leal::campello_audio::pi {

/// @brief Parse a Strudel-inspired mini-notation string into a Pattern.
///
/// Grammar:
///   pattern   := seq
///   seq       := elem { ws elem }
///   elem      := factor [ "*" number ]
///   factor    := word | "[" seq "]" | word "(" number "," number ")" | "~" | "."
///   word      := [a-zA-Z_][a-zA-Z0-9_:-]*
///   number    := digits [ "." digits ]
///
/// Supported features (Phase A):
///   - Sequential events: "bd sd hh cp"
///   - Repetition: "bd*4"
///   - Grouping: "[hh oh]"
///   - Group repetition: "[hh oh]*2"
///   - Euclidean rhythms: "hh(3,8)"
///   - Rests: "~" or "."
///
/// @param input       Mini-notation string.
/// @param cycleBeats  Length of one pattern cycle in beats (default 4.0 for 4/4).
/// @return nullptr on parse error, otherwise a compiled Pattern.
std::unique_ptr<Pattern> parseMiniNotation(const std::string& input,
                                           double cycleBeats = 4.0);

} // namespace systems::leal::campello_audio::pi
