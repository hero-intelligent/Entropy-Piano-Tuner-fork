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

#include "tunerapplication.h"
#include <iostream>
#include <QDebug>
#include <QThread>
#include <QMediaPlayer>
#include <QFile>
#include <QResource>
#include <QFileOpenEvent>
#include <QStandardPaths>
#include <QScreen>
#include "../core/config.h"
#include "../core/messages/messagehandler.h"
#include "filemanagerforqt.h"
#include "platformtools.h"
#include "projectmanagerforqt.h"
#include "logforqt.h"
#include "settingsforqt.h"
#include "../core/system/eptexception.h"

TunerApplication *TunerApplication::mSingleton(nullptr);

TunerApplication::TunerApplication(int & argc, char ** argv)
    : QApplication(argc, argv),
      mMessageHandlerTimerId(0),
      mAudioRecorder(this),
      mAudioPlayer(this) {

    EptAssert(!mSingleton, "Singleton class already created");
    mSingleton = this;

    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << ":/media" << ":/media/icons");

    if (primaryScreen()->devicePixelRatio() >= 1.5) {
        setAttribute(Qt::AA_UseHighDpiPixmaps);
    }

    new FileManagerForQt();
}

TunerApplication::~TunerApplication()
{
    stop();
    exit();
    mCore.reset();
    platformtools::enableScreensaver();

    mSingleton = nullptr;
}

TunerApplication &TunerApplication::getSingleton() {
    EptAssert(mSingleton, "Class has to be created");
    return *mSingleton;
}

TunerApplication *TunerApplication::getSingletonPtr() {
    return mSingleton;
}

void TunerApplication::init() {

    // open the main window with the startup file
    mMainWindow.reset(new MainWindow());

    auto log = new LogForQt();
    // writeout args to log
    INFORMATION("Number of arguments: %d", arguments().size());
    INFORMATION("Program arguments: %s", arguments().join(", ").toStdString().c_str());

    // create core
    mCore.reset(new Core(
                    new ProjectManagerForQt(mMainWindow.get()),
                    &mAudioRecorder,
                    &mAudioPlayer,
                    log));

    platformtools::disableScreensaver();


    EptAssert(mCore, "Core has to be created before entering init");

    // init the window
    mMainWindow->init(mCore.get());

    // then init the core
    initCore();
}

void TunerApplication::exit() {
    stop();

    if (!mCore) {return;}
    mCore->exit();
}

void TunerApplication::start() {
    MessageHandler::getSingleton().process();
    startCore();

    mMainWindow->start();

    QObject::connect(this, SIGNAL(applicationStateChanged(Qt::ApplicationState)),
                     this, SLOT(onApplicationStateChanged(Qt::ApplicationState)));




    // if the user opened this program by double clicking a file, we can set this file
    // as startup file, that is loaded when the program has started.
    // if startupFile is empty, we open the empty default file
    if (mStartupFile.size() > 0) {
        openFile(mStartupFile, false);
        mStartupFile.clear();
    } else {
        platformtools::loadStartupFile(arguments());
    }
}

void TunerApplication::stop() {
    if (!mCore) {return;}
    mCore->stop();
}

void TunerApplication::playStartupSound() {
    // play a startup sound

    // first copy startup sound from resource location to disk
    // it cannot be played out a resource file
    QFile audioFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/startup_sound.mp3");
    if (!audioFile.exists()) {
        // file does not exits yet, copy it
        QFile::copy(":/media/audio/startup_sound.mp3", audioFile.fileName());
    } else {
        if (audioFile.size() != QResource(":/media/audio/startup_sound.mp3").size()) {
            // size changed, this usually means a new size, remove and copy
            audioFile.remove();
            QFile::copy(":/media/audio/startup_sound.mp3", audioFile.fileName());
        }
    }

    // file should exist now
    EptAssert(audioFile.exists(), "Audio file should exist now");

    // play the actual sound
    QMediaPlayer *player = new QMediaPlayer(this);
    player->setMedia(QUrl::fromLocalFile(audioFile.fileName()));
    player->setVolume(50);
    player->play();
}

bool TunerApplication::openFile(QString filePath, bool cached) {
    if (!mCore || !mCore->getProjectManager() || !mCore->isInitialized()) {
        // not initiated, save file for later use
        INFORMATION("Storing startup file: %s", filePath.toStdString().c_str());
        mStartupFile = filePath;
        return true;
    }
    return mCore->getProjectManager()->openFile(filePath.toStdString(), cached) == ProjectManagerAdapter::R_ACCEPTED;
}

bool TunerApplication::event(QEvent *e) {
    switch (e->type()) {
    case QEvent::FileOpen:
        // note: this only supported in Mac OS X so far!
        return openFile(static_cast<QFileOpenEvent *>(e)->file(), false);
    default:
        return QApplication::event(e);
    }
}

void TunerApplication::timerEvent(QTimerEvent *event) {
    MessageHandler::getSingleton().process();
    (void)event; // event not used, suppress warning
}

bool TunerApplication::notify(QObject* receiver, QEvent* event) {
    try {
        return QApplication::notify(receiver, event);
    }
    catch (const EptException &e) {
        qCritical() << "Unhandled exception: ";
        qCritical() << QString::fromStdString(e.getFullDescription());

    }
    catch (const std::exception &e) {
        qCritical() << "Unhandled exception: ";
        qCritical() << QString::fromStdString(e.what());
    }
    catch (...) {
        qCritical() << "Unhandled exception: ";
        qCritical() << "unknown exception";
    }
  return true;
}

void TunerApplication::initCore() {
    if (mCore && !mCore->isInitialized()) {
        // disable main window during init
        mMainWindow->setEnabled(false);

        mCore->init(new QtCoreInitialisation(mMainWindow.get()));

        // enable the main dialog again
        mMainWindow->setEnabled(true);

        // recativate window after closing the dialog
        mMainWindow->activateWindow();
    }
}

void TunerApplication::exitCore() {
    if (mCore && mCore->isInitialized()) {
        mCore->exit();
    }

}

void TunerApplication::startCore() {
    if (mCore) {
        mCore->start();
    }


    // custom message loop
    mMessageHandlerTimerId = startTimer(10);
}

void TunerApplication::stopCore() {
    if (mCore) {
        mCore->stop();
    }

    // kill the custom timer
    if (mMessageHandlerTimerId) {
        killTimer(mMessageHandlerTimerId);
        mMessageHandlerTimerId = 0;
    }
}

void TunerApplication::onApplicationStateChanged(Qt::ApplicationState state) {
    if (state & Qt::ApplicationSuspended) {
        // called if application is 'shut down'
        INFORMATION("Application suspended: exiting core");
        stopCore();
        exitCore();
    } else if (state & Qt::ApplicationActive) {
        // init and start core components
        INFORMATION("Application gone active: starting core");
        initCore();
        startCore();
    } else if (state & Qt::ApplicationHidden ) {
        // delete core components
        INFORMATION("Application gone hidden: exiting core");
        exitCore();
    } else if (state & Qt::ApplicationInactive) {
        // stop the core on mobile platforms
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS) || defined(Q_OS_WINPHONE)
        INFORMATION("Application gone inactive: stopping core");
        stopCore();
#endif
    }
}