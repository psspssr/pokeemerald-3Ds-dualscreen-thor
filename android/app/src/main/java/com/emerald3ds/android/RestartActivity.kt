package com.emerald3ds.android

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.os.Process
import android.os.SystemClock
import java.io.File

/**
 * Restarts the app in a fresh process. It runs in its own process (":restart")
 * so it can end the game process, whose native globals cannot be reset, and
 * then launch the game again.
 */
class RestartActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pid = intent.getIntExtra(EXTRA_PID, -1)
        if (pid > 0 && pid != Process.myPid()) {
            Process.killProcess(pid)
            val deadline = SystemClock.uptimeMillis() + 3000
            while (File("/proc/$pid").exists() && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(20)
        }
        startActivity(
            Intent(this, GameActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        )
        finish()
        Process.killProcess(Process.myPid())
    }

    companion object {
        private const val EXTRA_PID = "pid"

        fun restart(from: Activity) {
            NativeBridge.setState(NativeBridge.STATE_PAUSED)
            from.startActivity(Intent(from, RestartActivity::class.java).putExtra(EXTRA_PID, Process.myPid()))
        }
    }
}
