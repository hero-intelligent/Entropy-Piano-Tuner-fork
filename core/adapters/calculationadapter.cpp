/*****************************************************************************
 * Copyright 2015 Haye Hinrichsen, Christoph Wick
 *
 * This file is part of Entropy Piano Tuner.
 *
 * Entropy Piano Tuner is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Entropy Piano Tuner is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Entropy Piano Tuner. If not, see http://www.gnu.org/licenses/.
 *****************************************************************************/

//=============================================================================
//                            Calculation adapter
//=============================================================================

#include "calculationadapter.h"

#include <algorithm>

#include "../core.h"
#include "../settings.h"


///////////////////////////////////////////////////////////////////////////////
/// \brief Constructor, copies a pointer to the core.
///
/// \param core : Pointer to the instance of the core.
///////////////////////////////////////////////////////////////////////////////

CalculationAdapter::CalculationAdapter(Core *core)
    : mCore(core)
{
}


//-----------------------------------------------------------------------------
//                              start calccuation
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Function called by the GUI to start the calculation of the
/// tuning curve
///////////////////////////////////////////////////////////////////////////////

void CalculationAdapter::startCalculation(const std::string &algorithmName)
{
    PianoManager *pianomanager = mCore->getPianoManager();
    CalculationManager *calculator = &CalculationManager::getSingleton();
    Piano &piano = pianomanager->getPiano();
    calculator->start (algorithmName, piano);

    Settings::getSingleton().setLastUsedAlgorithm(algorithmName);
}


//-----------------------------------------------------------------------------
//                               stop calccuation
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
/// \brief Function called by the GUI to interrupt or stop the
/// calculation thread.
///////////////////////////////////////////////////////////////////////////////

void CalculationAdapter::cancelCalculation()
{
    CalculationManager::getSingleton().stop();
}

std::vector<std::string> CalculationAdapter::getAvailableAlgorithms() const {
    std::vector<std::string> out;
    for (auto src : CalculationManager::getSingleton().getAlgorithms()) {
        out.push_back(src.first);
    }
    return out;
}