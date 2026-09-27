package pt.hearamp.glasses

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder

/**
 * Serviço em primeiro plano (tipo "microphone"): mantém a amplificação activa com o
 * ecrã desligado ou a app em segundo plano, como exigido pelo Android 10+ para captura.
 */
class AmplifierService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            NativeEngine.stop()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        startForeground(NOTIFICATION_ID, buildNotification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)
        val input = intent?.getIntExtra(EXTRA_INPUT, 0) ?: 0
        val output = intent?.getIntExtra(EXTRA_OUTPUT, 0) ?: 0
        if (!NativeEngine.start(input, output)) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        NativeEngine.stop()
        super.onDestroy()
    }

    private fun buildNotification(): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, getString(R.string.channel_name), NotificationManager.IMPORTANCE_LOW)
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, AmplifierService::class.java).setAction(ACTION_STOP), PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(R.drawable.ic_hearing)
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, getString(R.string.stop), stop).build())
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "amplification"
        private const val NOTIFICATION_ID = 1
        private const val ACTION_STOP = "pt.hearamp.glasses.STOP"
        private const val EXTRA_INPUT = "input"
        private const val EXTRA_OUTPUT = "output"

        fun start(context: Context, inputDeviceId: Int, outputDeviceId: Int) {
            context.startForegroundService(
                Intent(context, AmplifierService::class.java)
                    .putExtra(EXTRA_INPUT, inputDeviceId)
                    .putExtra(EXTRA_OUTPUT, outputDeviceId)
            )
        }

        fun stop(context: Context) {
            context.startService(Intent(context, AmplifierService::class.java).setAction(ACTION_STOP))
        }
    }
}
