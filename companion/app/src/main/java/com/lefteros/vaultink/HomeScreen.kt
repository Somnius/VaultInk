package com.lefteros.vaultink

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// The masthead keeps the navy of the light theme in both themes: it is the one block of colour.
private val MastheadInk = Color(0xFF1F3A5F)
private val MastheadPaper = Color(0xFFF3F1EC)
private val MastheadQuiet = Color(0xFFB9C7D8)

/**
 * The Home tab (0.6.0): what the reader says about itself, LiveSync, the outbox and the app's own
 * update check. With no reader it says how to open the connection instead of an error.
 *
 * It shows what `GET /api/status` has: the board, the firmware version, the address, the Wi-Fi
 * signal, the uptime and the free memory. The battery, the open screen and the frontlight are not
 * in that answer, so they are not shown.
 */
@Composable
fun HomeScreen(host: String, client: DeviceClient?, status: DeviceStatus?, searching: Boolean, waiting: Int,
               onRetry: () -> Unit, onConnect: (String) -> Unit) {
    when {
        searching -> Searching()
        status == null || client == null -> Offline(host, onRetry, onConnect)
        else -> {
            Masthead(host, status)
            if (status.hotspot) {
                Text(stringResource(R.string.hotspot_warning), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error)
            }
            LiveSyncSection(client)
        }
    }
    SectionLabel(stringResource(R.string.home_outbox))
    Group {
        ValueRow(stringResource(R.string.home_outbox_waiting), waiting.toString())
        Hairline()
        Text(stringResource(R.string.home_outbox_hint), Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
    SectionLabel(stringResource(R.string.home_app))
    Group {
        ValueRow(stringResource(R.string.home_app_version), BuildConfig.VERSION_NAME)
        Hairline()
        UpdateCheck()
        if (status != null && !searching) {
            Hairline()
            GroupRow(stringResource(R.string.search_again), onClick = onRetry)
        }
    }
    Text(stringResource(R.string.credit), Modifier.padding(start = 4.dp, top = 4.dp), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
private fun Masthead(host: String, status: DeviceStatus) {
    Surface(Modifier.fillMaxWidth(), shape = MaterialTheme.shapes.large, color = MastheadInk, contentColor = MastheadPaper) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 20.dp)) {
            Text(stringResource(R.string.home_connected), style = MaterialTheme.typography.labelMedium, color = MastheadQuiet)
            Text(deviceName(status.device).ifEmpty { stringResource(R.string.app_name) }, style = MaterialTheme.typography.displaySmall,
                maxLines = 2)
            Text(stringResource(R.string.home_version_at, status.version, host),
                style = MaterialTheme.typography.bodyMedium.copy(fontFeatureSettings = TABULAR), color = MastheadQuiet)
            Row(Modifier.fillMaxWidth().padding(top = 20.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                val signal = if (status.hotspot || status.rssi >= 0) "–" else status.rssi.toString()
                val quality = listOf(R.string.home_signal_none, R.string.home_signal_weak, R.string.home_signal_fair,
                    R.string.home_signal_good, R.string.home_signal_strong)[if (status.hotspot) 0 else signalBars(status.rssi)]
                Figure(signal, stringResource(R.string.home_signal_unit), stringResource(quality), Modifier.weight(1f))
                Figure(uptimeText(status.uptime, stringResource(R.string.unit_day), stringResource(R.string.unit_hour),
                    stringResource(R.string.unit_minute), stringResource(R.string.unit_second)), "",
                    stringResource(R.string.home_uptime), Modifier.weight(1f))
                Figure((status.freeHeap / 1024).toString(), "KB", stringResource(R.string.home_memory), Modifier.weight(1f))
            }
        }
    }
}

/** One number of the masthead: the figure in tabular digits, its unit, and what it is underneath. */
@Composable
private fun Figure(value: String, unit: String, label: String, modifier: Modifier = Modifier) {
    Column(modifier) {
        Row(verticalAlignment = Alignment.Bottom) {
            Text(value, style = MaterialTheme.typography.titleLarge.copy(fontFamily = FontFamily.Default,
                fontFeatureSettings = TABULAR), maxLines = 1)
            if (unit.isNotEmpty()) {
                Text(unit, Modifier.padding(start = 4.dp, bottom = 3.dp), style = MaterialTheme.typography.labelSmall,
                    color = MastheadQuiet)
            }
        }
        Text(label, style = MaterialTheme.typography.labelSmall, color = MastheadQuiet)
    }
}

@Composable
fun Searching() {
    Row(Modifier.padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
        Text(stringResource(R.string.searching))
    }
    Hint(R.string.connect_checklist)
}

/** No reader: what to do on the reader, Try again, and the address by hand for when mDNS finds nothing. */
@Composable
private fun Offline(host: String, onRetry: () -> Unit, onConnect: (String) -> Unit) {
    var address by remember { mutableStateOf(host) }
    Text(stringResource(R.string.offline_title), Modifier.padding(top = 8.dp), style = MaterialTheme.typography.headlineMedium)
    Text(stringResource(R.string.offline_body), style = MaterialTheme.typography.bodyLarge)
    Text(stringResource(R.string.not_found_step2), style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
    Button(onClick = onRetry, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.try_again)) }
    SectionLabel(stringResource(R.string.offline_address_label))
    Text(stringResource(R.string.not_found_step3), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedTextField(address, { address = it }, label = { Text(stringResource(R.string.address)) },
            placeholder = { Text(stringResource(R.string.address_hint)) }, singleLine = true, modifier = Modifier.weight(1f))
        TextButton(enabled = address.isNotBlank(), onClick = { onConnect(address) }) { Text(stringResource(R.string.connect)) }
    }
}

/** A short "can't reach the reader" for the tabs that need it, with the way back. */
@Composable
fun OfflineNotice(onRetry: () -> Unit) {
    Text(stringResource(R.string.offline_title), Modifier.padding(top = 8.dp), style = MaterialTheme.typography.headlineSmall)
    Text(stringResource(R.string.offline_body))
    Button(onClick = onRetry) { Text(stringResource(R.string.try_again)) }
}

/**
 * LiveSync as the reader reports it (firmware 0.63.0+). Sync now appears only when the firmware
 * says it can take a sync request from here; today's firmware can't (a sync runs on the LiveSync
 * screen, and leaving File Transfer restarts the reader), so from 0.64.0 the button leaves a
 * request file instead, and the section says so.
 */
@Composable
fun LiveSyncSection(client: DeviceClient) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var status by remember(client) { mutableStateOf<LiveSyncStatus?>(null) }
    var supported by remember(client) { mutableStateOf(true) }
    var message by remember(client) { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    suspend fun refresh() {
        withContext(Dispatchers.IO) { runCatching { client.liveSyncStatus() } }
            .onSuccess { status = it; supported = it != null }
            .onFailure { message = context.getString(R.string.livesync_failed, it.message ?: "") }
    }
    LaunchedEffect(client) { refresh() }
    SectionLabel(stringResource(R.string.livesync_title))
    val s = status
    Group {
        when {
            !supported -> GroupRow(stringResource(R.string.livesync_unsupported))
            s == null -> GroupRow(if (message.isEmpty()) stringResource(R.string.loading) else message)
            s.pendingSetup && !s.configured -> GroupRow(stringResource(R.string.livesync_pending))
            !s.configured -> GroupRow(stringResource(R.string.livesync_not_set_up))
            else -> {
                if (s.vault.isNotEmpty()) {
                    ValueRow(stringResource(R.string.vault), s.vault)
                    Hairline()
                }
                GroupRow(stringResource(R.string.home_last_sync),
                    if (s.running) stringResource(R.string.livesync_running)
                    else s.lastResult.ifEmpty { stringResource(R.string.livesync_never) })
                if (s.autoHours > 0) {
                    val tried = if (s.lastAttempt > 0) java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT,
                        java.text.DateFormat.SHORT).format(java.util.Date(s.lastAttempt * 1000)) else "–"
                    Hairline()
                    Text(stringResource(R.string.livesync_auto, s.autoHours, tried),
                        Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
                        style = MaterialTheme.typography.bodySmall.copy(fontFeatureSettings = TABULAR),
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
        // More than one vault (firmware 0.77.0+): the others. The lines above are already about the
        // first profile that is set up, so that one is not listed again.
        val above = if (s != null && s.configured) s.profiles.firstOrNull { it.configured }?.index else null
        if (s != null && s.profiles.size > 1) s.profiles.filter { it.index != above }.forEach { p ->
            Hairline()
            GroupRow(stringResource(R.string.home_profile, p.index, p.vault.ifEmpty { "–" }), when {
                p.configured -> p.lastResult.ifEmpty { stringResource(R.string.livesync_never) }
                p.pendingSetup -> stringResource(R.string.livesync_pending)
                else -> stringResource(R.string.livesync_not_set_up)
            })
        }
    }
    if (s != null && s.configured) {
        if (s.canSync) {
            Button(enabled = !busy && !s.running, modifier = Modifier.fillMaxWidth(), onClick = {
                busy = true
                scope.launch {
                    withContext(Dispatchers.IO) { runCatching { client.liveSyncNow() } }
                        .onFailure { message = context.getString(R.string.action_failed, it.message ?: "") }
                    refresh()
                    busy = false
                }
            }) { Text(stringResource(R.string.livesync_sync_now)) }
        } else if (s.syncRequest) {
            // The request is a file the reader picks up at the wake after File Transfer.
            Button(enabled = !busy, modifier = Modifier.fillMaxWidth(), onClick = {
                busy = true
                scope.launch {
                    message = withContext(Dispatchers.IO) { runCatching { client.requestLiveSync() } }
                        .fold({ context.getString(R.string.livesync_requested) },
                              { context.getString(R.string.action_failed, it.message ?: "") })
                    busy = false
                }
            }) { Text(stringResource(R.string.livesync_request)) }
            Hint(R.string.livesync_request_hint)
        } else {
            Hint(R.string.livesync_no_remote)
        }
        if (message.isNotEmpty()) Text(message, style = MaterialTheme.typography.bodyMedium)
    }
}

/** Check for a newer companion app, as a row; works with or without a reader connected. */
@Composable
fun UpdateCheck() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var update by remember { mutableStateOf<AppUpdate?>(null) }
    var message by remember { mutableStateOf("") }
    GroupRow(stringResource(R.string.check_update), message.ifEmpty { null }, onClick = {
        scope.launch {
            // The check runs in the background; what it found is written here, on the main thread.
            withContext(Dispatchers.IO) { runCatching { Updater.newer(BuildConfig.VERSION_NAME) } }
                .onSuccess { found ->
                    update = found
                    message = if (found == null) context.getString(R.string.update_none, BuildConfig.VERSION_NAME) else ""
                }
                .onFailure { message = context.getString(R.string.update_failed, it.message ?: "") }
        }
    })
    update?.let { found ->
        AlertDialog(
            onDismissRequest = { update = null },
            text = { Text(stringResource(R.string.update_found, found.version)) },
            confirmButton = {
                TextButton(onClick = {
                    update = null
                    scope.launch {
                        withContext(Dispatchers.IO) { runCatching { Updater.install(context, found) } }
                            .onFailure { message = context.getString(R.string.update_failed, it.message ?: "") }
                    }
                }) { Text(stringResource(R.string.update_install)) }
            },
            dismissButton = { TextButton(onClick = { update = null }) { Text(stringResource(R.string.update_later)) } },
        )
    }
}

@Composable
fun Hint(text: Int) {
    Text(stringResource(text), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** How many items wait in the outbox, as one quiet line; nothing when it is empty. */
@Composable
fun OutboxLine(waiting: Int) {
    if (waiting > 0) {
        Text(pluralStringResource(R.plurals.outbox_waiting, waiting, waiting), style = MaterialTheme.typography.bodyMedium)
    }
}
