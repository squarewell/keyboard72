package com.lurebat.keyboard71

import com.jormy.nin.NINLib

/**
 * The engine's own backups, for the launch page. Exports are the text its menus
 * put in a text box; imports go to the importer its "import from clipboard"
 * uses, which asks for confirmation (USE DATA) in the keyboard. Files therefore
 * stay compatible with the original Nintype.
 *
 * The engine runs on the keyboard's render thread and only exists while the
 * keyboard is shown, so each request waits for the keyboard and gives up after
 * [TIMEOUT_MS].
 */
object Backups {
    enum class Kind(val code: Int, val label: String, val fileName: String) {
        SETTINGS(0, "Settings", "settings"),
        SHORTCUTS(3, "Shortcuts", "shortcuts"),
        WORDS(1, "Learned words", "words"),
        READABLE_WORDS(2, "Learned words (readable)", "words-readable"),
    }

    sealed interface Result {
        data class Exported(val text: ByteArray) : Result
        object ImportStarted : Result
        object NotABackup : Result
        object KeyboardNotReady : Result
    }

    const val TIMEOUT_MS = 8000L

    fun export(kind: Kind, done: (Result) -> Unit) = onEngine(done) {
        NINLib.backupExport(kind.code)?.let { Result.Exported(it) } ?: Result.KeyboardNotReady
    }

    fun import(text: ByteArray, done: (Result) -> Unit) = onEngine(done) {
        when (NINLib.backupImport(text)) {
            1 -> Result.ImportStarted
            0 -> Result.NotABackup
            else -> Result.KeyboardNotReady
        }
    }

    private fun onEngine(done: (Result) -> Unit, work: () -> Result) =
        NINView.callOnEngine(TIMEOUT_MS, Result.KeyboardNotReady, work, done)
}
