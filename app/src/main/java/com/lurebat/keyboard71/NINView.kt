package com.lurebat.keyboard71

import android.content.Context
import android.content.res.Configuration
import android.graphics.PixelFormat
import android.opengl.EGL14
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.GestureDetector
import android.view.GestureDetector.SimpleOnGestureListener
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import com.jormy.nin.EXSurfaceView
import com.jormy.nin.NINLib
import com.jormy.nin.NINLib.init
import com.jormy.nin.NINLib.onTouchEvent
import com.jormy.nin.NINLib.step
import com.jormy.nin.NINLib.syncTiming
import com.jormy.nin.Utils.prin
import com.jormy.nin.Utils.tracedims
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.locks.LockSupport
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10
import kotlin.math.min
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

class NINView(context: Context) : EXSurfaceView(context) {
    var desiredRoenFullscreen = false
    var desiredRoenPixelHeight = 800.0f
    var desiredRoenPixelWidth = 640.0f
    var xViewScaling = 1.0f
    var yViewScaling = 1.0f
    private val preferences = Preferences(context)

    // The haptic setting is checked for every pointer of every touch event.
    // Follow it as it changes instead of blocking the UI thread on a
    // DataStore read each time.
    @Volatile
    private var haptics = preferences.hapticsBlocking()
    private var preferenceScope: CoroutineScope? = null

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        haptics = preferences.hapticsBlocking()
        preferenceScope = CoroutineScope(Dispatchers.Main.immediate).also { scope ->
            scope.launch { preferences.haptics.collect { haptics = it } }
        }
    }

    override fun onDetachedFromWindow() {
        preferenceScope?.cancel()
        preferenceScope = null
        super.onDetachedFromWindow()
    }

    private val gestureDetector = GestureDetector(getContext(), object : SimpleOnGestureListener() {
        override fun onLongPress(e: MotionEvent) {
            super.onLongPress(e)
            if (haptics != Preferences.Haptics.OFF) {
                this@NINView.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
            }
        }
    })

    init {
        syncTiming(System.currentTimeMillis())
        movementEventsQueue = ConcurrentLinkedQueue()
        holder.setFormat(PixelFormat.TRANSLUCENT)
        if (globalContextFactory == null) {
            globalContextFactory = ContextFactory()
        }
        setEGLContextFactory(globalContextFactory)
        setEGLConfigChooser(ConfigChooser(8, 8, 8, 8, 0, 0))
        setRenderer(Renderer())
        setPreserveEGLContextOnPause(true)
        globalView = this
        val metrics = resources.displayMetrics
        devicePPI = metrics.xdpi
        devicePortraitWidth = Math.min(metrics.widthPixels, metrics.heightPixels).toFloat()
        restoreSize()
        onResume()
    }

    // The engine sets the keyboard's size only once it has started; until then
    // the view used an 800-unit default. Start at the size it last asked for
    // (per orientation: the same units give different heights) instead.
    private val sizePrefs
        get() = context.getSharedPreferences("keyboard_size", Context.MODE_PRIVATE)
    private val orientationKey
        get() = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) "landscape" else "portrait"

    private fun restoreSize() {
        val key = orientationKey
        desiredRoenPixelHeight = sizePrefs.getFloat("height_$key", desiredRoenPixelHeight)
        desiredRoenFullscreen = sizePrefs.getBoolean("fullscreen_$key", desiredRoenFullscreen)
        desiredScaling = sizePrefs.getFloat("scaling", desiredScaling)
        requestedHeight = desiredRoenPixelHeight
        requestedFullscreen = desiredRoenFullscreen
        requestedScaling = desiredScaling
    }

    private fun saveSize() {
        val key = orientationKey
        sizePrefs.edit()
            .putFloat("height_$key", desiredRoenPixelHeight)
            .putBoolean("fullscreen_$key", desiredRoenFullscreen)
            .putFloat("scaling", desiredScaling)
            .apply()
    }

    class ContextFactory : EGLContextFactory {
        // com.jormy.nin.EXSurfaceView.EGLContextFactory
        override fun createContext(
            egl10: EGL10,
            display: EGLDisplay,
            eglConfig: EGLConfig
        ): EGLContext {
            if (eglContext == null) {
                Log.w(TAG, "creating OpenGL ES 2.0 context")
                checkEglError("Before eglCreateContext", egl10)
                val context = egl10.eglCreateContext(
                    display,
                    eglConfig,
                    EGL10.EGL_NO_CONTEXT,
                    intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE)
                )
                checkEglError("After eglCreateContext", egl10)
                eglContext = context
                eglDisplay = display
                return context
            }
            if (display !== eglDisplay) {
                prin("BUT THE DISPLAY IS FUCKING DIFFERENT!")
                Thread.currentThread()
                Thread.dumpStack()
                System.exit(1)
            }
            return eglContext!!
        }

        // com.jormy.nin.EXSurfaceView.EGLContextFactory
        override fun destroyContext(egl: EGL10, display: EGLDisplay, context: EGLContext) {
            prin("--------------------- Destroy context, but nope")
            prin("Exiting, because we need to recreate the OGL Context!")
            egl.eglDestroyContext(display, context)
            Thread.currentThread()
            Thread.dumpStack()
            System.exit(1)
        }

        companion object {
            var eglContext: EGLContext? = null
            var eglDisplay: EGLDisplay? = null
        }
    }

    data class ConfigChooser(
        val redSize: Int,
        val greenSize: Int,
        val blueSize: Int,
        val alphaSize: Int,
        val minDepthSize: Int,
        val minStencilSize: Int
    ) : EGLConfigChooser {
        override fun chooseConfig(egl: EGL10, display: EGLDisplay): EGLConfig {
            val numConfigsArr = IntArray(1)
            egl.eglChooseConfig(display, attributes, null, 0, numConfigsArr)
            val numConfigs = numConfigsArr[0]
            require(numConfigs > 0) { "No configs match configSpec" }
            val configs = arrayOfNulls<EGLConfig>(numConfigs)
            egl.eglChooseConfig(display, attributes, configs, numConfigs, numConfigsArr)
            return chooseConfig(egl, display, configs)
        }

        private fun chooseConfig(egl: EGL10, display: EGLDisplay, configs: Array<EGLConfig?>): EGLConfig {
            for (config in configs) {
                val d = egl.findConfigAttrib(display, config, EGL14.EGL_DEPTH_SIZE)
                val s = egl.findConfigAttrib(display, config, EGL14.EGL_STENCIL_SIZE)
                if (d < minDepthSize || s < minStencilSize) continue

                val r = egl.findConfigAttrib(display, config, EGL14.EGL_RED_SIZE)
                val g = egl.findConfigAttrib(display, config, EGL14.EGL_GREEN_SIZE)
                val b = egl.findConfigAttrib(display, config, EGL14.EGL_BLUE_SIZE)
                val a = egl.findConfigAttrib(display, config, EGL14.EGL_ALPHA_SIZE)
                if (r != redSize || g != greenSize || b != blueSize || a != alphaSize) continue

                if (config == null) continue

                return config
            }
            throw IllegalArgumentException("No config chosen")
        }

        private fun EGL10.findConfigAttrib(
            display: EGLDisplay,
            config: EGLConfig?,
            attribute: Int,
        ): Int = intArrayOf(0).let {
            return if (eglGetConfigAttrib(display, config, attribute, it)) {
                it[0]
            } else {
                0
            }
        }

        companion object {
            private val attributes = intArrayOf(
                EGL14.EGL_RED_SIZE,
                4,
                EGL14.EGL_GREEN_SIZE,
                4,
                EGL14.EGL_BLUE_SIZE,
                4,
                EGL14.EGL_RENDERABLE_TYPE,
                EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_NONE
            )
        }
    }

    val desiredPixelWidth: Float
        get() = desiredPixelWidthFor(desiredScaling)

    private fun desiredPixelWidthFor(scaling: Float): Float {
            val metrics = resources.displayMetrics
            val width = metrics.widthPixels.toFloat()
            val height = metrics.heightPixels.toFloat()
            val ppi = metrics.xdpi
            val portraitPixels = min(width, height)
            val portraitInches = portraitPixels / ppi
            val pixelPerfectRoenPixelWidth = desiredRoenPixelWidth * (portraitInches / 1.9631902f)
            val desiredPortrait = pixelPerfectRoenPixelWidth / scaling
            lastDesiredPortrait = desiredPortrait
            return if (width <= height) desiredPortrait else desiredPortrait * (width / height)
        }

    // android.view.View
    override fun onTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        val actionId = event.actionMasked
        val pointerCount = event.pointerCount
        for (i in 0 until pointerCount) {
            doHapticFeedback(actionId)
            if ((actionId == MotionEvent.ACTION_POINTER_UP || actionId == MotionEvent.ACTION_POINTER_DOWN) && i != event.actionIndex) {
                continue
            }
            movementEventsQueue.add(
                RelayTouchInfo(
                    event.getPointerId(i),
                    event.getX(i) / xViewScaling,
                    event.getY(i) / yViewScaling,
                    event.getPressure(i),
                    event.getSize(i),
                    System.currentTimeMillis(),
                    actionToJormyAction(actionId)
                )
            )
        }
        globalView.requestRender()
        wakeRenderer()
        return true
    }

    private fun doHapticFeedback(actionId: Int) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O_MR1) {
            return
        }
        if (actionId != MotionEvent.ACTION_DOWN && actionId != MotionEvent.ACTION_POINTER_DOWN) {
            return
        }
        // Light is Android's key tap; Strong is its heavier long-press click.
        when (haptics) {
            Preferences.Haptics.OFF -> {}
            Preferences.Haptics.LIGHT -> performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            Preferences.Haptics.STRONG -> performHapticFeedback(HapticFeedbackConstants.LONG_PRESS)
        }
    }

    // android.view.SurfaceView, android.view.View
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val metrics = resources.displayMetrics
        val wwww = (metrics.widthPixels + 2).toFloat()
        val height = (metrics.heightPixels + 2).toFloat()
        tracedims(
            "::::::::onMeasure called --------------- : ",
            widthMeasureSpec.toFloat(),
            heightMeasureSpec.toFloat()
        )
        tracedims("metrics : ", metrics.widthPixels.toFloat(), metrics.heightPixels.toFloat())
        val desiredWidth = desiredPixelWidth
        val actual_scale = desiredWidth / wwww
        prin("desired width : $desiredWidth")
        var rawRoenDesiredHeight = desiredRoenPixelHeight
        if (!desiredRoenFullscreen) {
            rawRoenDesiredHeight = desiredRoenPixelHeight
        }
        prin("rawRoenDesiredHeight : $rawRoenDesiredHeight")
        prin("actual scaling : $actual_scale")
        val desiredHeight = Math.min(rawRoenDesiredHeight, height / wwww * desiredWidth)
        prin("desiredHeight : $desiredHeight")
        val f = wwww / desiredWidth
        yViewScaling = f
        xViewScaling = f
        val height2 = wwww * desiredHeight / desiredWidth
        val holder = holder
        holder?.setFixedSize(desiredWidth.toInt(), desiredHeight.toInt())
        tracedims("setMeasuredDims : ", wwww, height2)
        setMeasuredDimension(wwww.toInt(), height2.toInt())
    }

    class Renderer : EXSurfaceView.Renderer {
        override fun onDrawFrame(gl10: GL10) {
            paceFrozenStep()
            while (true) {
                val rti = movementEventsQueue.poll()
                if (rti != null) {
                    onTouchEvent(
                        rti.touchId,
                        rti.jormyActionId,
                        rti.xPos,
                        rti.yPos,
                        rti.pressureValue,
                        rti.areaValue,
                        rti.timestampLong
                    )
                } else {
                    SoftKeyboard.keyboard?.relayDelayedEvents()
                    if (System.nanoTime() - surfaceChangedNanos > SETTLED_NANOS) {
                        while (true) engineTasks.poll()?.run() ?: break
                    }
                    step()
                    return
                }
            }
        }

        // com.jormy.nin.EXSurfaceView.Renderer
        override fun onSurfaceChanged(gl10: GL10, width: Int, height: Int) {
            surfaceChangedNanos = System.nanoTime()
            surfaceWidth = width
            surfaceHeight = height
            initEngine()
        }

        override fun onSurfaceCreated(gl10: GL10, config: EGLConfig) {}
    }

    companion object {
        // Idle pacing. The renderer is continuous, as in the original app, and
        // the engine must keep stepping while idle: its timers (spacebar
        // messages, freeze countdowns) run inside steps. But a frozen engine
        // draws nothing, so its steps no longer wait for the display and ran
        // at the shim's 250 Hz floor. While frozen, space steps at 60 Hz, the
        // display rate of the phones the engine was written for; a touch or
        // editor event ends the wait at once. Drawing frames are unchanged.
        private const val FROZEN_STEP_NANOS = 16_666_667L
        @Volatile private var engineFrozen = false
        @Volatile private var renderThread: Thread? = null
        private val wakeRequested = AtomicBoolean(false)
        private var lastStepNanos = 0L

        // Work that must run on this thread with the engine initialized, i.e. while
        // the keyboard is shown (backups from the launch page; see Backups.kt).
        // It waits until the keyboard has kept its size for a moment: a fresh
        // keyboard is resized twice, and each resize reloads the board and closes
        // any prompt the work opened.
        val engineTasks = ConcurrentLinkedQueue<Runnable>()
        private const val SETTLED_NANOS = 500_000_000L
        @Volatile private var surfaceChangedNanos = 0L

        @Volatile private var surfaceWidth = 0
        @Volatile private var surfaceHeight = 0

        fun runOnEngine(task: Runnable) {
            engineTasks.add(task)
            wakeRenderer()
        }

        /**
         * Runs [work] with [runOnEngine] and hands its result to [done] on the main
         * thread, or [fallback] if the keyboard does not get to it within [timeoutMs].
         */
        fun <T> callOnEngine(timeoutMs: Long, fallback: T, work: () -> T, done: (T) -> Unit) {
            val main = Handler(Looper.getMainLooper())
            val claimed = AtomicBoolean(false)
            val timeout = Runnable { if (claimed.compareAndSet(false, true)) done(fallback) }
            runOnEngine {
                if (!claimed.compareAndSet(false, true)) return@runOnEngine
                val result = work()
                main.post {
                    main.removeCallbacks(timeout)
                    done(result)
                }
            }
            main.postDelayed(timeout, timeoutMs)
        }

        private fun initEngine() {
            val metrics = globalView.resources.displayMetrics
            init(surfaceWidth, surfaceHeight, metrics.widthPixels, metrics.heightPixels)
        }

        /**
         * Shuts the engine down and boots a fresh one in place (launch page,
         * Extras), so settings read at boot such as fast memory access apply
         * without restarting the app. Runs as an engine task. Returns the boot
         * time in milliseconds.
         */
        fun restartEngine(): Long {
            val start = System.nanoTime()
            NINLib.restartEngine()
            SoftKeyboard.keyboard?.bootScreen?.let { screen -> screen.post { screen.watch(restart = true) } }
            engineFrozen = false
            initEngine()  // boots the new engine
            surfaceChangedNanos = System.nanoTime()  // later tasks wait for it to settle
            SoftKeyboard.keyboard?.resendField()
            return (System.nanoTime() - start) / 1_000_000
        }

        fun wakeRenderer() {
            wakeRequested.set(true)
            renderThread?.let { LockSupport.unpark(it) }
        }

        private fun paceFrozenStep() {
            renderThread = Thread.currentThread()
            if (engineFrozen) {
                val deadline = lastStepNanos + FROZEN_STEP_NANOS
                while (!wakeRequested.get()) {
                    val left = deadline - System.nanoTime()
                    if (left <= 0) break
                    LockSupport.parkNanos(left)
                }
            }
            wakeRequested.set(false)
            lastStepNanos = System.nanoTime()
        }

        var globalContextFactory: ContextFactory? = null
        lateinit var globalView: NINView
        var movementEventsQueue: ConcurrentLinkedQueue<RelayTouchInfo> = ConcurrentLinkedQueue()
        private const val TAG = "NINView"


        var devicePPI = 326.0f

        var devicePortraitWidth = 640.0f

        var desiredScaling = 1.0f
        var lastDesiredPortrait = 640.0f

        fun checkEglError(prompt: String?, egl: EGL10) {
            while (true) {
                when (egl.eglGetError()) {
                    EGL14.EGL_SUCCESS -> {
                        return
                    }
                    else -> {
                        Log.e(TAG, String.format("%s: EGL error: 0x%x", prompt, egl.eglGetError()))
                    }
                }
            }
        }

        fun actionToJormyAction(action: Int): Int {
            return when (action) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> 0
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> 2
                MotionEvent.ACTION_MOVE -> 1
                else -> -1
            }
        }


        fun adjustWantedScaling(scaling: Float) {
            requestedScaling = scaling
            sizeChanged()
        }

        // The engine's latest size request (GL thread). Copied to the size in
        // use (desiredScaling, desiredRoenPixelHeight, desiredRoenFullscreen)
        // by applySize, so a layout pass during the hold below keeps the old size.
        @Volatile private var requestedScaling = 1.0f
        @Volatile private var requestedHeight = 800.0f
        @Volatile private var requestedFullscreen = false

        // While the engine starts, its size requests change within a few
        // hundred ms (e.g. 432 units, then 560 once its settings apply). The
        // boot screen holds layout until they settle, so the keyboard keeps its
        // size instead of jumping; only the last request is applied.
        private val main = Handler(Looper.getMainLooper())
        private var layoutHeld = false
        private val applySize = Runnable {
            layoutHeld = false
            desiredScaling = requestedScaling
            globalView.desiredRoenPixelHeight = requestedHeight
            globalView.desiredRoenFullscreen = requestedFullscreen
            globalView.requestLayout()
            globalView.saveSize()
        }

        /** Holds the engine's size requests; [release] applies the last one after [settleMs]. */
        fun holdLayout() {
            main.removeCallbacks(applySize)
            layoutHeld = true
        }

        fun releaseLayout(settleMs: Long) {
            main.removeCallbacks(applySize)
            main.postDelayed(applySize, settleMs)
        }

        private fun sizeChanged() = main.post { if (!layoutHeld) applySize.run() }


        fun onRoenSignalDirty() {
            globalView.requestRender()
        }


        fun onRoenFrozennessChange(truth: Boolean) {
            engineFrozen = truth
            if (!truth) {
                globalView.requestRender()
            }
        }

        fun adjustKeyboardDimensions(wantedRoenHeight: Float, fullscreen: Boolean) {
            var height = 2.0f * wantedRoenHeight
            if (fullscreen) {
                val metrics = globalView.resources.displayMetrics
                val wantedRatio = (metrics.heightPixels - 240).toFloat() / metrics.widthPixels.toFloat()
                height = globalView.desiredPixelWidthFor(requestedScaling) * wantedRatio
            }
            prin("::::::::::: onAdjustKeyboardDimension : $wantedRoenHeight // $fullscreen -> $height")
            requestedHeight = height
            requestedFullscreen = fullscreen
            sizeChanged()
        }
    }
}
