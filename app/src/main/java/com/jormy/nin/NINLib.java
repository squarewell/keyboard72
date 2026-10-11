package com.jormy.nin;

@Api
public class NINLib {
    @Api
    public static native int getUnicodeBackIndex(String str, int i);

    @Api
    public static native int getUnicodeFrontIndex(String str, int i);

    @Api
    public static native void init(int i, int i2, int i3, int i4);

    @Api
    public static native void memTestStep();

    @Api
    public static native void onChangeAppOrTextbox(String str, String str2, String str3);

    @Api
    public static native void onEditorChangeTypeClass(String str, String str2);

    @Api
    public static native void onExternalSelChange();

    @Api
    public static native void onTextSelection(String str, String str2, String str3, String str4);

    @Api
    public static native void onTouchEvent(int i, int i2, float f, float f2, float f3, float f4, long j);

    @Api
    public static native void onWordDestruction(String str, String str2);

    @Api
    public static native int processBackspaceAllowance(String str, String str2, int i);

    @Api
    public static native long processSoftKeyboardCursorMovementLeft(String str);

    @Api
    public static native long processSoftKeyboardCursorMovementRight(String str);

    @Api
    public static native void step();

    @Api
    public static native long syncTiming(long j);

    static {
        System.loadLibrary("gl2jni");
    }


    // Keyboard 72: shuts the engine down; the next call boots a fresh one (GL thread).
    public static native boolean restartEngine();

    // Keyboard 72: the boot screen's log from line `next` on (see boot_log.h), UTF-8.
    public static native byte[] bootLog(int next);

    // Keyboard 72: start-up progress, 0 (no engine) to 6 (ready); see ninlib_exports.cpp.
    public static native int engineStage();


    // Keyboard 72: the field's keyboard and return key types, engine numbering
    // (see FieldRules).
    public static native void setFieldTypes(int keyboardType, int returnKeyType);

    // Keyboard 72 backups (launch page): the engine's own export text, and its importer.
    public static native byte[] backupExport(int kind);

    public static native int backupImport(byte[] text);
}
