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
 * A terminal-style boot log over the keyboard while the engine starts (first
 * show after the app starts, and the launch page's Restart engine). The GL
 * thread is busy booting the engine then and the keyboard is blank; this view
 * polls the engine's start-up stage and log (NINLib.engineStage, NINLib.bootLog,
 * boot_log.h) from the UI thread: the steps with their times, the shim's
 * details, the engine's own log lines in its own colors, and a progress ruler
 * estimated from the previous start. Drawn like the launch page.
 */
class BootScreen(context: Context) : View(context) {
    private class Line(val kind: Char, val ms: Int, val text: String) {
        var doneMs = -1  // steps: when the next step started
    }

    private val density = resources.displayMetrics.density
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE
        textSize = 11 * resources.displayMetrics.scaledDensity
    }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val prefs = context.getSharedPreferences("boot_screen", Context.MODE_PRIVATE)
    private val lines = ArrayList<Line>()
    private var next = 0
    private var elapsedMs = 0
    private var ready = false
    private var watching = false

    init {
        visibility = GONE
    }

    /** Shows the boot log if the engine is not ready, until it is. */
    fun watch() {
        if (NINLib.engineStage() == STAGE_READY) return
        lines.clear()
        next = 0
        elapsedMs = 0
        ready = false
        animate().cancel()
        alpha = 1f
        visibility = VISIBLE
        NINView.holdLayout()
        if (!watching) {
            watching = true
            post(poll)
        }
    }

    private val poll = object : Runnable {
        override fun run() {
            val stage = NINLib.engineStage()
            readLog()
            invalidate()
            if (stage == STAGE_READY) {
                readLog()
                ready = true
                watching = false
                lines.lastOrNull { it.kind == 'S' }?.let { if (it.doneMs < 0) it.doneMs = elapsedMs }
                lines.add(Line('R', elapsedMs, "ready"))
                if (elapsedMs > 0) prefs.edit().putInt(KEY_LAST_MS, elapsedMs).apply()
                NINView.releaseLayout(SETTLE_MS)
                // Ready once the keyboard's first full frame is drawn (ninlib_exports.cpp).
                animate().alpha(0f).setStartDelay(FADE_DELAY_MS).setDuration(150).withEndAction { visibility = GONE }
            } else {
                postDelayed(this, POLL_MS)
            }
        }
    }

    private fun readLog() {
        val raw = String(NINLib.bootLog(next), Charsets.UTF_8).split('\n')
        val header = raw.first().split('\t')
        next = header[0].toIntOrNull() ?: next
        elapsedMs = header.getOrNull(1)?.toIntOrNull() ?: elapsedMs
        for (entry in raw.drop(1)) {
            val parts = entry.split('\t', limit = 3)
            if (parts.size < 3) continue
            val line = Line(parts[0].firstOrNull() ?: continue, parts[1].toIntOrNull() ?: 0, parts[2])
            if (line.kind == 'S') lines.lastOrNull { it.kind == 'S' }?.let { if (it.doneMs < 0) it.doneMs = line.ms }
            lines.add(line)
        }
        if (lines.size > MAX_LINES) lines.subList(0, lines.size - MAX_LINES).clear()
    }

    // Takes the keyboard's height rather than setting it: as the frame's only
    // other child, an unconstrained height would stretch the keyboard to the
    // whole screen.
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val height = if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.EXACTLY)
            MeasureSpec.getSize(heightMeasureSpec) else 0
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), height)
    }

    // Touches would only queue up behind the boot; keep them from the keyboard.
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent) = watching

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(PAPER)
        drawGrid(canvas)
        val pad = 14 * density
        val lineHeight = text.fontSpacing * 1.2f
        val w = width.toFloat()

        // Log: the newest lines that fit above the progress ruler.
        val rulerTop = height - pad - 34 * density
        val logTop = pad
        val fit = ((rulerTop - logTop) / lineHeight).toInt().coerceAtLeast(1)
        val shown = lines.takeLast(fit)
        var y = logTop - text.ascent()
        canvas.save()
        canvas.clipRect(pad, logTop, w - pad, rulerTop)
        val blink = (SystemClock.uptimeMillis() / 400) % 2 == 0L
        val currentStep = lines.lastOrNull { it.kind == 'S' }
        for (line in shown) {
            when (line.kind) {
                'S' -> drawStep(canvas, line, pad, y, w - pad, blink && line === currentStep)
                'I' -> {
                    text.color = DIM
                    canvas.drawText("  + ${line.text}", pad, y, text)
                }
                'R' -> {
                    text.color = OK
                    canvas.drawText("> ${line.text}", pad, y, text)
                }
                else -> drawAnsi(canvas, line.text, pad + text.measureText("  "), y)
            }
            y += lineHeight
        }
        canvas.restore()
        drawRuler(canvas, pad, rulerTop + 8 * density, w - pad)
    }

    private fun drawStep(canvas: Canvas, line: Line, left: Float, y: Float, right: Float, cursor: Boolean) {
        val label = "> ${line.text} "
        text.color = INK
        canvas.drawText(label, left, y, text)
        val labelWidth = text.measureText(label)
        if (line.doneMs >= 0) {
            val result = " [OK] %.2fs".format((line.doneMs - line.ms) / 1000f)
            val resultWidth = text.measureText(result)
            val dots = ((right - left - labelWidth - resultWidth) / text.measureText(".")).toInt()
            text.color = DIM
            canvas.drawText(".".repeat(dots.coerceAtLeast(0)), left + labelWidth, y, text)
            text.color = OK
            canvas.drawText(result, right - resultWidth, y, text)
        } else if (cursor && !ready) {
            text.color = ACCENT
            canvas.drawText("█", left + labelWidth, y, text)
        }
    }

    /** Draws a line of the engine's log in its own colors (ANSI 256-color escapes). */
    private fun drawAnsi(canvas: Canvas, s: String, left: Float, y: Float) {
        var x = left
        var color = ENGINE_DEFAULT
        var i = 0
        val run = StringBuilder()
        fun flush() {
            if (run.isEmpty()) return
            text.color = color
            val piece = run.toString()
            canvas.drawText(piece, x, y, text)
            x += text.measureText(piece)
            run.setLength(0)
        }
        while (i < s.length) {
            val c = s[i]
            if (c == '\u001b' && i + 1 < s.length && s[i + 1] == '[') {
                val end = s.indexOf('m', i + 2)
                if (end < 0) break
                flush()
                val codes = s.substring(i + 2, end).split(';').mapNotNull { it.toIntOrNull() }
                if (codes.isEmpty() || codes[0] == 0) color = ENGINE_DEFAULT
                if (codes.size >= 3 && codes[0] == 38 && codes[1] == 5) color = xterm256(codes[2])
                i = end + 1
                continue
            }
            if (c >= ' ') run.append(c)
            i++
        }
        flush()
    }

    private fun drawRuler(canvas: Canvas, left: Float, top: Float, right: Float) {
        val estimate = prefs.getInt(KEY_LAST_MS, DEFAULT_ESTIMATE_MS).coerceAtLeast(500)
        val progress = if (ready) 1f else (elapsedMs.toFloat() / estimate).coerceIn(0f, 0.97f)
        val label = "%3d%%  %.2fs".format((progress * 100).toInt(), elapsedMs / 1000f)
        val labelWidth = text.measureText(label)
        val barRight = right - labelWidth - 10 * density
        val barHeight = 8 * density
        paint.style = Paint.Style.FILL
        paint.color = ACCENT
        canvas.drawRect(left, top, left + (barRight - left) * progress, top + barHeight, paint)
        paint.style = Paint.Style.STROKE
        paint.color = LINE
        paint.strokeWidth = 1f
        canvas.drawRect(left, top, barRight, top + barHeight, paint)
        // Ticks every 10%, longer at 0, 50 and 100%.
        for (t in 0..10) {
            val x = left + (barRight - left) * t / 10
            val length = (if (t % 5 == 0) 6 else 3) * density
            canvas.drawLine(x, top + barHeight, x, top + barHeight + length, paint)
        }
        paint.style = Paint.Style.FILL
        text.color = if (ready) OK else INK
        canvas.drawText(label, right - labelWidth, top + barHeight / 2 - (text.ascent() + text.descent()) / 2, text)
    }

    private fun drawGrid(canvas: Canvas) {
        val minor = 12 * density
        paint.strokeWidth = 1f
        var i = 0
        var x = 0f
        while (x < width) {
            paint.color = if (i++ % 5 == 0) GRID_MAJOR else GRID_MINOR
            canvas.drawLine(x, 0f, x, height.toFloat(), paint)
            x += minor
        }
        i = 0
        var y = 0f
        while (y < height) {
            paint.color = if (i++ % 5 == 0) GRID_MAJOR else GRID_MINOR
            canvas.drawLine(0f, y, width.toFloat(), y, paint)
            y += minor
        }
    }

    private companion object {
        const val POLL_MS = 50L
        const val SETTLE_MS = 600L  // engine size requests settle within ~400 ms of init
        const val FADE_DELAY_MS = 120L  // a moment to see "ready"
        const val MAX_LINES = 300
        const val KEY_LAST_MS = "last_boot_ms"
        const val DEFAULT_ESTIMATE_MS = 2000
        // NINLib.engineStage (ninlib_exports.cpp)
        const val STAGE_READY = 6
        // The launch page's colors (NINActivity.kt)
        const val PAPER = 0xFF0C0F13.toInt()
        const val GRID_MINOR = 0xFF131920.toInt()
        const val GRID_MAJOR = 0xFF1A222B.toInt()
        const val LINE = 0xFF3B4756.toInt()
        const val INK = 0xFFD8DEE5.toInt()
        const val DIM = 0xFF7D8793.toInt()
        const val ACCENT = 0xFFF2A93B.toInt()
        const val OK = 0xFF79D29A.toInt()
        const val ENGINE_DEFAULT = 0xFFA9B2BC.toInt()

        private val BASIC = intArrayOf(
            0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xc0c0c0,
            0x808080, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff,
        )
        private val LEVELS = intArrayOf(0, 95, 135, 175, 215, 255)

        /** An xterm 256-color index as a color, dark ones lifted to read on the paper. */
        fun xterm256(index: Int): Int {
            val rgb = when (index) {
                in 0..15 -> BASIC[index]
                in 16..231 -> (index - 16).let { LEVELS[it / 36] shl 16 or (LEVELS[it / 6 % 6] shl 8) or LEVELS[it % 6] }
                in 232..255 -> (8 + 10 * (index - 232)).let { it shl 16 or (it shl 8) or it }
                else -> return ENGINE_DEFAULT
            }
            var r = rgb shr 16 and 0xff
            var g = rgb shr 8 and 0xff
            var b = rgb and 0xff
            val lift = (110 - maxOf(r, g, b)).coerceAtLeast(0)
            r += lift; g += lift; b += lift
            return (0xff shl 24) or (r shl 16) or (g shl 8) or b
        }
    }
}
