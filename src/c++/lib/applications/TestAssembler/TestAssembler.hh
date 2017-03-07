// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Manta - Structural Variant and Indel Caller
// Copyright (c) 2013-2017 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

/// \author Chris Saunders
///

#pragma once

#include <iostream>
#include <cstdlib>

#include "blt_util/log.hh"
#include "common/OutStream.hh"
#include "common/Program.hh"

#include "assembly/IterativeAssembler.hh"
#include "assembly/SmallAssembler.hh"
#include "TestAssemblerOptions.hh"
#include "extractAssemblyReads.hh"


/// test front-end to run the manta assembler from command-line
///
struct TestAssembler : public illumina::Program
{
    const char*
    name() const
    {
        return "TestAssembler";
    }

    void
    runInternal(int argc, char* argv[]) const;
};
