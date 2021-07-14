/*
 * Advanced Configuration and Power Interface (ACPI)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2019-2024 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#include "acpi_table_hpet.hpp"
#include "hpet.hpp"
#include "stdio.hpp"

void Acpi_table_hpet::parse() const
{
    if (EXPECT_FALSE (regs.asid != Acpi_gas::Asid::MEM))
        return;

    auto const hpet { new Hpet { uid } };
    if (EXPECT_FALSE (!hpet))
        panic ("HPET allocation failed");

    trace (TRACE_FIRM, "HPET: %#010lx", uint64_t { regs.addr });
}
