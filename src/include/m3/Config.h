/*
 * Copyright (C) 2015, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of M3 (Microkernel-based SysteM for Heterogeneous Manycores).
 *
 * M3 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * M3 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <m3/Common.h>

#if defined(__host__)
#   include <m3/arch/host/Config.h>
#elif defined(__t2__)
#   include <m3/arch/t2/Config.h>
#elif defined(__t3__)
#   include <m3/arch/t3/Config.h>
#elif defined(__gem5__)
#   include <m3/arch/gem5/Config.h>
#else
#   error "Unsupported target"
#endif
