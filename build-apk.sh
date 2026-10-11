#!/bin/sh
# Build the APK with everything kept inside this folder by default: the Android
# SDK in .android-sdk, Gradle's cache in .gradle-home, Android user files (debug
# signing key) in .android-user. Set ANDROID_HOME, ANDROID_USER_HOME or
# GRADLE_USER_HOME to use existing ones. Needs JDK 17 (jdk17-openjdk).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk}"
export ANDROID_HOME="${ANDROID_HOME:-$here/.android-sdk}"
export ANDROID_USER_HOME="${ANDROID_USER_HOME:-$here/.android-user}"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$here/.gradle-home}"
cd "$here"
exec ./gradlew "${@:-assembleDebug}"
