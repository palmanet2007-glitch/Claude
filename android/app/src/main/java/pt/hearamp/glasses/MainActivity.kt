package pt.hearamp.glasses

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {

    private lateinit var audioManager: AudioManager
    private lateinit var inputSpinner: Spinner
    private lateinit var outputSpinner: Spinner
    private lateinit var startButton: Button
    private lateinit var status: TextView
    private lateinit var routeWarning: TextView
    private var inputs: List<AudioDeviceInfo?> = emptyList()
    private var outputs: List<AudioDeviceInfo?> = emptyList()
    private val handler = Handler(Looper.getMainLooper())

    private val statusUpdater = object : Runnable {
        override fun run() {
            refreshStatus()
            handler.postDelayed(this, 500)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        audioManager = getSystemService(AudioManager::class.java)

        inputSpinner = findViewById(R.id.inputSpinner)
        outputSpinner = findViewById(R.id.outputSpinner)
        startButton = findViewById(R.id.startButton)
        status = findViewById(R.id.status)
        routeWarning = findViewById(R.id.routeWarning)

        requestPermissionsIfNeeded()
        populateDevices()

        outputSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = updateRouteWarning()
            override fun onNothingSelected(p: AdapterView<*>?) = Unit
        }

        findViewById<Spinner>(R.id.presetSpinner).onItemSelectedListener =
            object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                    NativeEngine.setPreset(pos)
                }
                override fun onNothingSelected(p: AdapterView<*>?) = Unit
            }

        startButton.setOnClickListener {
            if (NativeEngine.isRunning()) {
                AmplifierService.stop(this)
            } else if (hasMicPermission()) {
                AmplifierService.start(
                    this,
                    inputs.getOrNull(inputSpinner.selectedItemPosition)?.id ?: 0,
                    outputs.getOrNull(outputSpinner.selectedItemPosition)?.id ?: 0,
                )
            } else {
                Toast.makeText(this, R.string.permission_needed, Toast.LENGTH_LONG).show()
                requestPermissionsIfNeeded()
            }
        }

        findViewById<Switch>(R.id.muteSwitch).setOnCheckedChangeListener { _, on -> NativeEngine.setMuted(on) }
        findViewById<Switch>(R.id.feedbackSwitch).setOnCheckedChangeListener { _, on ->
            NativeEngine.setFeedbackEnabled(on)
        }

        // Volume: -20…+10 dB.
        bindSeek(R.id.volumeSeek, R.id.volumeLabel, offset = -20) { db ->
            NativeEngine.setVolumeDb(db)
            getString(R.string.volume) + ": %+d dB".format(db.toInt())
        }
        // Ganho por banda: -10…+30 dB (limitado no nativo a kMaxOpenEarGainDb).
        val bandSeeks = listOf(R.id.lowSeek, R.id.midSeek, R.id.highSeek)
        val bandLabels = listOf(R.id.lowLabel, R.id.midLabel, R.id.highLabel)
        val bandNames = listOf(R.string.band_low, R.string.band_mid, R.string.band_high)
        val defaults = listOf(4, 14, 18)
        bandSeeks.forEachIndexed { i, seekId ->
            findViewById<SeekBar>(seekId).progress = defaults[i] + 10
            bindSeek(seekId, bandLabels[i], offset = -10) { db ->
                val g = bandSeeks.map { findViewById<SeekBar>(it).progress - 10f }
                NativeEngine.setBandGains(g[0], g[1], g[2])
                getString(bandNames[i]) + ": %+d dB".format(db.toInt())
            }
        }
        // MPO: -24…0 dBFS.
        bindSeek(R.id.mpoSeek, R.id.mpoLabel, offset = -24) { db ->
            NativeEngine.setMpoDbFS(db)
            getString(R.string.mpo) + ": %d dBFS".format(db.toInt())
        }
    }

    override fun onResume() {
        super.onResume()
        populateDevices()
        handler.post(statusUpdater)
    }

    override fun onPause() {
        handler.removeCallbacks(statusUpdater)
        super.onPause()
    }

    private fun bindSeek(seekId: Int, labelId: Int, offset: Int, onChange: (Float) -> String) {
        val seek = findViewById<SeekBar>(seekId)
        val label = findViewById<TextView>(labelId)
        label.text = onChange((seek.progress + offset).toFloat())
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar, progress: Int, fromUser: Boolean) {
                label.text = onChange((progress + offset).toFloat())
            }
            override fun onStartTrackingTouch(s: SeekBar) = Unit
            override fun onStopTrackingTouch(s: SeekBar) = Unit
        })
    }

    private fun populateDevices() {
        inputs = listOf(null) + audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS).toList()
        outputs = listOf(null) + audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            .filter { it.type != AudioDeviceInfo.TYPE_TELEPHONY }
        inputSpinner.adapter = adapterFor(inputs)
        outputSpinner.adapter = adapterFor(outputs)
        // Pré-selecciona óculos/auscultadores LE Audio quando presentes (menor latência sem fios).
        outputs.indexOfFirst { it?.type == AudioDeviceInfo.TYPE_BLE_HEADSET }
            .takeIf { it > 0 }?.let { outputSpinner.setSelection(it) }
        updateRouteWarning()
    }

    private fun adapterFor(devices: List<AudioDeviceInfo?>) = ArrayAdapter(
        this, android.R.layout.simple_spinner_dropdown_item,
        devices.map { d -> if (d == null) getString(R.string.automatic) else "${d.productName} — ${typeName(d.type)}" }
    )

    private fun updateRouteWarning() {
        val out = outputs.getOrNull(outputSpinner.selectedItemPosition)
        routeWarning.text = when (out?.type) {
            AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ->
                "Bluetooth clássico (A2DP): latência típica 100–300 ms. Ouvirá eco do som directo. " +
                    "Inadequado para amplificação em tempo real — use LE Audio, ligação com fios ou DSP nos óculos."
            AudioDeviceInfo.TYPE_BLUETOOTH_SCO ->
                "Bluetooth HFP: banda estreita (voz) e latência elevada. Não recomendado."
            AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLE_SPEAKER ->
                "LE Audio (LC3): latência de transporte da ordem de 20–40 ms; perceptível como eco ligeiro."
            else -> ""
        }
    }

    private fun refreshStatus() {
        val running = NativeEngine.isRunning()
        startButton.text = getString(if (running) R.string.stop else R.string.start)
        status.text = if (!running) "Parado" else buildString {
            val lat = NativeEngine.getLatencyMs()
            append("Taxa: ${NativeEngine.getSampleRate()} Hz\n")
            append("Latência estimada (SO, entrada+saída): ")
            append(if (lat >= 0) "%.1f ms".format(lat) else "indisponível").append('\n')
            append("Latência do DSP: 0 amostras\n")
            append("Entalhes anti-Larsen activos: ${NativeEngine.getActiveNotches()}\n")
            append("Falhas de entrada (underruns): ${NativeEngine.getUnderruns()}")
        }
    }

    private fun typeName(type: Int) = when (type) {
        AudioDeviceInfo.TYPE_BUILTIN_MIC -> "microfone interno"
        AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "altifalante interno"
        AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "auricular interno"
        AudioDeviceInfo.TYPE_WIRED_HEADSET, AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "com fios"
        AudioDeviceInfo.TYPE_USB_HEADSET, AudioDeviceInfo.TYPE_USB_DEVICE -> "USB"
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "Bluetooth A2DP"
        AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "Bluetooth HFP"
        AudioDeviceInfo.TYPE_BLE_HEADSET, AudioDeviceInfo.TYPE_BLE_SPEAKER -> "LE Audio"
        AudioDeviceInfo.TYPE_HEARING_AID -> "aparelho auditivo (ASHA)"
        else -> "tipo $type"
    }

    private fun hasMicPermission() =
        checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    private fun requestPermissionsIfNeeded() {
        val wanted = buildList {
            add(Manifest.permission.RECORD_AUDIO)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) add(Manifest.permission.BLUETOOTH_CONNECT)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) add(Manifest.permission.POST_NOTIFICATIONS)
        }.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (wanted.isNotEmpty()) requestPermissions(wanted.toTypedArray(), 1)
    }
}
