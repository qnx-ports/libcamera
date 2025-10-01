/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 *
 * Piecewise linear functions interface
 */
#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "libcamera/internal/vector.h"

namespace libcamera {

namespace ipa {

class Pwl
{
public:
	using Point = Vector<double, 2>;

	struct Interval {
		Interval(double _start, double _end)
			: start(_start), end(_end) {}

		bool contains(double value)
		{
			return value >= start && value <= end;
		}

		double clamp(double value)
		{
			return std::clamp(value, start, end);
		}

		double length() const { return end - start; }

		double start, end;
	};

	Pwl();
#ifdef __QNX__
	Pwl(const std::vector<Point, NothrowAllocator<Point>> &points);
	Pwl(std::vector<Point, NothrowAllocator<Point>> &&points);
#else
	Pwl(const std::vector<Point> &points);
	Pwl(std::vector<Point> &&points);
#endif

	void append(double x, double y, double eps = 1e-6);

	bool empty() const { return points_.empty(); }
	void clear() { points_.clear(); }
	size_t size() const { return points_.size(); }

	Interval domain() const;
	Interval range() const;

	double eval(double x, int *span = nullptr,
		    bool updateSpan = true) const;

	std::pair<Pwl, bool> inverse(double eps = 1e-6) const;
	Pwl compose(const Pwl &other, double eps = 1e-6) const;

	void map(std::function<void(double x, double y)> f) const;

#ifdef __QNX__
	// Don't use std::function to avoid the new operator
	template<typename Func>
	static Pwl combine(Pwl const &pwl0, Pwl const &pwl1, Func &&f, double eps = 1e-6)
	{
		// Implementation copied from the pwl.cpp file
		Pwl result;
		map2(pwl0, pwl1, [&](double x, double y0, double y1) {
			result.append(x, f(x, y0, y1), eps);
		});
		return result;
	}
#else
	static Pwl
	combine(const Pwl &pwl0, const Pwl &pwl1,
		std::function<double(double x, double y0, double y1)> f,
		double eps = 1e-6);
#endif

	Pwl &operator*=(double d);

	std::string toString() const;

private:
#ifdef __QNX__
	// Don't use std::function to avoid the new operator
	template<typename Func>
	static void map2(const Pwl &pwl0, const Pwl &pwl1, Func&& f)
	{
		// Implementation copied from the pwl.cpp file
		int span0 = 0, span1 = 0;
		double x = std::min(pwl0.points_[0].x(), pwl1.points_[0].x());
		f(x, pwl0.eval(x, &span0, false), pwl1.eval(x, &span1, false));

		while (span0 < (int)pwl0.points_.size() - 1 ||
			span1 < (int)pwl1.points_.size() - 1) {
			if (span0 == (int)pwl0.points_.size() - 1)
				x = pwl1.points_[++span1].x();
			else if (span1 == (int)pwl1.points_.size() - 1)
				x = pwl0.points_[++span0].x();
			else if (pwl0.points_[span0 + 1].x() > pwl1.points_[span1 + 1].x())
				x = pwl1.points_[++span1].x();
			else
				x = pwl0.points_[++span0].x();
			f(x, pwl0.eval(x, &span0, false), pwl1.eval(x, &span1, false));
		}
	}
#else
	static void map2(const Pwl &pwl0, const Pwl &pwl1,
			 std::function<void(double x, double y0, double y1)> f);
#endif
	void prepend(double x, double y, double eps = 1e-6);
	int findSpan(double x, int span) const;

#ifdef __QNX__
	std::vector<Point, NothrowAllocator<Point>> points_;
#else
	std::vector<Point> points_;
#endif
};

} /* namespace ipa */

} /* namespace libcamera */
