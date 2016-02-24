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

#include "settingsforqt.h"
#include <assert.h>
#include "../core/audio/recorder/audiorecorderadapter.h"
#include "donotshowagainmessagebox.h"
#include "options/optionsdialog.h"


const QString SettingsForQt::KEY_CURRENT_FILE_DIALOG_PATH = "app/currentFileDialogPath";

SettingsForQt::SettingsForQt()
    : mApplicationRuns(0),
      mLastVisitedOptionsPage(options::OptionsDialog::PAGE_ENVIRONMENT) {
}

SettingsForQt::~SettingsForQt()
{

}


SettingsForQt &SettingsForQt::getSingleton() {
    return static_cast<SettingsForQt&>(Settings::getSingleton());
}


void SettingsForQt::load() {
    mApplicationRuns = mSettings.value("app/runs", 0).toLongLong();
    mLanguageId = mSettings.value("app/languageId", QString()).toString().toStdString();

    mLastVisitedOptionsPage = mSettings.value("app/lastOptionsPage", options::OptionsDialog::PAGE_ENVIRONMENT).toInt();

    mInputDeviceName = mSettings.value("audio/inputDeviceName", QString()).toString();
    mInputDeviceSamplingRate = mSettings.value("audio/inputDeviceSamplingRate", 44100).toInt();

    mOutputDeviceName = mSettings.value("audio/outputDeviceName", QString()).toString();
    mOutputDeviceSamplingRate = mSettings.value("audio/outputDeviceSamplingRate", 22050).toInt();


    mSoundGeneratorMode = static_cast<SoundGenerator::SoundGeneratorMode>(
                mSettings.value("core/soundGeneratorMode", static_cast<int>(SoundGenerator::SGM_SYNTHESIZE_KEY)).toInt());
    mSoundGeneratorVolumeDynamic = mSettings.value("core/soundGeneratorVolumeDynamic", true).toBool();
    mStroboscopeActive = mSettings.value("core/stroboscopeMode", true).toBool();
    mDisableAutomaticKeySelection = mSettings.value("core/disableAutomaticKeySelection", false).toBool();
}

qlonglong SettingsForQt::getApplicationRuns() const {
    return mApplicationRuns;
}

void SettingsForQt::increaseApplicationRuns() {
    ++mApplicationRuns;
    mSettings.setValue("app/runs", mApplicationRuns);
}

void SettingsForQt::setLanguageId(const std::string &s) {
    Settings::setLanguageId(s);
    mSettings.setValue("app/languageId", QString::fromStdString(mLanguageId));
}

std::string SettingsForQt::getUserLanguageId() const {
    if (mLanguageId.size() > 0) {
        return mLanguageId;
    } else {
        return QLocale::system().name().left(2).toStdString();
    }
}

int SettingsForQt::doNotShowAgainMessageBox(int id) const {
   if (mSettings.value(QString("doNotShowAgain/id%1").arg(id), false).toBool() == true) {
       return mSettings.value(QString("doNotShowAgain/id%1_value").arg(id), -1).toInt();
   }
   return -1;
}

void SettingsForQt::setDoNotShowAgainMessageBox(int id, bool doNotShowAgain, int value) {
    mSettings.setValue(QString("doNotShowAgain/id%1").arg(id), doNotShowAgain);
    mSettings.setValue(QString("doNotShowAgain/id%1_value").arg(id), value);
}

void SettingsForQt::clearAllDoNotShowAgainMessageBoxes() {
    for (int i = 0; i < DoNotShowAgainMessageBox::COUNT; ++i) {
        setDoNotShowAgainMessageBox(i, false, -1);
    }
}

int SettingsForQt::countActiveDoNotShowAgainMessageBoxes() const {
    int out = 0;
    for (int i = 0; i < DoNotShowAgainMessageBox::COUNT; ++i) {
        if (doNotShowAgainMessageBox(i) >= 0) {
            out += 1;
        }
    }
    return out;
}

void SettingsForQt::setLastVisitedOptionsPage(int id) {
    mLastVisitedOptionsPage = id;
    mSettings.setValue("app/lastOptionsPage", mLastVisitedOptionsPage);
}

void SettingsForQt::setInputDeviceName(const QString &s) {
    mInputDeviceName = s;
    mSettings.setValue("audio/inputDeviceName", mInputDeviceName);
}

void SettingsForQt::setInputDeviceSamplingRate(int rate) {
    mInputDeviceSamplingRate = rate;
    mSettings.setValue("audio/inputDeviceSamplingRate", mInputDeviceSamplingRate);
}

void SettingsForQt::setOutputDeviceName(const QString &s) {
    mOutputDeviceName = s;
    mSettings.setValue("audio/outputDeviceName", mOutputDeviceName);
}

void SettingsForQt::setOutputDeviceSamplingRate(int rate) {
    mOutputDeviceSamplingRate = rate;
    mSettings.setValue("audio/outputDeviceSamplingRate", mOutputDeviceSamplingRate);
}

void SettingsForQt::setSoundGeneratorMode(SoundGenerator::SoundGeneratorMode mode) {
    Settings::setSoundGeneratorMode(mode);
    mSettings.setValue("core/soundGeneratorMode", static_cast<int>(mode));
}

void SettingsForQt::setSoundGeneratorVolumeDynamic(bool dynamic) {
    Settings::setSoundGeneratorVolumeDynamic(dynamic);
    mSettings.setValue("core/soundGeneratorVolumeDynamic", dynamic);
}

void SettingsForQt::setStroboscopeMode(bool enable) {
    Settings::setStroboscopeMode(enable);
    mSettings.setValue("core/stroboscopeMode", enable);
}

void SettingsForQt::setDisableAutomaticKeySelection(bool disable) {
    Settings::setDisableAutomaticKeySelection(disable);
    mSettings.setValue("core/disableAutomaticKeySelection", disable);
}

double SettingsForQt::getAudioPlayerBufferSize() const {
    return mSettings.value("audio/playerBufferSize", AudioPlayerAdapter::DefaultBufferSizeMilliseconds).toDouble();
}

void SettingsForQt::setAudioPlayerBufferSize(double d) {
    mSettings.setValue("audio/playerBufferSize", d);
}

int SettingsForQt::getAudioPlayerChannelsCount() const {
    return mSettings.value("audio/playerChannels", 2).toInt();
}

void SettingsForQt::setAudioPlayerChannelsCount(int i) {
    mSettings.setValue("audio/playerChannels", i);
}
