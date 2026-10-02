plugins {
    id("com.android.application") version "8.7.3"
    id("org.jetbrains.kotlin.android") version "2.0.21"
}

/*
 * libemerald.so and the RomFS are not built by Gradle: tools/bootstrap.py
 * --make writes them to build/android-out (see gradle.properties).
 */
val nativeOut: File = (findProperty("emerald.nativeOut") as String? ?: "../../build/android-out")
    .let { if (File(it).isAbsolute) File(it) else file(it) }
    .canonicalFile
val allowMissingNative = (findProperty("emerald.allowMissingNative") as String?).toBoolean()
val nativeLibrary = File(nativeOut, "jniLibs/armeabi-v7a/libemerald.so")

android {
    namespace = "com.emerald3ds.android"
    compileSdk = 35
    buildToolsVersion = "35.0.0"

    defaultConfig {
        applicationId = "com.emerald3ds.android"
        minSdk = 28
        targetSdk = 35
        versionCode = (findProperty("emerald.versionCode") as String?)?.toInt() ?: 1
        versionName = findProperty("emerald.versionName") as String? ?: "0.1.0"
        ndk {
            abiFilters += "armeabi-v7a"
        }
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.srcDir(File(nativeOut, "jniLibs"))
            assets.srcDir(File(nativeOut, "assets"))
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        buildConfig = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.preference:preference-ktx:1.2.1")
    implementation("com.google.android.material:material:1.12.0")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test:core-ktx:1.6.1")
    androidTestImplementation("androidx.test.ext:junit-ktx:1.2.1")
}

val checkEmeraldNative by tasks.registering {
    val library = nativeLibrary
    val allowMissing = allowMissingNative
    doLast {
        if (!library.isFile) {
            val message = "libemerald.so not found at $library.\n" +
                "Build it with `python3 tools/bootstrap.py --make` (or point -Pemerald.nativeOut=<dir> at a\n" +
                "directory holding jniLibs/armeabi-v7a/libemerald.so and assets/romfs/), or pass\n" +
                "-Pemerald.allowMissingNative=true to build the UI without the game."
            if (allowMissing) logger.warn("warning: $message") else throw GradleException(message)
        }
    }
}

tasks.named("preBuild") {
    dependsOn(checkEmeraldNative)
}
