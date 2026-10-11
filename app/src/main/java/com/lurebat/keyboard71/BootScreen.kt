package com.lurebat.keyboard71

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import com.jormy.nin.NINLib

/**
 * A terminal-style status screen over the keyboard while the engine starts
 * (first show after the app starts, and the launch page's Restart engine).
 * The GL thread is busy booting the engine then and the keyboard is blank;
 * this view polls NINLib.engineStage from the UI thread and lists the steps.
 * Drawn like the launch page: drafting grid, monospace, amber accents.
 */
class BootScreen(context: Context) : View(context) {
    private class Step(val label: String) {
        var start = 0L
        var end = 0L
    }

    private val density = resources.displayMetrics.density
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE
        textSize = 12 * resources.displayMetrics.scaledDensity
        letterSpacing = 0.05f
    }
    private val grid = Paint().apply { strokeWidth = 1f }
    private val steps = listOf(Step("loading engine"), Step("starting engine"), Step("loading dictionaries + layout"))
    private var watching = false

    init {
        visibility = GONE
    }

    /** Shows the screen if the engine is not ready, until it is. */
    fun watch() {
        if (NINLib.engineStage() == STAGE_READY) return
        steps.forEach { it.start = 0; it.end = 0 }
        animate().cancel()
        alpha = 1f
        visibility = VISIBLE
        if (!watching) {
            watching = true
            post(poll)
        }
    }

    private val poll = object : Runnable {
        override fun run() {
            val stage = NINLib.engineStage()
            val now = SystemClock.uptimeMillis()
            val current = when (stage) {
                STAGE_NO_ENGINE, STAGE_LOADING -> 0
                STAGE_STARTING, STAGE_BOOTED -> 1
                STAGE_INITIALIZING -> 2
                else -> steps.size
            }
            steps.forEachIndexed { i, step ->
                if (i <= current && step.start == 0L) step.start = now
                if (i < current && step.end == 0L) step.end = now
            }
            invalidate()
            if (stage == STAGE_READY) {
                watching = false
                animate().alpha(0f).setStartDelay(150).setDuration(150).withEndAction { visibility = GONE }
            } else {
                postDelayed(this, POLL_MS)
            }
        }
    }

    // Touches would only queue up behind the boot; keep them from the keyboard.
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent) = watching

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(PAPER)
        val minor = 12 * density
        var i = 0
        var x = 0f
        while (x < width) {
            grid.color = if (i++ % 5 == 0) GRID_MAJOR else GRID_MINOR
            canvas.drawLine(x, 0f, x, height.toFloat(), grid)
            x += minor
        }
        i = 0
        var y = 0f
        while (y < height) {
            grid.color = if (i++ % 5 == 0) GRID_MAJOR else GRID_MINOR
            canvas.drawLine(0f, y, width.toFloat(), y, grid)
            y += minor
        }

        val pad = 16 * density
        val line = text.fontSpacing * 1.5f
        y = pad - text.ascent()
        text.color = ACCENT
        canvas.drawText("KEYBOARD 72", pad, y, text)
        text.color = DIM
        canvas.drawText("  ·  NINTYPE ENGINE", pad + text.measureText("KEYBOARD 72"), y, text)
        y += line * 1.3f

        val now = SystemClock.uptimeMillis()
        for (step in steps) {
            if (step.start == 0L) break
            val label = "> ${step.label} "
            text.color = INK
            canvas.drawText(label, pad, y, text)
            if (step.end != 0L) {
                // Dot leaders, then the result right-aligned.
                val result = " [OK] %.2fs".format((step.end - step.start) / 1000f)
                val labelWidth = text.measureText(label)
                val resultWidth = text.measureText(result)
                val dots = ((width - 2 * pad - labelWidth - resultWidth) / text.measureText(".")).toInt()
                text.color = DIM
                canvas.drawText(".".repeat(dots.coerceAtLeast(0)), pad + labelWidth, y, text)
                text.color = OK
                canvas.drawText(result, width - pad - resultWidth, y, text)
            } else if ((now / 500) % 2 == 0L) {
                text.color = ACCENT
                canvas.drawText("█", pad + text.measureText(label), y, text)
            }
            y += line
        }
    }

    private companion object {
        const val POLL_MS = 50L
        // NINLib.engineStage (ninlib_exports.cpp)
        const val STAGE_NO_ENGINE = 0
        const val STAGE_LOADING = 1
        const val STAGE_STARTING = 2
        const val STAGE_BOOTED = 3
        const val STAGE_INITIALIZING = 4
        const val STAGE_READY = 5
        // The launch page's colors (NINActivity.kt)
        const val PAPER = 0xFF0C0F13.toInt()
        const val GRID_MINOR = 0xFF131920.toInt()
        const val GRID_MAJOR = 0xFF1A222B.toInt()
        const val INK = 0xFFD8DEE5.toInt()
        const val DIM = 0xFF7D8793.toInt()
        const val ACCENT = 0xFFF2A93B.toInt()
        const val OK = 0xFF79D29A.toInt()
    }
}
