#ifndef __COLOR_H_INCLUDED
#define __COLOR_H_INCLUDED

#include <Windows.h>
#include "pb2lib.h"
#include <cassert>

namespace Color {

static constexpr DWORD RED = RGB(255, 100, 60);
static constexpr DWORD BLUE = RGB(100, 150, 255);
static constexpr DWORD PURPLE = RGB(225, 0, 225);
static constexpr DWORD YELLOW = RGB(240, 240, 0);
static constexpr DWORD WHITE = RGB(255, 255, 255);

DWORD from_ping(int ping) noexcept {
	// code assumes lower limit (0, 255, 0) == 0
	constexpr int UPPER_LIMIT = 200; // ping for (255, 0, 0)
	double p = max(0.0, min(1.0, static_cast<double>(ping) / UPPER_LIMIT));

	double red = 1.0, green = 1.0;
	if (p >= 0.5) {
		green = 2.0 - 2.0 * p;
	}
	else {
		red = 2.0 * p;
	}

	return RGB(red * 255, green * 255, 0);
}

DWORD from_team(pb2lib::Team team) {
	switch (team) {
	case pb2lib::Team::BLUE: return Color::BLUE;
	case pb2lib::Team::RED: return Color::RED;
	case pb2lib::Team::PURPLE: return Color::PURPLE;
	case pb2lib::Team::YELLOW: return Color::YELLOW;
	case pb2lib::Team::OBSERVER: return Color::WHITE;
	case pb2lib::Team::AUTO: return Color::WHITE;
	}

	assert(false);
	return Color::WHITE;
}

};

#endif // __COLOR_H_INCLUDED