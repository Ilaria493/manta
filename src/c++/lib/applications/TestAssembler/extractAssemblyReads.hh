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

#pragma once

#include "assembly/AssemblyReadInfo.hh"
#include "options/ReadScannerOptions.hh"
#include "options/SmallAssemblerOptions.hh"
#include "options/IterativeAssemblerOptions.hh"

//#define ITERATIVE_ASSEMBLER

#ifdef ITERATIVE_ASSEMBLER
typedef IterativeAssemblerOptions AssemblerOptions;
#else
typedef SmallAssemblerOptions AssemblerOptions;
#endif


/// load all reads form bam into assembly input structure with minimal
/// filtration / input manipulation
///
void
extractAssemblyReadsFromBam(
    const ReadScannerOptions& scanOpt,
    const AssemblerOptions& asmOpt,
    const char* bamFile,
    AssemblyReadInput& reads);
