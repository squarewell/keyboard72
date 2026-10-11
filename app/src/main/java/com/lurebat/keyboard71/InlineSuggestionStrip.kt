package com.lurebat.keyboard71

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.Icon
import android.os.Build
import android.util.Size
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.InlineSuggestion
import android.view.inputmethod.InlineSuggestionsRequest
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.inline.InlinePresentationSpec
import androidx.annotation.RequiresApi
import androidx.autofill.inline.UiVersions
import androidx.autofill.inline.common.TextViewStyle
import androidx.autofill.inline.common.ViewStyle
import androidx.autofill.inline.v1.InlineSuggestionUi

/**
 * Password managers' autofill chips (Android 11+ inline suggestions), laid over
 * the middle of the keyboard's top bar, between its menu and mode icons. The
 * strip is an ordinary view above the GL keyboard and exists only while there
 * are chips, so the keyboard itself is untouched.
 */
@RequiresApi(Build.VERSION_CODES.R)
class InlineSuggestionStrip(private val context: Context) {
    private val row = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER
    }
    private val scroller = HorizontalScrollView(context).apply {
        isHorizontalScrollBarEnabled = false
        visibility = View.GONE
        isFillViewport = true  // a narrow row stays centered
        addView(row, ViewGroup.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT))
    }
    private var generation = 0

    /** Adds the strip to [root], above the keyboard view that fills it. */
    fun attach(root: FrameLayout) {
        (scroller.parent as ViewGroup?)?.removeView(scroller)
        root.addView(scroller, FrameLayout.LayoutParams(0, 0, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        root.addOnLayoutChangeListener { _, left, top, right, bottom, _, _, _, _ ->
            val width = right - left
            val height = bottom - top
            val params = scroller.layoutParams as FrameLayout.LayoutParams
            val wanted = (width * (1 - 2 * SIDE_MARGIN)).toInt() to (height * TOP_BAR_FRACTION).toInt()
            if (params.width != wanted.first || params.height != wanted.second) {
                params.width = wanted.first
                params.height = wanted.second
                scroller.post { scroller.layoutParams = params }
            }
        }
    }

    fun request(): InlineSuggestionsRequest {
        val density = context.resources.displayMetrics.density
        val chip = ViewStyle.Builder()
            .setBackground(Icon.createWithResource(context, R.drawable.inline_chip_background))
            .setPadding((12 * density).toInt(), 0, (12 * density).toInt(), 0)
            .build()
        val style = InlineSuggestionUi.newStyleBuilder()
            .setChipStyle(chip)
            .setTitleStyle(TextViewStyle.Builder().setTextColor(Color.WHITE).setTextSize(14f).build())
            .setSubtitleStyle(TextViewStyle.Builder().setTextColor(Color.LTGRAY).setTextSize(12f).build())
            .build()
        val styles = UiVersions.newStylesBuilder().addStyle(style).build()
        val spec = InlinePresentationSpec.Builder(
            Size((48 * density).toInt(), (32 * density).toInt()),
            Size((320 * density).toInt(), (40 * density).toInt()),
        ).setStyle(styles).build()
        return InlineSuggestionsRequest.Builder(listOf(spec)).setMaxSuggestionCount(MAX_CHIPS).build()
    }

    /** Replaces the chips; an empty list hides the strip. */
    @SuppressLint("NewApi")
    fun show(suggestions: List<InlineSuggestion>) {
        val current = ++generation
        row.removeAllViews()
        scroller.visibility = if (suggestions.isEmpty()) View.GONE else View.VISIBLE
        if (suggestions.isEmpty()) return
        // A chip inflated at WRAP_CONTENT keeps a zero width here, so give it an
        // explicit size within the request's spec: one wide chip, or the strip
        // shared between several.
        val density = context.resources.displayMetrics.density
        val gap = (6 * density).toInt()
        val stripWidth = scroller.layoutParams?.width?.takeIf { it > 0 } ?: (300 * density).toInt()
        val stripHeight = scroller.layoutParams?.height?.takeIf { it > 0 } ?: (40 * density).toInt()
        val height = minOf((40 * density).toInt(), (stripHeight * 0.8f).toInt()).coerceAtLeast((32 * density).toInt())
        val shared = (stripWidth - gap * (suggestions.size - 1)) / suggestions.size
        val width = minOf((260 * density).toInt(), shared).coerceIn((48 * density).toInt(), (320 * density).toInt())
        suggestions.forEachIndexed { index, suggestion ->
            suggestion.inflate(context, Size(width, height), context.mainExecutor) { view ->
                if (view == null || current != generation) return@inflate
                val params = LinearLayout.LayoutParams(width, height)
                params.marginStart = if (index == 0) 0 else gap
                // Keep the service's order even when chips inflate out of order.
                view.tag = index
                val position = (0 until row.childCount).count { (row.getChildAt(it).tag as Int) < index }
                row.addView(view, position, params)
            }
        }
    }

    fun clear() = show(emptyList())

    private companion object {
        const val MAX_CHIPS = 6
        const val SIDE_MARGIN = 0.15f        // keep the top bar's menu and mode icons free
        const val TOP_BAR_FRACTION = 0.153f  // top bar height / keyboard height (e.g. 120 of 784 px)
    }
}
