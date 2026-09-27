// Ponte JNI entre a UI Kotlin (pt.hearamp.glasses.NativeEngine) e o motor nativo.
#include <jni.h>

#include "AudioEngine.h"

namespace {
AudioEngine& engine() { return AudioEngine::instance(); }
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_pt_hearamp_glasses_NativeEngine_start(JNIEnv*, jobject, jint inputId, jint outputId) {
    return engine().start(inputId, outputId) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_stop(JNIEnv*, jobject) { engine().stop(); }

JNIEXPORT jboolean JNICALL Java_pt_hearamp_glasses_NativeEngine_isRunning(JNIEnv*, jobject) {
    return engine().isRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_setPreset(JNIEnv*, jobject, jint preset) {
    engine().setPreset(static_cast<hearamp::Preset>(preset));
}

JNIEXPORT void JNICALL
Java_pt_hearamp_glasses_NativeEngine_setBandGains(JNIEnv*, jobject, jfloat low, jfloat mid, jfloat high) {
    engine().setBandGains(low, mid, high);
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_setVolumeDb(JNIEnv*, jobject, jfloat db) {
    engine().setVolumeDb(db);
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_setMuted(JNIEnv*, jobject, jboolean muted) {
    engine().setMuted(muted == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_setMpoDbFS(JNIEnv*, jobject, jfloat db) {
    engine().setMpoDbFS(db);
}

JNIEXPORT void JNICALL Java_pt_hearamp_glasses_NativeEngine_setFeedbackEnabled(JNIEnv*, jobject, jboolean on) {
    engine().setFeedbackEnabled(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_pt_hearamp_glasses_NativeEngine_applyAudiogram(JNIEnv* env, jobject, jfloatArray thresholds) {
    if (env->GetArrayLength(thresholds) != 6) return;
    hearamp::Audiogram a;
    env->GetFloatArrayRegion(thresholds, 0, 6, a.thresholdsDbHL.data());
    engine().applyAudiogram(a);
}

JNIEXPORT jdouble JNICALL Java_pt_hearamp_glasses_NativeEngine_getLatencyMs(JNIEnv*, jobject) {
    return engine().estimatedLatencyMs();
}

JNIEXPORT jint JNICALL Java_pt_hearamp_glasses_NativeEngine_getSampleRate(JNIEnv*, jobject) {
    return engine().sampleRate();
}

JNIEXPORT jint JNICALL Java_pt_hearamp_glasses_NativeEngine_getActiveNotches(JNIEnv*, jobject) {
    return engine().activeNotches();
}

JNIEXPORT jint JNICALL Java_pt_hearamp_glasses_NativeEngine_getUnderruns(JNIEnv*, jobject) {
    return engine().underruns();
}

}  // extern "C"
