package info.cemu.cemu.emulation

internal enum class EmulationLifecycleCommand {
    PAUSE,
    RESUME,
}

/**
 * Keeps the native title running only while Android is resumed and the main rendering surface is
 * available. The native title starts in the running state, so the first command after launch is
 * only needed when launch completes in the background or without a surface.
 */
internal class EmulationLifecycleController {
    private var isActivityResumed = false
    private var isMainSurfaceAvailable = false
    private var isTitleLaunched = false
    private var isNativeTitleRunning = false

    @Synchronized
    fun onActivityResumed(): EmulationLifecycleCommand? {
        isActivityResumed = true
        return reconcile()
    }

    @Synchronized
    fun onActivityPaused(): EmulationLifecycleCommand? {
        isActivityResumed = false
        return reconcile()
    }

    @Synchronized
    fun onMainSurfaceAvailable(): EmulationLifecycleCommand? {
        isMainSurfaceAvailable = true
        return reconcile()
    }

    @Synchronized
    fun onMainSurfaceDestroyed(): EmulationLifecycleCommand? {
        isMainSurfaceAvailable = false
        return reconcile()
    }

    @Synchronized
    fun onTitleLaunched(): EmulationLifecycleCommand? {
        isTitleLaunched = true
        isNativeTitleRunning = true
        return reconcile()
    }

    private fun reconcile(): EmulationLifecycleCommand? {
        if (!isTitleLaunched) {
            return null
        }

        val shouldRun = isActivityResumed && isMainSurfaceAvailable
        if (shouldRun == isNativeTitleRunning) {
            return null
        }

        isNativeTitleRunning = shouldRun
        return if (shouldRun) {
            EmulationLifecycleCommand.RESUME
        } else {
            EmulationLifecycleCommand.PAUSE
        }
    }
}
