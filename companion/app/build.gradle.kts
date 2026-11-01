import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Release signing: companion/signing.properties (not in git) names a keystore kept outside the
// repository (storeFile, storePassword, keyAlias, keyPassword).
val signing = Properties().apply {
    val file = rootProject.file("signing.properties")
    if (file.exists()) file.inputStream().use { load(it) }
}

android {
    namespace = "com.lefteros.vaultink"
    compileSdk = 36
    buildToolsVersion = "36.0.0"

    defaultConfig {
        applicationId = "com.lefteros.vaultink"
        minSdk = 26
        targetSdk = 36
        versionCode = 5
        versionName = "0.5.0"
        // Shown as pills on the About screen.
        buildConfigField("int", "MIN_SDK", "$minSdk")
        buildConfigField("int", "TARGET_SDK", "$targetSdk")
    }

    signingConfigs {
        if (signing.getProperty("storeFile") != null) {
            create("release") {
                storeFile = file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }
}

kotlin {
    jvmToolchain(17)
}

// Help: the repo's documentation goes into the APK as assets at build time. Only the Markdown files
// (README.md as the start page, docs/*.md), no images, so the APK stays small. help/index.txt lists
// them as "file<TAB>title" in reading order, README first.
val helpDocsDir = layout.buildDirectory.dir("generated/helpdocs")
val copyHelpDocs by tasks.registering(Copy::class) {
    from(rootProject.file("../README.md"))
    from(rootProject.file("../docs")) { include("*.md") }
    into(helpDocsDir.map { it.dir("help") })
    doLast {
        val dir = helpDocsDir.get().dir("help").asFile
        val heading = Regex("""^#\s+(.+?)\s*$""")
        val files = dir.listFiles { f -> f.extension == "md" }!!.sortedWith(compareBy({ it.name != "README.md" }, { it.name }))
        dir.resolve("index.txt").writeText(files.joinToString("\n") { f ->
            // README.md has no level-1 heading (its title is a picture), so it's named by hand.
            val title = if (f.name == "README.md") "VaultInk"
                else f.readLines().firstNotNullOfOrNull { heading.find(it)?.groupValues?.get(1) } ?: f.nameWithoutExtension
            "${f.name}\t$title"
        })
    }
}
android.sourceSets["main"].assets.srcDir(helpDocsDir)
tasks.named("preBuild") { dependsOn(copyHelpDocs) }

dependencies {
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("androidx.webkit:webkit:1.12.1")
    implementation(platform("androidx.compose:compose-bom:2026.06.01"))
    implementation("androidx.activity:activity-compose:1.13.0")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    testImplementation("junit:junit:4.13.2")
}
