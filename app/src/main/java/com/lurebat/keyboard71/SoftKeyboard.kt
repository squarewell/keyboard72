@file:Suppress("NAME_SHADOWING")

package com.lurebat.keyboard71

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.inputmethodservice.InputMethodService
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.InlineSuggestionsRequest
import android.view.inputmethod.InlineSuggestionsResponse
import android.widget.FrameLayout
import androidx.annotation.RequiresApi
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import com.jormy.nin.NINLib
import com.jormy.nin.NINLib.onChangeAppOrTextbox
import com.jormy.nin.NINLib.onExternalSelChange
import com.jormy.nin.NINLib.onTextSelection
import com.jormy.nin.NINLib.onWordDestruction
import com.lurebat.keyboard71.BuildConfig
import com.lurebat.keyboard71.tasker.triggerBasicTaskerEvent
import java.util.concurrent.ConcurrentLinkedQueue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlin.math.min


class SoftKeyboard : InputMethodService() {
    private var startedRetype: Boolean = false
    val textBoxEventQueue: ConcurrentLinkedQueue<TextBoxEvent> = ConcurrentLinkedQueue()
    val textOpQueue: ConcurrentLinkedQueue<TextOp> = ConcurrentLinkedQueue()
    private var lazyString: LazyString = LazyStringRope(
        SimpleCursor(0,0),
        SimpleCursor(-1, -1),
        "",
        "",
        "",
        InputConnectionRefresher(){ currentInputConnection }
    )
    private var didProcessTextOps = false
    private var lastTextOpTimeMillis: Long = 0
    private var selectionDiffRetype: SimpleCursor? = null
    private var candidateLocationBeforeRetype: SimpleCursor? = null
    private var afterRetypeCounter = 0

    private var selectionMode = false

    // With the screen off the keyboard's surface stays alive, so its render loop
    // would keep running with nothing pacing it. (The original app paused on lock
    // instead, which also stopped the keyboard on the lock screen.)
    private val screenReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_OFF -> ninView?.onPause()
                Intent.ACTION_SCREEN_ON -> ninView?.onResume()
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        keyboard = this
        // Read when the engine boots, so set it first; it applies from the next boot.
        NINLib.setJitFastmem(Preferences(this).jitFastmemBlocking())
        window.window?.addFlags(
            WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD or
                    WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
        )
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_OFF)
            addAction(Intent.ACTION_SCREEN_ON)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(screenReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(screenReceiver, filter)
        }
        // Field behavior from the launch page, kept current while the keyboard runs.
        FieldRules.current = runBlocking { FieldRules.flow(this@SoftKeyboard).first() }
        rulesScope.launch { FieldRules.flow(this@SoftKeyboard).collect { FieldRules.current = it } }
    }

    private val rulesScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    override fun onDestroy() {
        rulesScope.cancel()
        mainHandler.removeCallbacks(surfaceCheck)
        unregisterReceiver(screenReceiver)
        super.onDestroy()
    }

    override fun onUpdateSelection(
        oldSelStart: Int,
        oldSelEnd: Int,
        newSelStart: Int,
        newSelEnd: Int,
        candidatesStart: Int,
        candidatesEnd: Int
    ) {
        val shouldSignal =
            !didProcessTextOps && System.currentTimeMillis() - lastTextOpTimeMillis >= 55
        if (BuildConfig.DEBUG) {
            Log.d("NIN", "[before $lazyString, $oldSelStart, $oldSelEnd, $newSelStart, $newSelEnd, $candidatesStart, $candidatesEnd]")
        }
        changeSelection(
            currentInputConnection,
            newSelStart,
            newSelEnd,
            candidatesStart,
            candidatesEnd,
            if (shouldSignal) "external" else null
        )
        if (!shouldSignal) {
            didProcessTextOps = false
        }

        if (BuildConfig.DEBUG) {
            Log.d("NIN", "[after $lazyString]")
        }
    }

    override fun onCreateInputView(): View {
        val view = ninView
        if (view == null) {
            ninView = NINView(this)
        } else {
            (view.parent as ViewGroup?)?.removeView(view)
        }
        // The keyboard view fills a frame that can also hold the autofill chips
        // and the boot screen.
        val root = FrameLayout(this)
        root.addView(ninView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        val screen = bootScreen ?: BootScreen(this).also { bootScreen = it }
        (screen.parent as ViewGroup?)?.removeView(screen)
        root.addView(screen, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) inlineStrip.attach(root)
        return root
    }

    /** Shown over the keyboard while the engine starts (BootScreen.kt). */
    var bootScreen: BootScreen? = null
        private set

    // Password managers' inline autofill chips (Android 11+), over the top bar.
    @get:RequiresApi(Build.VERSION_CODES.R)
    private val inlineStrip by lazy { InlineSuggestionStrip(this) }

    @RequiresApi(Build.VERSION_CODES.R)
    override fun onCreateInlineSuggestionsRequest(uiExtras: Bundle): InlineSuggestionsRequest =
        inlineStrip.request()

    @RequiresApi(Build.VERSION_CODES.R)
    override fun onInlineSuggestionsResponse(response: InlineSuggestionsResponse): Boolean {
        inlineStrip.show(response.inlineSuggestions)
        return true
    }

    override fun onStartInputView(attribute: EditorInfo, restarting: Boolean) {
        super.onStartInputView(attribute, restarting)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !restarting) inlineStrip.clear()
        bootScreen?.watch()

        Log.d("SoftKeyboard",
            "------------ jormoust Editor Info : ${attribute.packageName} | ${attribute.fieldName}|${attribute.inputType}"
        )
        val field = FieldRules.resolve(attribute)

        val fieldChange = TextBoxEvent.AppFieldChange(
            attribute.packageName, attribute.fieldName, field.mode, field.keyboardType, field.returnKeyType,
        )
        lastFieldChange = fieldChange
        doTextEvent(fieldChange)

        lazyString = LazyStringRope(
            SimpleCursor(attribute.initialSelStart, attribute.initialSelEnd),
            SimpleCursor(-1, -1),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) attribute.getInitialTextBeforeCursor(1000, 0) else currentInputConnection.getTextBeforeCursor(1000, 0),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) attribute.getInitialSelectedText(0) else currentInputConnection.getSelectedText(0),
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) attribute.getInitialTextAfterCursor(1000, 0) else currentInputConnection.getTextAfterCursor(1000, 0),
            InputConnectionRefresher(){ currentInputConnection }
        )

        signalCursorCandidacyResult(currentInputConnection, "startInputView")

        mainHandler.removeCallbacks(surfaceCheck)
        mainHandler.postDelayed(surfaceCheck, SURFACE_CHECK_DELAY_MS)
    }

    // Showing the keyboard again shortly after hiding it (seen on recent Android versions) can
    // bring the window back without its GL surface: the SurfaceView released it
    // on hide and is never told to recreate it, so the keyboard stays blank until
    // the input method is restarted. Toggling the view itself does not help;
    // cycling the window's visibility makes the SurfaceView create a new surface.
    private val mainHandler = Handler(Looper.getMainLooper())
    private var lastFieldChange: TextBoxEvent.AppFieldChange? = null

    /** Tells a freshly restarted engine about the current field and text (see NINView.restartEngine). */
    fun resendField() = mainHandler.post {
        lastFieldChange?.let { doTextEvent(it) }
        signalCursorCandidacyResult(currentInputConnection, "restart")
    }
    private var surfaceRecoveries = 0
    private val surfaceCheck = Runnable { restoreLostSurface() }

    private fun restoreLostSurface() {
        val view = ninView ?: return
        if (!isInputViewShown || view.holder.surface.isValid) {
            if (surfaceRecoveries > 0) Log.i(TAG, "surface restored")
            surfaceRecoveries = 0
            return
        }
        if (surfaceRecoveries == MAX_SURFACE_RECOVERIES) {
            Log.e(TAG, "surface still missing after $surfaceRecoveries recoveries")
            surfaceRecoveries = 0
            return
        }
        surfaceRecoveries += 1
        Log.w(TAG, "keyboard shown without a surface; cycling window visibility")
        val decor = window.window?.decorView ?: return
        decor.visibility = View.INVISIBLE
        mainHandler.post { decor.visibility = View.VISIBLE }
        mainHandler.postDelayed(surfaceCheck, SURFACE_CHECK_DELAY_MS)
    }

    @Deprecated("Deprecated in Java")
    override fun onViewClicked(focusChanged: Boolean) {
        val curconn = currentInputConnection
        if (curconn != null) {
            curconn.setComposingRegion(-1, -1)
            signalCursorCandidacyResult(curconn, "onViewClicked")
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        return super.onKeyUp(keyCode, event)
    }
        private fun changeSelection(
            ic: InputConnection,
            selStart: Int? = null,
            selEnd: Int? = null,
            candidatesStart: Int? = null,
            candidatesEnd: Int? = null,
            signal: String? = null
        ) {
            lazyString.setSelectionAndCandidate(selStart, selEnd, candidatesStart, candidatesEnd)
            if (signal != null) {
                signalCursorCandidacyResult(ic, signal)
            }
        }

        private fun performSetSelection(
            selectStart: Int,
            selectEnd: Int,
            fromStart: Boolean,
            signal: Boolean,
            ic: InputConnection,
        ) {

            var shouldSignal = !fromStart || signal

            var keepSelection = false

            var keepCandidate = false

            var start = selectStart
            var end = selectEnd
            val isNormalMovement = start == end && !fromStart && !signal

            var finalSelectionStart = 0
            var finalSelectionEnd = 0

            val isAfterRetype = selectStart == 5000 && selectEnd == 5000 && !fromStart && !signal
            if (isAfterRetype) {
                afterRetypeCounter += 1
                if (afterRetypeCounter < 3) {
                    return
                }
                afterRetypeCounter = 0
                val deltaSelection = selectionDiffRetype ?: SimpleCursor(0, 0)

                val candidateStart = candidateLocationBeforeRetype?.let {
                    candidateLocationBeforeRetype = null
                    if (it.start == -1 || it.end == -1) {
                        SimpleCursor(-1, -1)
                    } else {
                        SimpleCursor(lazyString.selection.end + it.start , lazyString.selection.end + it.end)
                    }
                } ?: SimpleCursor(0, 0)

                lazyString.candidate.set(candidateStart.start, candidateStart.end)
                finalSelectionStart = deltaSelection.start
                finalSelectionEnd = deltaSelection.end
                keepCandidate = false // Sadly signaling messes up the candidate, if we find a way to avoid signaling - we can turn this on
                keepSelection = true
            } else {
                val candidate = lazyString.getCandidate()
                if (candidate != null && fromStart) {
                    val candidate = lazyString.getCandidate()
                    if (candidate != null) {
                        val candidateBeforeSelection = candidate.substring(0, min(candidate.length, lazyString.selection.min - lazyString.candidate.min)).toByteArray().size
                        if (candidateBeforeSelection > 0) {
                            start -= candidateBeforeSelection
                            end -= candidateBeforeSelection
                        }
                    }
                }

                if ((isNormalMovement && selectionMode)) {
                    keepSelection = true
                    finalSelectionEnd = lazyString.byteOffsetToGraphemeOffset(lazyString.selection.end, end)
                } else {
                    finalSelectionStart = lazyString.byteOffsetToGraphemeOffset(lazyString.selection.start, start)
                    finalSelectionEnd = lazyString.byteOffsetToGraphemeOffset(lazyString.selection.end,-lazyString.selection.length() + end)
                }



                if (!startedRetype) {
                    // get the selection before retype
                    selectionDiffRetype = SimpleCursor(lazyString.selection.start, lazyString.selection.end)
                    candidateLocationBeforeRetype = SimpleCursor(lazyString.candidate.start, lazyString.candidate.end)
                }


            }


            lazyString.moveSelection(finalSelectionStart, finalSelectionEnd)

            if (fromStart || start != end) {
                keepSelection = true
            }

            ic.setComposingRegion(if (keepCandidate) lazyString.candidate.start else lazyString.candidate.end, lazyString.candidate.end)
            ic.setSelection(if (keepSelection) lazyString.selection.start else lazyString.selection.end, lazyString.selection.end)
            if (!startedRetype) {
                selectionDiffRetype = SimpleCursor(selectionDiffRetype!!.start - lazyString.selection.end, selectionDiffRetype!!.end - lazyString.selection.end)
                candidateLocationBeforeRetype = SimpleCursor(candidateLocationBeforeRetype!!.start - lazyString.selection.end, candidateLocationBeforeRetype!!.end - lazyString.selection.end)
            }

            if (startedRetype) {
                startedRetype = false
            }

            if (shouldSignal) {
                signalCursorCandidacyResult(ic, "setselle")
            }
        }

        private fun performBackReplacement(
            rawBackIndex: Int,
            original: String,
            replacement: String?,
            ic: InputConnection
        ) {
            var backIndexBytes = rawBackIndex

            var candidateBeforeSelection = 0
            // For some reason it only skips the candidate if the selection is at the end of the candidate
            if (lazyString.selection.max == lazyString.candidate.max) {
                val candidate = lazyString.getCandidate()
                if (candidate != null) {
                    candidateBeforeSelection = candidate.substring(0, min(candidate.length, lazyString.selection.min - lazyString.candidate.min)).toByteArray().size
                    if (candidateBeforeSelection > 0) {
                        backIndexBytes += candidateBeforeSelection
                    }
                }
            }

            val stringUntilCursor = lazyString.getStringByBytesBeforeCursor(backIndexBytes)
            val replacement = replacement ?: ""
            var startIndex = lazyString.selection.min - stringUntilCursor.length
            val replacingMiddleOfWord = stringUntilCursor.length < original.length

            lazyString.delete(startIndex, startIndex + original.length)
            lazyString.addString(startIndex, replacement)

            // Delete the original text
            ic.setSelection(startIndex, startIndex)
            ic.setComposingRegion(startIndex, startIndex)
            ic.deleteSurroundingText(0, original.length)

            // Insert the replacement text
            ic.commitText(replacement, 1)
            ic.setComposingRegion(lazyString.candidate.start, lazyString.candidate.end)
            ic.setSelection(lazyString.selection.start, lazyString.selection.end)
            if (replacingMiddleOfWord) {
                signalCursorCandidacyResult(ic, "backrepl")
            }
        }

        @Suppress("UNUSED_PARAMETER")
        private fun performCursorMovement(
            xmove: Int,
            ymove: Int,
            selectionMode: Boolean,
            ic: InputConnection
        ) {
            lazyString.moveSelection(xmove, xmove)
            if (!selectionMode) {
                lazyString.setSelection(lazyString.selection.end, lazyString.selection.end)
            }

            ic.setComposingRegion(lazyString.selection.start, lazyString.selection.end)
            ic.setSelection(lazyString.selection.start, lazyString.selection.end)
            changeSelection(ic, lazyString.selection.start, lazyString.selection.end, lazyString.candidate.start, lazyString.candidate.end, "cursordrag")
        }

        private fun signalCursorCandidacyResult(ic: InputConnection?, mode: String?) {
            if (ic == null) {
                doTextEvent(TextBoxEvent.Reset)
                return
            }
            if (lazyString.candidate.isNotEmpty() && (lazyString.selection.max < lazyString.candidate.min || lazyString.selection.min > lazyString.candidate.max)) {
                // we jumped out of the candidate range - reset it
                ic.finishComposingText()
                lazyString.candidate.start = -1
                lazyString.candidate.end = -1
            }

            val charCount = 100
            val candidate = lazyString.getCandidate()?.toString()
            val beforeCandidate = if (candidate != null) lazyString.getStringByIndex(maxOf(0, lazyString.candidate.min - charCount), lazyString.candidate.min) else lazyString.getCharsBeforeCursor(charCount).toString()
            val afterCandidate = if (candidate != null) lazyString.getStringByIndex(lazyString.candidate.max, lazyString.candidate.max + charCount) else lazyString.getCharsAfterCursor(charCount).toString()

            doTextEvent(TextBoxEvent.Selection(candidate ?: "", beforeCandidate, afterCandidate, mode))
        }

        fun relayDelayedEvents() {
            while (true) {
                val event = textBoxEventQueue.poll()
                if (event != null && BuildConfig.DEBUG) {
                    Log.d("NIN", "Relaying delayed event: $event")
                }
                when (event) {
                    is TextBoxEvent.AppFieldChange -> {
                        NINLib.setFieldTypes(event.keyboardType, event.returnKeyType)
                        onChangeAppOrTextbox(event.packageName, event.field, event.mode)
                    }

                    TextBoxEvent.Reset -> onExternalSelChange()

                    is TextBoxEvent.Selection -> onTextSelection(
                        event.textBefore,
                        event.currentWord,
                        event.textAfter,
                        event.mode
                    )

                    is TextBoxEvent.WordDestruction -> onWordDestruction(
                        event.destroyedWord,
                        event.destroyedString
                    )

                    null -> break
                }
            }
        }

        @Suppress("UNUSED_PARAMETER")
        private fun performMUCommand(
            cmd: String?,
            a1: String?,
            a2: String?,
            a3: String?,
            ic: InputConnection
        ) {
            if (cmd == "retypebksp") {
                ic.setComposingText("", 0)
                changeSelection(ic, lazyString.selection.start, lazyString.selection.start, lazyString.selection.start, lazyString.selection.start, null)
                startedRetype = true
            }
        }

        private fun performBackspacing(mode: String?, singleCharacterMode: Boolean, ic: InputConnection) {
            val (min, max) = if (lazyString.selection.isNotEmpty()) {
                Pair(lazyString.selection.min, lazyString.selection.max)
            }
            else if (lazyString.candidate.isNotEmpty()) {
                Pair(lazyString.candidate.min, lazyString.candidate.max)
            } else {
                var singleCharacterMode = singleCharacterMode
                var count = 1;
                when(mode) {
                    // deletes punctuation
                    "S" -> {Pair(lazyString.findCharBeforeCursor(charArrayOf('\n', '.', '!', ',', '?')), lazyString.selection.min)}
                    // Deletes line
                    "L" -> {Pair(lazyString.findCharBeforeCursor(charArrayOf('\n')), lazyString.selection.min)}
                    // does nothing
                    "C", ". " -> {Pair(lazyString.selection.min, lazyString.selection.min)}
                    "emjf" -> {
                        val word = lazyString.getWordsBeforeCursor(1).takeIf { it.isNotBlank() } ?: lazyString.getWordsBeforeCursor(2)
                        Pair(lazyString.selection.min - word.length, lazyString.selection.min)
                    }
                    else -> {
                        if (mode != null && mode.startsWith("X:")) {
                            count = mode.substring(2).toInt()
                        }
                        if (mode == "emjf") {
                            singleCharacterMode = false
                        }

                        val method = if(singleCharacterMode) lazyString::getCharsBeforeCursor else lazyString::getWordsBeforeCursor
                        Pair(lazyString.selection.min - method(count).length, lazyString.selection.min)
                    }
                }
            }

            val length = max - min

            ic.setComposingRegion(min, min)
            ic.setSelection(min, min)
            ic.deleteSurroundingText(0, length)

            lazyString.delete(min, max)

            changeSelection(ic, min, min, -1, -1, mode ?: "setselle")
        }

        fun processTextOps() {
            val buffer = textOpQueue
            val bufferSize = buffer.size
            if (bufferSize == 0) return

            val ic = keyboard?.currentInputConnection ?: return

            ic.beginBatchEdit()

            try {
                for (i in 0 until bufferSize) {
                    checkIfWeirdDelete(textOpQueue)
                    val op = textOpQueue.poll() ?: break
                    val next = textOpQueue.peek()

                    processOperation(op, next, ic)
                }
            } finally {
                didProcessTextOps = true
                lastTextOpTimeMillis = System.currentTimeMillis()
                ic.endBatchEdit()
            }
        }

    private fun checkIfWeirdDelete(textOpQueue: ConcurrentLinkedQueue<TextOp>) {
        if (textOpQueue.size != 3) {
            return
        }

        val first = textOpQueue.poll()
        val second = textOpQueue.poll()
        val third = textOpQueue.poll()

        if (first is TextOp.SetSelection && !first.fromStart && first.signal &&
            second is TextOp.MarkLiquid && second.newString == "" &&
            third is TextOp.SetSelection && third.start == 0 && third.end == 0 && !third.fromStart && !third.signal) {
            if (BuildConfig.DEBUG) {
                Log.d("NIN", "this is a weird delete")
            }
            doTextOp(TextOp.SimpleBackspace(false));
            return;
        }

        textOpQueue.add(first)
        textOpQueue.add(second)
        textOpQueue.add(third)
    }

    private fun processOperation(
            op: TextOp,
            next: TextOp?,
            ic: InputConnection
        ) {
            if (BuildConfig.DEBUG) {
                Log.d("NIN", "processOperation: $op, next: $next")
            }
            when (op) {
                is TextOp.MarkLiquid -> {
                    if (next !is TextOp.MarkLiquid && next !is TextOp.Solidify) {
                        ic.setComposingText(op.newString, 1)
                    }
                }

                is TextOp.Solidify -> {
                    when {
                        op.newString == "\n" -> {
                            var action = 1
                            if (currentInputEditorInfo.imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION == 0) {
                                action =
                                    currentInputEditorInfo.imeOptions and EditorInfo.IME_MASK_ACTION
                            }
                            if (action != 1) {
                                ic.performEditorAction(action)
                            } else {
                                ic.commitText(op.newString, 1)
                            }
                        }

                        op.newString.startsWith("<{") && op.newString.endsWith("}>") -> {
                            val inner = op.newString.substring(2, op.newString.length - 2)
                            if (inner.isNotEmpty()) {
                                changeSelection(
                                    ic,
                                    signal="external"
                                )
                                doTextOp(TextOp.Special(inner))
                            }
                        }

                        else -> {
                            ic.commitText(op.newString, 1)
                        }
                    }
                }

                is TextOp.SetSelection -> performSetSelection(
                    op.start,
                    op.end,
                    op.fromStart,
                    op.signal,
                    ic
                )

                is TextOp.BackspaceReplacement -> performBackReplacement(
                    op.backIndexFromCursorBytes,
                    op.oldString,
                    op.newString,
                    ic
                )

                is TextOp.SimpleBackspace -> performBackspacing(
                    null,
                    op.singleCharacterMode,
                    ic
                )

                is TextOp.BackspaceModed -> performBackspacing(
                    op.mode,
                    true,
                    ic
                )

                is TextOp.DragCursorUp -> {}
                is TextOp.RequestSelection -> signalCursorCandidacyResult(
                    ic,
                    "requestsel"
                )

                is TextOp.MuCommand -> performMUCommand(
                    op.command,
                    op.arg1,
                    op.arg2,
                    op.arg3,
                    ic
                )

                is TextOp.DragCursorMove -> performCursorMovement(
                    op.xMovement,
                    op.xMovement,
                    op.selectionMode,
                    ic
                )

                is TextOp.Special -> parseSpecialText(ic, op.args)
            }
        }

        private fun parseSpecialText(ic: InputConnection, args: String) {
            val first = args[0]
            val rest = args.substring(1).trim()
            val parts = rest.let {
                val temp = it.split("\\|".toRegex()).toTypedArray()
                if (temp.isEmpty()) {
                    arrayOf(it)
                } else {
                    temp
                }
            }

            when (first) {
                'k' -> {
                    val code = WordHelper.parseKeyCode(parts[0])
                    val modifiers = if (parts.size > 1) WordHelper.parseModifiers(parts[1]) else 0
                    val repeat = if (parts.size > 2) parts[2].toInt() else 0
                    val flags =
                        if (parts.size > 3) parts[3].toInt() else KeyEvent.FLAG_SOFT_KEYBOARD
                    keyDownUp(ic, code, modifiers, repeat, flags)
                }

                'c' -> ic.performContextMenuAction(
                    when (parts[0].uppercase()) {
                        "0", "CUT" -> android.R.id.cut
                        "1", "COPY" -> android.R.id.copy
                        "2", "PASTE" -> android.R.id.paste
                        "3", "SELECT_ALL" -> android.R.id.selectAll
                        "4", "START_SELECT" -> android.R.id.startSelectingText
                        "5", "STOP_SELECT" -> android.R.id.stopSelectingText
                        "6", "SWITCH_KEYBOARD" -> android.R.id.switchInputMethod
                        else -> parts[0].toInt()
                    }
                )

                's' -> selectionMode = !selectionMode

                't' -> this.triggerBasicTaskerEvent(
                    rest,
                    lazyString.getCharsBeforeCursor(1000).toString(),
                    lazyString.getCharsAfterCursor(1000).toString()
                )
            }
        }

    companion object {
        private const val TAG = "SoftKeyboard"

        private const val SURFACE_CHECK_DELAY_MS = 150L
        private const val MAX_SURFACE_RECOVERIES = 2

        var keyboard: SoftKeyboard? = null
        var ninView: NINView? = null

        fun doTextOp(op: TextOp) = keyboard?.let{ k ->
            k.textOpQueue.let {
                it.add(op)
                Handler(Looper.getMainLooper()).post{ k.processTextOps()}
            }
        }

        fun doTextEvent(event: TextBoxEvent) = keyboard?.textBoxEventQueue?.add(event).also {
            NINView.wakeRenderer()  // relayed by the next frame
        }

        private fun keyDownUp(
            ic: InputConnection,
            keyEventCode: Int,
            modifiers: Int,
            repeat: Int,
            flags: Int
        ) {
            val eventTime = SystemClock.uptimeMillis()
            ic.sendKeyEvent(
                KeyEvent(
                    eventTime,
                    eventTime,
                    KeyEvent.ACTION_DOWN,
                    keyEventCode,
                    repeat,
                    modifiers,
                    -1,
                    0,
                    flags
                )
            )
            ic.sendKeyEvent(
                KeyEvent(
                    eventTime,
                    SystemClock.uptimeMillis(),
                    KeyEvent.ACTION_UP,
                    keyEventCode,
                    repeat,
                    modifiers,
                    -1,
                    0,
                    flags
                )
            )
        }
    }
}
