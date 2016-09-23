#include "androidnativewrapper.h"
#include "midiprerequisites.h"
#include "androidmidimanager.h"
#include "androidmidiinputdevice.h"
#include "jnienvironment.h"
#include "jniobject.h"
#include "midisystem.h"

#include <jni.h>
#include <android/log.h>
#include <sstream>

namespace midi {

AndroidMidiManager &androidManager() {
    return static_cast<AndroidMidiManager&>(manager());
}

void java_sendMidiMessage(JNIEnv *env, jobject thiz, jstring deviceName, jint eventId, jint byte1, jint byte2) {
    MIDI_UNUSED(env);
    MIDI_UNUSED(thiz);
    const char* raw = env->GetStringUTFChars(deviceName, 0);
    androidManager().receiveMidiEvent(raw, eventId, byte1, byte2);
    env->ReleaseStringUTFChars(deviceName, raw);
}

void java_inputDeviceAttached(JNIEnv *env, jobject thiz, jstring deviceName) {
    MIDI_UNUSED(env);
    MIDI_UNUSED(thiz);
    const char* raw = env->GetStringUTFChars(deviceName, 0);
    androidManager().midiInputDeviceAttached(raw);
    env->ReleaseStringUTFChars(deviceName, raw);
}

void java_inputDeviceDetached(JNIEnv *env, jobject thiz, jstring deviceName) {
    MIDI_UNUSED(env);
    MIDI_UNUSED(thiz);
    const char* raw = env->GetStringUTFChars(deviceName, 0);
    androidManager().midiInputDeviceDetached(raw);
    env->ReleaseStringUTFChars(deviceName, raw);
}

void java_outputDeviceAttached(JNIEnv *env, jobject thiz, jstring deviceName) {
    MIDI_UNUSED(env);
    MIDI_UNUSED(thiz);
    const char* raw = env->GetStringUTFChars(deviceName, 0);
    androidManager().midiOutputDeviceAttached(raw);
    env->ReleaseStringUTFChars(deviceName, raw);
}

void java_outputDeviceDetached(JNIEnv *env, jobject thiz, jstring deviceName) {
    MIDI_UNUSED(env);
    MIDI_UNUSED(thiz);
    const char* raw = env->GetStringUTFChars(deviceName, 0);
    androidManager().midiOutputDeviceDetached(raw);
    env->ReleaseStringUTFChars(deviceName, raw);
}

static JNIClass g_midiClass;
static jmethodID g_methodUsbMidiDriverAdapterInstance = 0;

static JNINativeMethod jniMidiNativeMethods[] = {
    {"java_sendMidiMessage", "(Ljava/lang/String;III)V", (void *)java_sendMidiMessage},
    {"java_inputDeviceAttached", "(Ljava/lang/String;)V", (void *)java_inputDeviceAttached},
    {"java_outputDeviceDetached", "(Ljava/lang/String;)V", (void *)java_outputDeviceDetached},
    {"java_inputDeviceAttached", "(Ljava/lang/String;)V", (void *)java_inputDeviceAttached},
    {"java_outputDeviceDetached", "(Ljava/lang/String;)V", (void *)java_outputDeviceDetached},
};

JNIEXPORT void JNICALL initAndroidManagerJNI(JNIObject *usbmanager) {
    __android_log_print(ANDROID_LOG_VERBOSE, "MIDI", "initAndroidManagerJNI: Attaching");
    JNIEnv* env;
    JNIEnvironment::env(&env);

    __android_log_print(ANDROID_LOG_DEBUG, "MIDI", "initAndroidManagerJNI: CallStaticObjectMethod");
    *usbmanager = std::make_shared<JNIObjectData>(env->CallStaticObjectMethod(g_midiClass->get(), g_methodUsbMidiDriverAdapterInstance));
    if (env->ExceptionCheck()) {
        return;
    }

    __android_log_print(ANDROID_LOG_DEBUG, "MIDI", "initAndroidManagerJNI: NewGlobalRef");
}

JNIEXPORT void JNICALL releaseAndroidManagerJNI(JNIObject usbmanager) {
    MIDI_UNUSED(usbmanager);
    JNIEnv* env;
    JNIEnvironment::env(&env);

    __android_log_print(ANDROID_LOG_VERBOSE, "MIDI", "exitAndroidManagerJNI");
}

std::vector<std::string> android_listAvailableInputDevices(JNIObject usbmanager) {
    std::vector<std::string> res;
    if (!usbmanager) {
        __android_log_print(ANDROID_LOG_ERROR, "MIDI", "Usbmanager is null");
        return res;
    }

    JNIEnv* env;
    JNIEnvironment::env(&env);

    jmethodID method = env->GetMethodID(g_midiClass->get(), "getInputDeviceNames", "()Ljava/lang/String;");
    jstring names = static_cast<jstring>(env->CallObjectMethod(usbmanager->get(), method));
    const char* raw = env->GetStringUTFChars(names, 0);
    std::istringstream namesfill(raw);
    env->ReleaseStringUTFChars(names, raw);

    // split namesfill
    std::string temp;
    while (std::getline(namesfill, temp)) {
        res.push_back(temp);
    }

    return res;
}

MidiManager::MidiInDevRes android_createInputDevice(const std::string &id, JNIObject usbmanager) {
    JNIEnv* env;
    JNIEnvironment::env(&env);

    jmethodID method = env->GetMethodID(g_midiClass->get(), "connectInputDevice", "(Ljava/lang/String;)Z");
    jstring jid = env->NewStringUTF(id.c_str());
    jboolean res = env->CallBooleanMethod(usbmanager->get(), method, jid);
    env->DeleteLocalRef(jid);
    if (!res) {
        return std::make_pair(MIDI_INPUT_DEVICE_ID_NOT_FOUND, MidiInputDevicePtr());
    }
    return std::make_pair(OK, std::make_shared<AndroidMidiInputDevice>(std::make_shared<MidiDeviceIdentifier>(INPUT, id), usbmanager));
}

MidiResult android_deleteInputDevice(const std::string &id, JNIObject usbmanager) {
    JNIEnv* env;
    JNIEnvironment::env(&env);

    jmethodID method = env->GetMethodID(g_midiClass->get(), "disconnectInputDevice", "(Ljava/lang/String;)Z");
    jstring jid = env->NewStringUTF(id.c_str());
    jboolean res = env->CallBooleanMethod(usbmanager->get(), method, jid);
    env->DeleteLocalRef(jid);
    if (!res) {
        return MIDI_INPUT_DEVICE_ID_NOT_FOUND;
    }
    return OK;
}

bool android_registerNatives(JNIEnv *env) {
    __android_log_print(ANDROID_LOG_VERBOSE, "MIDI", "Searching for UsbMidiDriver");

    // search for UsbMidiDriver class
    midi::g_midiClass = std::make_shared<JNIClassData>(env->FindClass("org/modules/midi/UsbMidiDriverAdapter"));
    if (!g_midiClass) {
        __android_log_print(ANDROID_LOG_ERROR, "MIDI", "Failed to get usb midi driver adapter for MIDI");
        return false;
    }

    midi::g_methodUsbMidiDriverAdapterInstance =
                env->GetStaticMethodID(midi::g_midiClass->get(), "instance", "()Lorg/modules/midi/UsbMidiDriverAdapter;");


    __android_log_print(ANDROID_LOG_VERBOSE, "MIDI", "RegisterNatives");

    // register our native methods
    if (env->RegisterNatives(midi::g_midiClass->get(), midi::jniMidiNativeMethods, sizeof(midi::jniMidiNativeMethods) / sizeof(midi::jniMidiNativeMethods[0])) < 0)
    {
        __android_log_print(ANDROID_LOG_ERROR, "MIDI", "Failed to register natives JNI for MIDI");
        return false;
    }

    __android_log_print(ANDROID_LOG_VERBOSE, "MIDI", "Connected to JNI");

    return true;
}

}  // namespace midi