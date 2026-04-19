# kotlinx.serialization — keep generated serializers and descriptors intact
-keepattributes *Annotation*, InnerClasses
-dontnote kotlinx.serialization.**
-keepclassmembers class kotlinx.serialization.json.** { *** Companion; }
-keepclasseswithmembers class kotlinx.serialization.json.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class com.driscord.**$$serializer { *; }
-keepclassmembers class com.driscord.** {
    *** Companion;
    static ** INSTANCE;
}
-keepclasseswithmembers class com.driscord.** {
    kotlinx.serialization.KSerializer serializer(...);
}

# Suppress duplicate-class notes produced by multiple serialization jars on classpath
-dontnote kotlin.**
-dontwarn kotlin.**
