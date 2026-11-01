package com.lefteros.vaultink

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

const val REPO_URL = "https://github.com/Somnius/VaultInk"
const val PROJECT_URL = "https://lefteros.com/en/apps/vaultink"
const val RELEASES_URL = "https://github.com/Somnius/VaultInk/releases"

// What this app is built with: the dependencies and settings in app/build.gradle.kts.
private val APP_STACK = listOf("Kotlin", "Jetpack Compose", "Material 3", "Kotlin coroutines", "OkHttp",
    "AndroidX WebView", "Gradle", "Android 8.0+ (API ${BuildConfig.MIN_SDK})", "target API ${BuildConfig.TARGET_SDK}")
// What the reader's firmware is built with: README badges and platformio.ini.
private val FIRMWARE_STACK = listOf("C++20", "ESP32-S3", "PlatformIO", "Arduino + FreeRTOS", "NimBLE", "Expat",
    "FreeInk SDK", "CrossPoint Reader", "Obsidian", "Self-hosted LiveSync", "English · Ελληνικά")

/** App name and version, what it is, links, the tech stack as pills, credits. */
@Composable
fun AboutScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    BackHandler(onBack = onBack)
    fun open(url: String) = runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium)
        Text(stringResource(R.string.about_version, BuildConfig.VERSION_NAME), style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(stringResource(R.string.about_what))
        Text(stringResource(R.string.credit), style = MaterialTheme.typography.bodySmall)

        Text(stringResource(R.string.about_links), style = MaterialTheme.typography.titleMedium)
        Column {
            TextButton(onClick = { open(REPO_URL) }) { Text(stringResource(R.string.about_repo)) }
            TextButton(onClick = { open(PROJECT_URL) }) { Text(stringResource(R.string.about_project_page)) }
            TextButton(onClick = { open(RELEASES_URL) }) { Text(stringResource(R.string.about_releases)) }
        }

        Text(stringResource(R.string.about_stack_app), style = MaterialTheme.typography.titleMedium)
        Pills(APP_STACK)
        Text(stringResource(R.string.about_stack_firmware), style = MaterialTheme.typography.titleMedium)
        Pills(FIRMWARE_STACK)

        Text(stringResource(R.string.about_credits), style = MaterialTheme.typography.titleMedium)
        Text("• " + stringResource(R.string.about_credit_tester))
        Text("• " + stringResource(R.string.about_credit_crosspoint))
        Text("• " + stringResource(R.string.about_credit_banana))
    }
}

/** Rounded, non-clickable chips that wrap onto as many rows as they need. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun Pills(items: List<String>) {
    FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items.forEach { label ->
            Surface(shape = RoundedCornerShape(50), color = MaterialTheme.colorScheme.secondaryContainer) {
                Text(label, Modifier.padding(horizontal = 12.dp, vertical = 6.dp), style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSecondaryContainer)
            }
        }
    }
}
