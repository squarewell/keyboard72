package com.lurebat.keyboard71

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.view.ViewTreeObserver
import android.view.inputmethod.InputMethodManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import kotlinx.coroutines.flow.Flow
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.alorma.compose.settings.storage.datastore.composeSettingsDataStore
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

/**
 * The launch page in a technical-drawing style: a fixed drafting grid, hairline
 * frames, monospace labels and numbered sheets. Nothing animates.
 */
private object Draft {
    val paper = Color(0xFF0C0F13)
    val gridMinor = Color(0xFF131920)
    val gridMajor = Color(0xFF1A222B)
    val line = Color(0xFF3B4756)
    val ink = Color(0xFFD8DEE5)
    val dim = Color(0xFF7D8793)
    val accent = Color(0xFFF2A93B)  // dimension/callout amber
    val ok = Color(0xFF79D29A)
    val warn = Color(0xFFE5695C)
    val mono = FontFamily.Monospace
}

@Composable
private fun Mono(text: String, color: Color = Draft.ink, size: TextUnit = 13.sp, spacing: TextUnit = 1.sp,
                 weight: FontWeight = FontWeight.Normal, modifier: Modifier = Modifier, align: TextAlign? = null) =
    Text(text, color = color, fontSize = size, fontFamily = Draft.mono, letterSpacing = spacing, fontWeight = weight,
        lineHeight = size * 1.4f, modifier = modifier, textAlign = align)

/** The drafting grid, fixed to the page. */
private fun Modifier.draftingGrid() = drawBehind {
    val minor = 12.dp.toPx()
    var x = 0f
    var i = 0
    while (x < size.width) {
        drawLine(if (i % 5 == 0) Draft.gridMajor else Draft.gridMinor, Offset(x, 0f), Offset(x, size.height), 1f)
        x += minor; i++
    }
    var y = 0f
    i = 0
    while (y < size.height) {
        drawLine(if (i % 5 == 0) Draft.gridMajor else Draft.gridMinor, Offset(0f, y), Offset(size.width, y), 1f)
        y += minor; i++
    }
}

/** Registration ticks at the corners of a frame, just outside it. */
private fun DrawScope.cornerTicks(color: Color) {
    val t = 6.dp.toPx()
    val g = 3.dp.toPx()
    val w = size.width
    val h = size.height
    for ((cx, cy, sx, sy) in listOf(
        listOf(0f, 0f, -1f, -1f), listOf(w, 0f, 1f, -1f), listOf(0f, h, -1f, 1f), listOf(w, h, 1f, 1f),
    )) {
        drawLine(color, Offset(cx + sx * g, cy), Offset(cx + sx * (g + t), cy), 1.5f)
        drawLine(color, Offset(cx, cy + sy * g), Offset(cx, cy + sy * (g + t)), 1.5f)
    }
}

/** A numbered sheet: hairline frame, corner ticks and a header rule. */
@Composable
private fun Sheet(number: String, title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier = Modifier
            .padding(top = 22.dp)
            .fillMaxWidth()
            .drawBehind { cornerTicks(Draft.accent) }
            .border(1.dp, Draft.line)
            .background(Draft.paper.copy(alpha = 0.92f)),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
            Mono(number, Draft.accent, 13.sp, 2.sp, FontWeight.Bold)
            Spacer(Modifier.width(10.dp))
            Box(Modifier.width(18.dp).height(1.dp).background(Draft.line))
            Spacer(Modifier.width(10.dp))
            Mono(title.uppercase(), Draft.ink, 13.sp, 3.sp, FontWeight.Bold)
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(Draft.line))
        Column(modifier = Modifier.padding(14.dp), content = content)
    }
}

/** A rectangular outlined button with a monospace label. */
@Composable
private fun DraftButton(label: String, enabled: Boolean = true, filled: Boolean = false, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier
            .heightIn(min = 44.dp)
            .background(if (filled) Draft.accent.copy(alpha = if (enabled) 1f else 0.3f) else Color.Transparent)
            .border(1.dp, if (enabled) (if (filled) Draft.accent else Draft.ink.copy(alpha = 0.7f)) else Draft.line)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 8.dp),
    ) {
        Mono(label.uppercase(), if (filled) Draft.paper else if (enabled) Draft.ink else Draft.dim, 12.sp, 1.5.sp,
            FontWeight.Bold, align = TextAlign.Center)
    }
}

/** One choice among several, as a row of boxes; the chosen one is inked in. */
@Composable
private fun <T> Choice(options: List<T>, selected: T, label: (T) -> String, onSelect: (T) -> Unit) {
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.fillMaxWidth()) {
        for (option in options) {
            val on = option == selected
            Box(
                contentAlignment = Alignment.Center,
                modifier = Modifier
                    .weight(1f)
                    .heightIn(min = 38.dp)
                    .background(if (on) Draft.accent else Color.Transparent)
                    .border(1.dp, if (on) Draft.accent else Draft.line)
                    .clickable { onSelect(option) }
                    .padding(horizontal = 4.dp, vertical = 6.dp),
            ) {
                Mono(label(option).uppercase(), if (on) Draft.paper else Draft.ink,
                    if (options.size > 3) 10.sp else 11.sp, if (options.size > 3) 0.sp else 1.sp,
                    if (on) FontWeight.Bold else FontWeight.Normal, align = TextAlign.Center)
            }
        }
    }
}

/** Title. */
@Composable
private fun Header() {
    Column(modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
        Mono("KEYBOARD 72", Draft.ink, 30.sp, 6.sp, FontWeight.Bold)
    }
}

private const val KEYBOARD_HINT = "The keyboard did not open. Make Keyboard 72 the current keyboard, then try again."

private fun whenWindowFocused(view: View, action: () -> Unit) {
    if (view.hasWindowFocus()) return action()
    view.viewTreeObserver.addOnWindowFocusChangeListener(object : ViewTreeObserver.OnWindowFocusChangeListener {
        override fun onWindowFocusChanged(hasFocus: Boolean) {
            if (!hasFocus) return
            view.viewTreeObserver.removeOnWindowFocusChangeListener(this)
            view.post(action)
        }
    })
}

@Composable
private fun SetupRow(text: String, done: Boolean, action: String, onClick: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Mono(if (done) "[OK]" else "[--]", if (done) Draft.ok else Draft.warn, 13.sp, 0.5.sp, FontWeight.Bold)
        Spacer(Modifier.width(10.dp))
        Mono(text, if (done) Draft.ink else Draft.dim, 13.sp, 0.5.sp, modifier = Modifier.weight(1f))
        if (!done) DraftButton(action, modifier = Modifier.width(110.dp), onClick = onClick)
    }
}

/** Per-group input field behavior (see [FieldRules]). */
@Composable
private fun FieldSheet(number: String, open: () -> Unit) {
    val context = LocalContext.current
    val rules by remember { FieldRules.flow(context) }.collectAsState(initial = FieldRules.defaults())
    val changed = FieldRules.Group.values().count { (rules[it] ?: it.default) != it.default }
    Sheet(number, "Input fields") {
        DraftButton(if (changed == 0) "Configure" else "Configure  ·  $changed changed", modifier = Modifier.fillMaxWidth()) { open() }
    }
}

/** The input field settings, a page of their own (see [FieldRules]). */
@Composable
private fun FieldsPage(number: String, back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val rules by remember { FieldRules.flow(context) }.collectAsState(initial = FieldRules.defaults())
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    Mono("< BACK", Draft.accent, 13.sp, 2.sp, FontWeight.Bold,
        modifier = Modifier.clickable { back() }.padding(top = 8.dp, bottom = 4.dp))
    Sheet(number, "Input fields") {
        for (group in FieldRules.Group.values()) {
            val rule = rules[group] ?: group.default
            val expanded = open == group.name
            val summary = if (group.privacyOnly) rule.privacy.label
            else "${rule.board.label} · ${rule.privacy.label} · ${rule.enter.label}"
            val changed = rule != group.default
            Column(modifier = Modifier.fillMaxWidth().border(1.dp, if (expanded) Draft.accent else Draft.line.copy(alpha = 0.6f))) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { open = if (expanded) null else group.name }
                        .padding(horizontal = 10.dp, vertical = 8.dp),
                ) {
                    Column(Modifier.weight(1f)) {
                        Mono(group.label.uppercase(), Draft.ink, 12.sp, 1.sp, FontWeight.Bold)
                        Mono(summary.uppercase(), if (changed) Draft.accent else Draft.dim, 10.sp, 0.5.sp)
                    }
                    Mono(if (expanded) "−" else "+", Draft.dim, 16.sp, 0.sp)
                }
                if (expanded) {
                    Column(modifier = Modifier.padding(start = 10.dp, end = 10.dp, bottom = 12.dp)) {
                        fun save(new: FieldRules.Rule) = scope.launch { FieldRules.set(context, group, new) }
                        if (!group.privacyOnly) {
                            Choice(FieldRules.Board.values().toList(), rule.board, { it.label }) { save(rule.copy(board = it)) }
                            Spacer(Modifier.height(6.dp))
                        }
                        Choice(FieldRules.Privacy.values().toList(), rule.privacy, { it.label }) { save(rule.copy(privacy = it)) }
                        if (!group.privacyOnly) {
                            Spacer(Modifier.height(6.dp))
                            Choice(FieldRules.Enter.values().toList(), rule.enter, { it.label }) { save(rule.copy(enter = it)) }
                        }
                    }
                }
            }
            Spacer(Modifier.height(6.dp))
        }
        Spacer(Modifier.height(4.dp))
        DraftButton("Reset defaults", enabled = rules != FieldRules.defaults(), modifier = Modifier.fillMaxWidth()) {
            scope.launch { FieldRules.reset(context) }
        }
    }
}

/** The engine's own backups as text files (see [Backups]). */
@Composable
private fun BackupSheet(number: String, openKeyboard: () -> Unit, closeKeyboard: () -> Unit) {
    val context = LocalContext.current
    var status by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }
    var pending by remember { mutableStateOf<Pair<Backups.Kind, ByteArray>?>(null) }

    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
        val export = pending
        pending = null
        if (uri == null || export == null) return@rememberLauncherForActivityResult
        status = try {
            context.contentResolver.openOutputStream(uri, "wt")!!.use { it.write(export.second) }
            "${export.first.label} saved."
        } catch (e: Exception) {
            "Could not save the file: ${e.message}"
        }
    }
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        val text = try {
            context.contentResolver.openInputStream(uri)!!.use { it.readBytes() }
        } catch (e: Exception) {
            status = "Could not read the file: ${e.message}"
            return@rememberLauncherForActivityResult
        }
        busy = true
        status = null
        openKeyboard()
        Backups.import(text) { result ->
            busy = false
            status = when (result) {
                Backups.Result.ImportStarted -> "Now confirm in the keyboard: USE DATA imports it, CANCEL leaves everything as it is."
                Backups.Result.NotABackup -> "That file is not a Nintype backup."
                else -> KEYBOARD_HINT
            }
        }
    }
    fun export(kind: Backups.Kind) {
        busy = true
        status = null
        openKeyboard()
        Backups.export(kind) { result ->
            busy = false
            closeKeyboard()
            if (result is Backups.Result.Exported) {
                pending = kind to result.text
                val date = SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date())
                save.launch("keyboard72-${kind.fileName}-$date.txt")
            } else {
                status = KEYBOARD_HINT
            }
        }
    }

    Sheet(number, "Backups") {
        val kinds = listOf(
            Backups.Kind.SETTINGS to "Settings", Backups.Kind.SHORTCUTS to "Shortcuts",
            Backups.Kind.WORDS to "Words", Backups.Kind.READABLE_WORDS to "Words, readable",
        )
        for (pair in kinds.chunked(2)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(bottom = 8.dp)) {
                for ((kind, label) in pair) {
                    DraftButton("Export $label", enabled = !busy, modifier = Modifier.weight(1f)) { export(kind) }
                }
            }
        }
        DraftButton("Import from a file", enabled = !busy, filled = true, modifier = Modifier.fillMaxWidth()) {
            open.launch(arrayOf("text/*", "application/octet-stream"))
        }
        status?.let {
            Spacer(Modifier.height(10.dp))
            Mono("> $it", Draft.ink, 12.sp, 0.3.sp)
        }
    }
}

@Composable
private fun ExtrasSheet(number: String, openKeyboard: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val prefs = remember { Preferences(context) }
    val haptics by prefs.haptics.collectAsState(initial = Preferences.Haptics.LIGHT)
    var status by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }
    // The keyboard opens and its boot screen shows the restart.
    fun restart() {
        busy = true
        status = null
        openKeyboard()
        NINView.callOnEngine<Long?>(Backups.TIMEOUT_MS, null, { NINView.restartEngine() }) { result ->
            busy = false
            if (result == null) status = KEYBOARD_HINT
        }
    }
    Sheet(number, "Extras") {
        Mono("HAPTICS", Draft.dim, 10.sp, 2.sp)
        Spacer(Modifier.height(4.dp))
        Choice(Preferences.Haptics.values().toList(), haptics, { it.label }) { scope.launch { prefs.setHaptics(it) } }
        Spacer(Modifier.height(14.dp))
        DraftButton("Restart engine", enabled = !busy, modifier = Modifier.fillMaxWidth()) { restart() }
        status?.let {
            Spacer(Modifier.height(10.dp))
            Mono("> $it", Draft.ink, 12.sp, 0.3.sp)
        }
    }
}

@Composable
private fun ThanksSheet(number: String) {
    Sheet(number, "Special thanks") {
        Mono("lurebat/keyboard71", Draft.ink, 13.sp, 1.sp, modifier = Modifier.padding(vertical = 2.dp))
        Mono("jormy/nintype", Draft.ink, 13.sp, 1.sp, modifier = Modifier.padding(vertical = 2.dp))
    }
}

class NINActivity : ComponentActivity() {

    private fun keyboardEnabled() = (getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager)
        .enabledInputMethodList.any { it.packageName == packageName }

    private fun keyboardSelected() = Settings.Secure.getString(contentResolver, Settings.Secure.DEFAULT_INPUT_METHOD)
        ?.startsWith("$packageName/") == true

    @OptIn(ExperimentalComposeUiApi::class, ExperimentalMaterial3Api::class)  // keyboard controller, text field colors
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = 0xFF0C0F13.toInt()
        window.navigationBarColor = 0xFF0C0F13.toInt()

        setContent {
            var enabled by remember { mutableStateOf(keyboardEnabled()) }
            var selected by remember { mutableStateOf(keyboardSelected()) }
            val view = LocalView.current
            val lifecycle = LocalLifecycleOwner.current.lifecycle
            // Refresh the setup state on return from Settings or the keyboard picker.
            DisposableEffect(lifecycle, view) {
                val refresh = { enabled = keyboardEnabled(); selected = keyboardSelected() }
                val observer = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) refresh() }
                val focus = ViewTreeObserver.OnWindowFocusChangeListener { if (it) refresh() }
                lifecycle.addObserver(observer)
                view.viewTreeObserver.addOnWindowFocusChangeListener(focus)
                onDispose {
                    lifecycle.removeObserver(observer)
                    view.viewTreeObserver.removeOnWindowFocusChangeListener(focus)
                }
            }

            MaterialTheme(colorScheme = darkColorScheme(background = Draft.paper, surface = Draft.paper)) {
                val tryFocus = remember { FocusRequester() }
                val keyboardController = LocalSoftwareKeyboardController.current
                // The input field settings are a page of their own; Back returns.
                var fieldsPage by rememberSaveable { mutableStateOf(false) }
                BackHandler(enabled = fieldsPage) { fieldsPage = false }
                val mainScroll = rememberScrollState()
                Box(modifier = Modifier.fillMaxSize().background(Draft.paper)) {
                    if (fieldsPage) {
                        Column(
                            modifier = Modifier
                                .fillMaxSize()
                                .verticalScroll(rememberScrollState())
                                .draftingGrid()
                                .padding(horizontal = 20.dp, vertical = 16.dp),
                        ) {
                            FieldsPage("05") { fieldsPage = false }
                        }
                    } else Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(mainScroll)
                            .draftingGrid()
                            .padding(horizontal = 20.dp, vertical = 16.dp),
                    ) {
                        Header()

                        Sheet("01", "Setup") {
                            SetupRow("KEYBOARD ENABLED", enabled, "Enable") {
                                startActivity(Intent(Settings.ACTION_INPUT_METHOD_SETTINGS))
                            }
                            SetupRow("CURRENT KEYBOARD", selected, "Switch") {
                                (getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager).showInputMethodPicker()
                            }
                        }

                        Sheet("02", "Test") {
                            var tryText by rememberSaveable { mutableStateOf("") }
                            OutlinedTextField(
                                value = tryText,
                                onValueChange = { tryText = it },
                                placeholder = { Mono("> type here_", Draft.dim, 14.sp) },
                                textStyle = TextStyle(color = Draft.ink, fontSize = 16.sp),
                                shape = RoundedCornerShape(0.dp),
                                colors = TextFieldDefaults.outlinedTextFieldColors(
                                    cursorColor = Draft.accent,
                                    focusedBorderColor = Draft.accent,
                                    unfocusedBorderColor = Draft.line,
                                    containerColor = Draft.paper,
                                ),
                                modifier = Modifier.fillMaxWidth().focusRequester(tryFocus),
                            )
                        }

                        val openKeyboard = {
                            // After a file picker closes, this window regains focus a
                            // moment later, and a keyboard request before that is dropped.
                            whenWindowFocused(view) {
                                tryFocus.requestFocus()
                                keyboardController?.show()
                            }
                        }
                        BackupSheet("03", openKeyboard, closeKeyboard = { keyboardController?.hide() })

                        ExtrasSheet("04", openKeyboard)

                        FieldSheet("05") { fieldsPage = true }

                        ThanksSheet("06")
                    }
                }
            }
        }
    }

    companion object {
        const val HAPTIC_FEEDBACK_PREFERENCE = "hapticFeedbackPreference"
    }
}

class Preferences(val context: Context) {
    val store = context.composeSettingsDataStore

    enum class Haptics(val label: String) { OFF("Off"), LIGHT("Light"), STRONG("Strong") }

    /** Key haptics; follows the old on/off switch until a strength is chosen. */
    val haptics: Flow<Haptics> = store.data.map { s ->
        s[intPreferencesKey(HAPTIC_STRENGTH)]?.let { Haptics.values().getOrNull(it) }
            ?: if (s[booleanPreferencesKey(NINActivity.HAPTIC_FEEDBACK_PREFERENCE)] ?: true) Haptics.LIGHT else Haptics.OFF
    }

    fun hapticsBlocking() = runBlocking { haptics.first() }

    suspend fun setHaptics(value: Haptics) {
        store.edit { it[intPreferencesKey(HAPTIC_STRENGTH)] = value.ordinal }
    }

    private companion object {
        const val HAPTIC_STRENGTH = "hapticStrength"
    }
}
