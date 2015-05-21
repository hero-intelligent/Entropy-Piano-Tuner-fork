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

#include "logforqt.h"
#include "qdebug.h"

LogForQt::LogForQt()
{

}

LogForQt::~LogForQt()
{

}

void LogForQt::impl_verbose(const char *l) {
    qDebug() << "Verbose: " << l;
}

void LogForQt::impl_debug(const char *l) {
    qDebug() << "Debug: " << l;
}

void LogForQt::impl_information(const char *l) {
    qDebug() << "Information: " << l;
}

void LogForQt::impl_warning(const char *l)  {
    qWarning() << "Warning: " << l;
}

void LogForQt::impl_error(const char *l)  {
    qCritical() << "Error: " << l;
}