package pt.hearamp.glasses

/** Interface para o motor de áudio nativo (C++/Oboe). Instância única no processo. */
object NativeEngine {
    init {
        System.loadLibrary("hearamp_jni")
    }

    /** Ordem igual a hearamp::Preset (core/include/hearamp/fitting.hpp). */
    enum class Preset { CONVERSATION, RESTAURANT, TELEVISION, OUTDOOR }

    /** deviceId = 0 -> encaminhamento automático do sistema. */
    external fun start(inputDeviceId: Int, outputDeviceId: Int): Boolean
    external fun stop()
    external fun isRunning(): Boolean

    external fun setPreset(preset: Int)
    external fun setBandGains(lowDb: Float, midDb: Float, highDb: Float)
    external fun setVolumeDb(db: Float)
    external fun setMuted(muted: Boolean)
    external fun setMpoDbFS(db: Float)
    external fun setFeedbackEnabled(enabled: Boolean)

    /** Limiares (dB HL) a 250, 500, 1000, 2000, 4000 e 8000 Hz. */
    external fun applyAudiogram(thresholdsDbHL: FloatArray)

    /** Latência entrada+saída estimada pelo sistema (ms), ou -1 se indisponível. */
    external fun getLatencyMs(): Double
    external fun getSampleRate(): Int
    external fun getActiveNotches(): Int
    external fun getUnderruns(): Int
}
