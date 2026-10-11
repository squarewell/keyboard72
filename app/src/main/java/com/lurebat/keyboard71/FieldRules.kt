package com.lurebat.keyboard71

import android.content.Context
import android.view.inputmethod.EditorInfo
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import com.alorma.compose.settings.storage.datastore.composeSettingsDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

/**
 * How the keyboard behaves in each kind of text field, set on the launch page.
 *
 * Android describes a field by its input type (class, variation, flags) and IME
 * options; [classify] sorts that into a [Group]. Each group's [Rule] picks the
 * engine's board, its privacy mode and the enter key's icon, which reach the
 * engine as its text box mode ("passwd", "uri" or none) and the keyboard-type
 * and return-key-type globals its iOS host used to set (see
 * SoftKeyboard.relayDelayedEvents). The defaults reproduce Keyboard 72's
 * built-in mapping.
 */
object FieldRules {
    enum class Board(val label: String, val engineType: Int) {
        LETTERS("Letters", 0),
        PECK("Peck + URL bar", 1),  // the engine's URL mode: one letter per tap, no word correction
        NUMBERS("123 board", 2),
        NUMPAD("Numpad", 3),
    }

    enum class Privacy(val label: String) {
        LEARN("Learn"),
        PRIVATE("Private"),  // the engine's password mode: no learning and no suggestions
    }

    enum class Enter(val label: String) {
        ACTION("Field's action"),  // go, next, search, send or done icons
        RETURN("Plain return"),
    }

    data class Rule(val board: Board, val privacy: Privacy, val enter: Enter) {
        fun encode() = "${board.name},${privacy.name},${enter.name}"

        companion object {
            fun decode(text: String?): Rule? = text?.split(",")?.takeIf { it.size == 3 }?.let { (b, p, e) ->
                runCatching { Rule(Board.valueOf(b), Privacy.valueOf(p), Enter.valueOf(e)) }.getOrNull()
            }
        }
    }

    enum class Group(val label: String, val detail: String, val default: Rule, val privacyOnly: Boolean = false) {
        NORMAL("Text", "Ordinary text fields", Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION)),
        MESSAGE("Messages", "Short and long messages, email subjects", Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION)),
        SEARCH("Search", "Search boxes and filter fields", Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION)),
        NAME("Names", "Person name fields", Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION)),
        ADDRESS("Postal addresses", "Street address fields", Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION)),
        EMAIL("Email addresses", "Email and web email fields", Rule(Board.PECK, Privacy.LEARN, Enter.ACTION)),
        URL("Web addresses", "URL fields", Rule(Board.PECK, Privacy.LEARN, Enter.ACTION)),
        PASSWORD("Passwords", "Password, visible password and web password fields", Rule(Board.PECK, Privacy.PRIVATE, Enter.ACTION)),
        PIN("PINs", "Numeric password fields", Rule(Board.NUMPAD, Privacy.PRIVATE, Enter.ACTION)),
        NUMBER("Numbers", "Number fields, signed or decimal", Rule(Board.NUMPAD, Privacy.LEARN, Enter.ACTION)),
        PHONE("Phone numbers", "Phone number fields", Rule(Board.NUMPAD, Privacy.LEARN, Enter.ACTION)),
        DATETIME("Dates and times", "Date, time and date-time fields", Rule(Board.NUMBERS, Privacy.LEARN, Enter.ACTION)),
        RAW("Raw key fields", "Fields with no input type: terminals, some games", Rule(Board.PECK, Privacy.PRIVATE, Enter.ACTION)),
        INCOGNITO(
            "Incognito", "Any field whose app asks for no learning (private browser tabs); applies on top of its group",
            Rule(Board.LETTERS, Privacy.LEARN, Enter.ACTION), privacyOnly = true,
        ),
    }

    /** What a field gets: engine text box mode, keyboard type and return key type. */
    data class Resolved(val group: Group, val mode: String, val keyboardType: Int, val returnKeyType: Int)

    /** The rules in force; kept current by the keyboard service from [flow]. */
    @Volatile
    var current: Map<Group, Rule> = defaults()

    fun defaults(): Map<Group, Rule> = Group.values().associateWith { it.default }

    fun classify(info: EditorInfo): Group {
        val inputType = info.inputType
        val variation = inputType and EditorInfo.TYPE_MASK_VARIATION
        return when (inputType and EditorInfo.TYPE_MASK_CLASS) {
            EditorInfo.TYPE_NULL -> Group.RAW
            EditorInfo.TYPE_CLASS_NUMBER ->
                if (variation == EditorInfo.TYPE_NUMBER_VARIATION_PASSWORD) Group.PIN else Group.NUMBER
            EditorInfo.TYPE_CLASS_PHONE -> Group.PHONE
            EditorInfo.TYPE_CLASS_DATETIME -> Group.DATETIME
            EditorInfo.TYPE_CLASS_TEXT -> when (variation) {
                EditorInfo.TYPE_TEXT_VARIATION_PASSWORD,
                EditorInfo.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD,
                EditorInfo.TYPE_TEXT_VARIATION_WEB_PASSWORD -> Group.PASSWORD
                EditorInfo.TYPE_TEXT_VARIATION_URI -> Group.URL
                EditorInfo.TYPE_TEXT_VARIATION_EMAIL_ADDRESS,
                EditorInfo.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS -> Group.EMAIL
                EditorInfo.TYPE_TEXT_VARIATION_EMAIL_SUBJECT,
                EditorInfo.TYPE_TEXT_VARIATION_SHORT_MESSAGE,
                EditorInfo.TYPE_TEXT_VARIATION_LONG_MESSAGE -> Group.MESSAGE
                EditorInfo.TYPE_TEXT_VARIATION_PERSON_NAME -> Group.NAME
                EditorInfo.TYPE_TEXT_VARIATION_POSTAL_ADDRESS -> Group.ADDRESS
                EditorInfo.TYPE_TEXT_VARIATION_FILTER -> Group.SEARCH
                else -> if (info.imeOptions and EditorInfo.IME_MASK_ACTION == EditorInfo.IME_ACTION_SEARCH) Group.SEARCH
                else Group.NORMAL
            }
            else -> Group.NORMAL
        }
    }

    fun resolve(info: EditorInfo, rules: Map<Group, Rule> = current): Resolved {
        val group = classify(info)
        val rule = rules[group] ?: group.default
        val incognito = info.imeOptions and EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0
        val private = rule.privacy == Privacy.PRIVATE ||
                (incognito && (rules[Group.INCOGNITO] ?: Group.INCOGNITO.default).privacy == Privacy.PRIVATE)
        // Password mode also stops word correction; peck mode outside it keeps
        // the engine's URL mode, as for addresses.
        val mode = when {
            private -> "passwd"
            rule.board == Board.PECK -> "uri"
            else -> ""
        }
        val returnKeyType = if (rule.enter == Enter.RETURN) 0 else returnKeyType(info.imeOptions)
        return Resolved(group, mode, rule.board.engineType, returnKeyType)
    }

    /**
     * The enter key's icon, in iOS's UIReturnKeyType numbering that the engine
     * draws: 1 go, 4 next, 6 search, 7 send, 9 done, 0 the plain return arrow
     * (also for fields whose enter key inserts a line break).
     */
    fun returnKeyType(imeOptions: Int): Int {
        if (imeOptions and EditorInfo.IME_FLAG_NO_ENTER_ACTION != 0) return 0
        return when (imeOptions and EditorInfo.IME_MASK_ACTION) {
            EditorInfo.IME_ACTION_GO -> 1
            EditorInfo.IME_ACTION_NEXT -> 4
            EditorInfo.IME_ACTION_SEARCH -> 6
            EditorInfo.IME_ACTION_SEND -> 7
            EditorInfo.IME_ACTION_DONE -> 9
            else -> 0
        }
    }

    private fun key(group: Group) = stringPreferencesKey("field_rule_${group.name.lowercase()}")

    fun flow(context: Context): Flow<Map<Group, Rule>> = context.composeSettingsDataStore.data.map { prefs ->
        Group.values().associateWith { Rule.decode(prefs[key(it)]) ?: it.default }
    }

    suspend fun set(context: Context, group: Group, rule: Rule) {
        context.composeSettingsDataStore.edit { it[key(group)] = rule.encode() }
    }

    suspend fun reset(context: Context) {
        context.composeSettingsDataStore.edit { prefs -> Group.values().forEach { prefs.remove(key(it)) } }
    }
}
