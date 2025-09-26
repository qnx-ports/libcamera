/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2023 Raspberry Pi Ltd
 *
 * CAC (Chromatic Abberation Correction) algorithm status
 */
#pragma once

struct CacStatus {
#ifdef __QNX__
	std::vector<double, NothrowAllocator<double>> lutRx;
	std::vector<double, NothrowAllocator<double>> lutRy;
	std::vector<double, NothrowAllocator<double>> lutBx;
	std::vector<double, NothrowAllocator<double>> lutBy;
#else
	std::vector<double> lutRx;
	std::vector<double> lutRy;
	std::vector<double> lutBx;
	std::vector<double> lutBy;
#endif
};
