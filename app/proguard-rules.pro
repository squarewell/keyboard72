# The original engine calls into the app through JNI by class and method name
# (com.jormy.nin.*, and the keyboard classes those forward to), so keep the
# app's own classes whole; R8 still shrinks the libraries.
-keep class com.jormy.nin.** { *; }
-keep class com.lurebat.keyboard71.** { *; }

# The Tasker plugin library finds its input and output fields by reflection.
-keep class com.joaomgcd.taskerpluginlibrary.** { *; }
-keepattributes *Annotation*, Signature, InnerClasses, EnclosingMethod
