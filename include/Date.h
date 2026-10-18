#pragma once

#include <compare>

// A calendar date (spec 4.1). Members are ordered year, month, day so the
// defaulted comparison is chronological.
struct Date {
	int year = 1;
	int month = 1;
	int day = 1;

	auto operator<=>(const Date&) const = default;
};

inline bool isLeapYear(int year) {
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

inline int daysInMonth(int month, int year) {
	static constexpr int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month == 2 && isLeapYear(year)) {
		return 29;
	}
	return days[month - 1];
}

inline bool isValidDate(int day, int month, int year) {
	return year >= 1 && year <= 9999
		&& month >= 1 && month <= 12
		&& day >= 1 && day <= daysInMonth(month, year);
}
