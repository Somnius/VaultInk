package com.lefteros.vaultink

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException

/** What a restore did, for the line under the section. */
private data class RestoreResult(val changed: Int, val added: Int, val failed: Int, val error: String)

/**
 * Backup in the Settings tab (0.6.0): save the reader's settings and lists as one JSON file through
 * the system's save dialog, and put such a file back. The file format, what is left out of it
 * (every password) and the restore plan are in Backup.kt.
 */
@Composable
fun BackupSection(client: DeviceClient, status: DeviceStatus?, onRestored: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf("") }
    // The backup waits here while the save dialog is open (it holds no secret, so it may be kept over a rotation).
    var pending by rememberSaveable { mutableStateOf<String?>(null) }
    var restore by remember { mutableStateOf<Pair<BackupFile, RestorePlan>?>(null) }
    fun failed(e: Throwable) = context.getString(R.string.action_failed, e.message ?: "")

    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
        val text = pending
        pending = null
        if (uri == null || text == null) return@rememberLauncherForActivityResult
        scope.launch {
            message = withContext(Dispatchers.IO) {
                runCatching { context.contentResolver.openOutputStream(uri)!!.use { it.write(text.toByteArray()) } }
            }.fold({ context.getString(R.string.backup_saved) }, ::failed)
        }
    }
    val open = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        busy = true
        scope.launch {
            withContext(Dispatchers.IO) {
                runCatching {
                    val bytes = context.contentResolver.openInputStream(uri)!!.use { readCapped(it, Backup.MAX_BYTES) }
                        ?: throw BackupError(BackupError.Reason.NOT_A_BACKUP)
                    val file = Backup.parse(String(bytes))
                    // A fresh look at the reader, so only what it knows and doesn't have yet is sent.
                    file to Backup.plan(file, client.settings(),
                        client.getOrNull("api/wifi")?.let(ReaderApi::parseWifi),
                        client.getOrNull("api/opds")?.let(ReaderApi::parseOpds),
                        client.getOrNull("api/remotes")?.let(ReaderApi::parseRemotes))
                }
            }.onSuccess { restore = it }.onFailure {
                message = when ((it as? BackupError)?.reason) {
                    BackupError.Reason.NOT_A_BACKUP -> context.getString(R.string.backup_not_backup)
                    BackupError.Reason.NEWER_VERSION -> context.getString(R.string.backup_newer)
                    null -> failed(it)
                }
            }
            busy = false
        }
    }

    SectionLabel(stringResource(R.string.backup_section))
    Group {
        GroupRow(stringResource(R.string.backup_save), stringResource(R.string.backup_save_note), onClick = {
            if (busy) return@GroupRow
            busy = true
            scope.launch {
                withContext(Dispatchers.IO) {
                    runCatching {
                        Backup.build(BuildConfig.VERSION_NAME, status?.version.orEmpty(), status?.device.orEmpty(),
                            java.time.OffsetDateTime.now().withNano(0).toString(),
                            client.getOrNull("api/settings") ?: throw IOException("HTTP 404"),
                            client.getOrNull("api/wifi"), client.getOrNull("api/opds"), client.getOrNull("api/remotes"))
                    }
                }.onSuccess {
                    pending = it
                    message = ""
                    save.launch(Backup.fileName(java.time.LocalDate.now().toString()))
                }.onFailure { message = failed(it) }
                busy = false
            }
        })
        Hairline()
        GroupRow(stringResource(R.string.backup_restore), stringResource(R.string.backup_restore_note), onClick = {
            // Any file can be offered: file managers don't agree on a type for .json.
            if (!busy) open.launch(arrayOf("application/json", "text/plain", "application/octet-stream", "*/*"))
        })
    }
    Hint(R.string.backup_no_passwords)
    if (message.isNotEmpty()) Text(message, style = MaterialTheme.typography.bodyMedium)

    restore?.let { (file, plan) ->
        val nothing = plan.settings.isEmpty() && plan.wifi.isEmpty() && plan.opds.isEmpty() && plan.remotes.isEmpty()
        AlertDialog(
            onDismissRequest = { restore = null },
            title = { Text(stringResource(R.string.backup_restore_title)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.backup_sum_from, deviceName(file.readerDevice).ifEmpty { "VaultInk" },
                        file.readerVersion.ifEmpty { "?" }, file.date.take(10).ifEmpty { "?" }))
                    Text(stringResource(R.string.backup_sum_settings, plan.settings.size, plan.settingsSame, plan.settingsSkipped.size))
                    Text(stringResource(R.string.backup_sum_lists, plan.wifi.size, plan.opds.size, plan.remotes.size, plan.listsPresent))
                    if (nothing) Text(stringResource(R.string.backup_sum_nothing))
                    Hint(R.string.backup_no_passwords)
                }
            },
            confirmButton = {
                TextButton(enabled = !nothing, onClick = {
                    restore = null
                    busy = true
                    scope.launch {
                        val r = withContext(Dispatchers.IO) { apply(client, plan) }
                        message = context.getString(R.string.backup_done, r.changed, r.added, plan.settingsSame + plan.listsPresent,
                            plan.settingsSkipped.size, r.failed) +
                            (if (plan.settingsSkipped.isEmpty()) "" else "\n" + context.getString(R.string.backup_done_skipped,
                                plan.settingsSkipped.joinToString(", "))) +
                            (if (r.error.isEmpty()) "" else "\n" + context.getString(R.string.action_failed, r.error))
                        busy = false
                        onRestored()
                    }
                }) { Text(stringResource(R.string.backup_restore_confirm)) }
            },
            dismissButton = { TextButton(onClick = { restore = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

/** Sends the plan: the settings a few at a time, then each missing entry, without a password. Counts what the reader took. */
private fun apply(client: DeviceClient, plan: RestorePlan): RestoreResult {
    var changed = 0
    var added = 0
    var failed = 0
    var error = ""
    fun attempt(count: Int, onOk: (Int) -> Unit, action: () -> Unit) {
        runCatching(action).onSuccess { onOk(count) }.onFailure { failed += count; if (error.isEmpty()) error = it.message ?: "" }
    }
    plan.settings.entries.chunked(16).forEach { part ->
        attempt(part.size, { changed += it }) { client.setSettings(part.associate { it.key to it.value }) }
    }
    plan.wifi.forEach { attempt(1, { added += it }) { client.saveWifi(it, "") } }
    plan.opds.forEach { attempt(1, { added += it }) { client.saveOpds(it.name, it.url, it.username, "") } }
    plan.remotes.forEach { attempt(1, { added += it }) { client.saveRemote(it.name, it.protocol, it.url, it.username, "", it.verifyCert) } }
    return RestoreResult(changed, added, failed, error)
}
