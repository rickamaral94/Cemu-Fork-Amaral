package info.cemu.cemu.emulation

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class EmulationLifecycleControllerTest {
    @Test
    fun `lifecycle changes before launch do not call native title controls`() {
        val controller = EmulationLifecycleController()

        assertNull(controller.onActivityResumed())
        assertNull(controller.onMainSurfaceAvailable())
        assertNull(controller.onActivityPaused())
        assertNull(controller.onMainSurfaceDestroyed())
    }

    @Test
    fun `foreground launch does not issue a redundant resume`() {
        val controller = EmulationLifecycleController()

        assertNull(controller.onActivityResumed())
        assertNull(controller.onMainSurfaceAvailable())
        assertNull(controller.onTitleLaunched())
    }

    @Test
    fun `activity pause and resume issue one command each`() {
        val controller = foregroundLaunchedController()

        assertEquals(EmulationLifecycleCommand.PAUSE, controller.onActivityPaused())
        assertNull(controller.onActivityPaused())
        assertEquals(EmulationLifecycleCommand.RESUME, controller.onActivityResumed())
        assertNull(controller.onActivityResumed())
    }

    @Test
    fun `surface recreation in background waits for activity resume`() {
        val controller = foregroundLaunchedController()

        assertEquals(EmulationLifecycleCommand.PAUSE, controller.onMainSurfaceDestroyed())
        assertNull(controller.onActivityPaused())
        assertNull(controller.onMainSurfaceAvailable())
        assertEquals(EmulationLifecycleCommand.RESUME, controller.onActivityResumed())
    }

    @Test
    fun `title launched in background is paused immediately`() {
        val controller = EmulationLifecycleController()

        assertNull(controller.onMainSurfaceAvailable())
        assertEquals(EmulationLifecycleCommand.PAUSE, controller.onTitleLaunched())
        assertEquals(EmulationLifecycleCommand.RESUME, controller.onActivityResumed())
    }

    private fun foregroundLaunchedController() = EmulationLifecycleController().apply {
        onActivityResumed()
        onMainSurfaceAvailable()
        onTitleLaunched()
    }
}
