/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2019, Raspberry Pi Ltd
 *
 * ALSC (auto lens shading correction) control algorithm status
 */
#pragma once

#include <vector>

#ifdef __QNX__
#include <no_throw_allocator.h>
#endif

/*
 * The ALSC algorithm should post the following structure into the image's
 * "alsc.status" metadata.
 */

struct AlscStatus {
#ifdef __QNX__
	std::vector<double, NothrowAllocator<double>> r;
	std::vector<double, NothrowAllocator<double>> g;
	std::vector<double, NothrowAllocator<double>> b;
#else
	std::vector<double> r;
	std::vector<double> g;
	std::vector<double> b;
#endif
	unsigned int rows;
	unsigned int cols;
};
