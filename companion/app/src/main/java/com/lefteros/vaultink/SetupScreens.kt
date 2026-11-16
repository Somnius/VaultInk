package com.lefteros.vaultink

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// Reader setup (0.6.0): the lists the reader's own Settings web page edits, from the phone. Each
// screen reads its list from the reader, shows it as one group, and has one form with one Save.
// Passwords are typed into SecretField, sent in the request and dropped: they are not logged, not
// put in preferences or saved state, and the reader never sends one back.

/** The Settings rows that open these screens, in order: page key and its title. */
val SETUP_PAGES = listOf(
    "wifi" to R.string.setup_wifi,
    "livesync" to R.string.setup_livesync,
    "opds" to R.string.setup_opds,
    "remotes" to R.string.setup_remotes,
    "fonts" to R.string.setup_fonts,
)

@Composable
fun SetupScreen(page: String, client: DeviceClient, onBack: () -> Unit) {
    when (page) {
        "wifi" -> WifiSetupScreen(client, onBack)
        "livesync" -> LiveSyncSetupScreen(client, onBack)
        "opds" -> OpdsSetupScreen(client, onBack)
        "remotes" -> RemotesSetupScreen(client, onBack)
        else -> FontsSetupScreen(client, onBack)
    }
}

/** A list read from the reader, the line about the last action, and whether one is running. */
private class Remote<T> {
    var value by mutableStateOf<T?>(null)
    var error by mutableStateOf("")
    var message by mutableStateOf("")
    var busy by mutableStateOf(false)
    var reload: () -> Unit = {}
}

@Composable
private fun <T> rememberRemote(client: DeviceClient, fetch: DeviceClient.() -> T): Pair<Remote<T>, CoroutineScope> {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val state = remember(client) { Remote<T>() }
    state.reload = {
        scope.launch {
            withContext(Dispatchers.IO) { runCatching { client.fetch() } }
                .onSuccess { state.value = it; state.error = "" }
                .onFailure { state.error = context.getString(R.string.setup_load_failed, it.message ?: "") }
        }
    }
    LaunchedEffect(client) { state.reload() }
    return state to scope
}

/** Runs one change on the reader, says how it went and reads the list again. */
private fun Remote<*>.run(scope: CoroutineScope, context: android.content.Context, ok: String, action: () -> Unit,
                          done: () -> Unit = {}) {
    busy = true
    scope.launch {
        val result = withContext(Dispatchers.IO) { runCatching(action) }
        message = result.fold({ ok }, { context.getString(R.string.action_failed, it.message ?: "") })
        if (result.isSuccess) done()
        busy = false
        reload()
    }
}

/** The list as one group: loading, the error, "none yet", or the rows with hairlines between them. */
@Composable
private fun <T> ListGroup(state: Remote<List<T>>, empty: Int, row: @Composable (T) -> Unit) {
    val list = state.value
    Group {
        when {
            state.error.isNotEmpty() -> GroupRow(state.error)
            list == null -> GroupRow(stringResource(R.string.loading))
            list.isEmpty() -> GroupRow(stringResource(empty))
            else -> list.forEachIndexed { i, item ->
                if (i > 0) Hairline()
                row(item)
            }
        }
    }
}

@Composable
private fun ConfirmDelete(name: String, onDismiss: () -> Unit, onConfirm: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        text = { Text(stringResource(R.string.setup_delete_ask, name)) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(stringResource(R.string.delete)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun Field(value: String, onChange: (String) -> Unit, label: Int, keyboard: KeyboardType = KeyboardType.Text,
                  supporting: String? = null, error: Boolean = false) {
    OutlinedTextField(value, onChange, label = { Text(stringResource(label)) }, singleLine = true, isError = error,
        modifier = Modifier.fillMaxWidth(), supportingText = supporting?.let { { Text(it) } },
        keyboardOptions = KeyboardOptions(keyboardType = keyboard, autoCorrectEnabled = false))
}

/** Save as the screen's one filled button, Cancel beside it while an entry is being changed. */
@Composable
private fun SaveRow(enabled: Boolean, editing: Boolean, onCancel: () -> Unit, onSave: () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(enabled = enabled, onClick = onSave) { Text(stringResource(R.string.setup_save)) }
        if (editing) TextButton(onClick = onCancel) { Text(stringResource(R.string.cancel)) }
    }
}

@Composable
private fun Message(text: String) {
    if (text.isNotEmpty()) Text(text, style = MaterialTheme.typography.bodyMedium)
}

@Composable
private fun passwordNote(hasPassword: Boolean) =
    stringResource(if (hasPassword) R.string.setup_password_saved else R.string.setup_password_none)

// ---- Wi-Fi networks: GET/POST /api/wifi, POST /api/wifi/delete ----

@Composable
fun WifiSetupScreen(client: DeviceClient, onBack: () -> Unit) = SubPage(stringResource(R.string.setup_wifi), onBack) {
    val context = LocalContext.current
    val (state, scope) = rememberRemote(client) { wifiNetworks() }
    // The entry being changed, by its index on the reader; kept over a rotation with the typed fields.
    var editIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    var editName by rememberSaveable { mutableStateOf("") }
    var deleting by remember { mutableStateOf<WifiNetwork?>(null) }
    var ssid by rememberSaveable { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    fun clear() { editIndex = null; editName = ""; ssid = ""; password = "" }
    Hint(R.string.setup_wifi_hint)
    ListGroup(state, R.string.setup_wifi_none) { n ->
        val last = if (n.isLastConnected) " · " + stringResource(R.string.setup_wifi_last) else ""
        GroupRow(n.ssid, passwordNote(n.hasPassword) + last, onClick = { editIndex = n.index; editName = n.ssid; ssid = n.ssid; password = "" }) {
            TextButton(enabled = !state.busy, onClick = { deleting = n }) { Text(stringResource(R.string.delete)) }
        }
    }
    SectionLabel(editIndex?.let { stringResource(R.string.setup_change, editName) } ?: stringResource(R.string.setup_wifi_add))
    Field(ssid, { ssid = it }, R.string.setup_wifi_ssid)
    SecretField(password, { password = it }, stringResource(R.string.setup_password),
        if (editIndex != null) stringResource(R.string.setup_password_keep) else null)
    SaveRow(!state.busy && ssid.isNotBlank(), editIndex != null, ::clear) {
        val (s, p, index) = Triple(ssid, password, editIndex)
        password = ""
        state.run(scope, context, context.getString(R.string.saved), { client.saveWifi(s, p, index) }, ::clear)
    }
    Message(state.message)
    Hint(R.string.setup_plain_http)
    deleting?.let { n ->
        ConfirmDelete(n.ssid, { deleting = null }) {
            deleting = null
            if (editIndex != null) clear()  // the indexes move when one goes
            state.run(scope, context, context.getString(R.string.deleted), { client.deleteWifi(n.index) })
        }
    }
}

// ---- OPDS catalogues: GET/POST /api/opds, POST /api/opds/delete ----

@Composable
fun OpdsSetupScreen(client: DeviceClient, onBack: () -> Unit) = SubPage(stringResource(R.string.setup_opds), onBack) {
    val context = LocalContext.current
    val (state, scope) = rememberRemote(client) { opdsServers() }
    // The entry being changed, by its index on the reader; kept over a rotation with the typed fields.
    var editIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    var editName by rememberSaveable { mutableStateOf("") }
    var deleting by remember { mutableStateOf<OpdsServer?>(null) }
    var name by rememberSaveable { mutableStateOf("") }
    var url by rememberSaveable { mutableStateOf("") }
    var username by rememberSaveable { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    fun clear() { editIndex = null; editName = ""; name = ""; url = ""; username = ""; password = "" }
    Hint(R.string.setup_opds_hint)
    ListGroup(state, R.string.setup_opds_none) { s ->
        GroupRow(s.name.ifEmpty { s.url }, s.url, onClick = { editIndex = s.index; editName = s.name.ifEmpty { s.url }; name = s.name; url = s.url; username = s.username; password = "" }) {
            TextButton(enabled = !state.busy, onClick = { deleting = s }) { Text(stringResource(R.string.delete)) }
        }
    }
    SectionLabel(editIndex?.let { stringResource(R.string.setup_change, editName) }
        ?: stringResource(R.string.setup_opds_add))
    Field(name, { name = it }, R.string.setup_name)
    Field(url, { url = it }, R.string.setup_url, KeyboardType.Uri)
    Field(username, { username = it }, R.string.setup_username)
    SecretField(password, { password = it }, stringResource(R.string.setup_password),
        if (editIndex != null) stringResource(R.string.setup_password_keep) else null)
    SaveRow(!state.busy && url.isNotBlank(), editIndex != null, ::clear) {
        val p = password
        val (n, u, user) = Triple(name, url, username)
        val index = editIndex
        password = ""
        state.run(scope, context, context.getString(R.string.saved), { client.saveOpds(n, u, user, p, index) }, ::clear)
    }
    Message(state.message)
    Hint(R.string.setup_plain_http)
    deleting?.let { s ->
        ConfirmDelete(s.name.ifEmpty { s.url }, { deleting = null }) {
            deleting = null
            if (editIndex != null) clear()
            state.run(scope, context, context.getString(R.string.deleted), { client.deleteOpds(s.index) })
        }
    }
}

// ---- Remote servers (WebDAV, FTP; firmware 0.74.0+): GET/POST /api/remotes, POST /api/remotes/delete ----

@Composable
fun RemotesSetupScreen(client: DeviceClient, onBack: () -> Unit) = SubPage(stringResource(R.string.setup_remotes), onBack) {
    val context = LocalContext.current
    val (state, scope) = rememberRemote(client) { remotes() }
    // The entry being changed, by its index on the reader; kept over a rotation with the typed fields.
    var editIndex by rememberSaveable { mutableStateOf<Int?>(null) }
    var editName by rememberSaveable { mutableStateOf("") }
    var deleting by remember { mutableStateOf<RemoteServer?>(null) }
    var name by rememberSaveable { mutableStateOf("") }
    var protocol by rememberSaveable { mutableStateOf("webdav") }
    var url by rememberSaveable { mutableStateOf("") }
    var username by rememberSaveable { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var verify by rememberSaveable { mutableStateOf(true) }
    fun clear() { editIndex = null; editName = ""; name = ""; protocol = "webdav"; url = ""; username = ""; password = ""; verify = true }
    Hint(R.string.setup_remotes_hint)
    ListGroup(state, R.string.setup_remotes_none) { s ->
        val plain = if (s.plain) " · " + stringResource(R.string.setup_remote_plain) else ""
        GroupRow(s.name.ifEmpty { s.url }, s.protocol.uppercase() + " · " + s.url + plain, onClick = {
            editIndex = s.index; editName = s.name.ifEmpty { s.url }; name = s.name; protocol = if (s.protocol == "ftp") "ftp" else "webdav"; url = s.url
            username = s.username; password = ""; verify = s.verifyCert
        }) {
            TextButton(enabled = !state.busy, onClick = { deleting = s }) { Text(stringResource(R.string.delete)) }
        }
    }
    SectionLabel(editIndex?.let { stringResource(R.string.setup_change, editName) }
        ?: stringResource(R.string.setup_remotes_add))
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FilterChip(protocol == "webdav", { protocol = "webdav" }, label = { Text("WebDAV") })
        FilterChip(protocol == "ftp", { protocol = "ftp" }, label = { Text("FTP") })
    }
    Field(name, { name = it }, R.string.setup_name)
    Field(url, { url = it }, R.string.setup_url, KeyboardType.Uri)
    Field(username, { username = it }, R.string.setup_username)
    SecretField(password, { password = it }, stringResource(R.string.setup_password),
        if (editIndex != null) stringResource(R.string.setup_password_keep) else null)
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.setup_remote_verify), Modifier.weight(1f))
        Switch(verify, { verify = it })
    }
    if (protocol == "ftp" || url.trim().startsWith("http://")) {
        Text(stringResource(R.string.setup_remote_plain_note), style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.error)
    }
    SaveRow(!state.busy && url.isNotBlank(), editIndex != null, ::clear) {
        val p = password
        val (n, u, user) = Triple(name, url, username)
        val (proto, check, index) = Triple(protocol, verify, editIndex)
        password = ""
        state.run(scope, context, context.getString(R.string.saved),
            { client.saveRemote(n, proto, u, user, p, check, index) }, ::clear)
    }
    Message(state.message)
    Hint(R.string.setup_plain_http)
    deleting?.let { s ->
        ConfirmDelete(s.name.ifEmpty { s.url }, { deleting = null }) {
            deleting = null
            if (editIndex != null) clear()
            state.run(scope, context, context.getString(R.string.deleted), { client.deleteRemote(s.index) })
        }
    }
}

// ---- LiveSync setup: POST /api/livesync/setup, with the profiles of GET /api/livesync/status ----

@Composable
fun LiveSyncSetupScreen(client: DeviceClient, onBack: () -> Unit) = SubPage(stringResource(R.string.setup_livesync), onBack) {
    val context = LocalContext.current
    val (state, scope) = rememberRemote(client) { liveSyncStatus()?.profiles ?: emptyList() }
    var uri by remember { mutableStateOf("") }
    var vault by rememberSaveable { mutableStateOf("") }
    var profile by rememberSaveable { mutableStateOf<Int?>(null) }
    Hint(R.string.setup_livesync_hint)
    ListGroup(state, R.string.setup_livesync_none) { p ->
        GroupRow(stringResource(R.string.home_profile, p.index, p.vault.ifEmpty { "–" }), when {
            p.configured -> p.lastResult.ifEmpty { stringResource(R.string.livesync_never) }
            p.pendingSetup -> stringResource(R.string.livesync_pending)
            else -> stringResource(R.string.livesync_not_set_up)
        })
    }
    SectionLabel(stringResource(R.string.setup_livesync_add))
    // The Setup URI carries the server's address and login (encrypted), so it is shown as dots like a password.
    SecretField(uri, { uri = it }, stringResource(R.string.setup_livesync_uri), stringResource(R.string.setup_livesync_uri_note))
    val vaultOk = ReaderApi.isVaultFolder(vault)
    Field(vault, { vault = it }, R.string.setup_livesync_vault, error = !vaultOk,
        supporting = stringResource(if (vaultOk) R.string.setup_livesync_vault_note else R.string.setup_livesync_vault_bad))
    Text(stringResource(R.string.setup_livesync_profile), style = MaterialTheme.typography.bodyMedium)
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        FilterChip(profile == null, { profile = null }, label = { Text(stringResource(R.string.setup_livesync_profile_auto)) })
        (1..4).forEach { n -> FilterChip(profile == n, { profile = n }, label = { Text(n.toString()) }) }
    }
    val uriOk = ReaderApi.isSetupUri(uri)
    if (uri.isNotBlank() && !uriOk) {
        Text(stringResource(R.string.setup_livesync_uri_bad), style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.error)
    }
    Button(enabled = !state.busy && uriOk && vaultOk, onClick = {
        val (u, v, p) = Triple(uri, vault, profile)
        uri = ""
        var reply: LiveSyncSetupReply? = null
        state.run(scope, context, "", { reply = client.liveSyncSetup(u, v, p) }) {
            val r = reply
            state.message = when {
                r == null -> context.getString(R.string.setup_livesync_saved_plain)
                r.folderExists -> context.getString(R.string.setup_livesync_saved_exists, r.profile, r.vault)
                r.vault.isEmpty() -> context.getString(R.string.setup_livesync_saved_noname, r.profile)
                else -> context.getString(R.string.setup_livesync_saved, r.profile, r.vault)
            }
            vault = ""
            profile = null
        }
    }) { Text(stringResource(R.string.setup_livesync_send)) }
    Message(state.message)
    Hint(R.string.setup_livesync_passphrase)
    Hint(R.string.setup_plain_http)
}

// ---- Fonts: GET /api/fonts, POST /api/fonts/upload (multipart: family, file), POST /api/fonts/delete ----

@Composable
fun FontsSetupScreen(client: DeviceClient, onBack: () -> Unit) = SubPage(stringResource(R.string.setup_fonts), onBack) {
    val context = LocalContext.current
    val (state, scope) = rememberRemote(client) { fonts().families }
    var deleting by remember { mutableStateOf<FontFamilyInfo?>(null) }
    var picked by rememberSaveable { mutableStateOf<android.net.Uri?>(null) }
    var fileName by rememberSaveable { mutableStateOf("") }
    var fileSize by rememberSaveable { mutableStateOf(-1L) }
    var family by rememberSaveable { mutableStateOf("") }
    var progress by remember { mutableStateOf<Float?>(null) }
    val main = remember { android.os.Handler(android.os.Looper.getMainLooper()) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        val (name, size) = displayName(context, uri)
        val clean = ReaderApi.fontFileName(name)
        if (clean == null) {
            picked = null
            state.message = context.getString(R.string.setup_fonts_not_font, name)
        } else {
            picked = uri; fileName = clean; fileSize = size; family = ReaderApi.fontFamilyFrom(name)
            state.message = ""
        }
    }
    Hint(R.string.setup_fonts_hint)
    ListGroup(state, R.string.setup_fonts_none) { f ->
        val sizes = if (f.scalable) stringResource(R.string.setup_fonts_scalable)
            else stringResource(R.string.setup_fonts_sizes, f.sizes.joinToString(", "))
        GroupRow(f.name, sizes + " · " + sizeText(f.bytes)) {
            TextButton(enabled = !state.busy, onClick = { deleting = f }) { Text(stringResource(R.string.delete)) }
        }
    }
    SectionLabel(stringResource(R.string.setup_fonts_add))
    val uri = picked
    if (uri == null) {
        // Font files have no one MIME type across file managers, so the picker shows everything
        // and the name is checked afterwards.
        Button(enabled = !state.busy, onClick = { pick.launch(arrayOf("*/*")) }) { Text(stringResource(R.string.setup_fonts_pick)) }
    } else {
        Text(if (fileSize > 0) "$fileName · ${sizeText(fileSize)}" else fileName, style = MaterialTheme.typography.bodyMedium)
        val familyOk = ReaderApi.isFontFamily(family)
        Field(family, { family = it }, R.string.setup_fonts_family, error = !familyOk,
            supporting = stringResource(R.string.setup_fonts_family_note))
        if (state.busy) progress?.let { LinearProgressIndicator(progress = { it }, modifier = Modifier.fillMaxWidth()) }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = !state.busy && familyOk, onClick = {
                val (fam, name, size) = Triple(family, fileName, fileSize)
                progress = 0f
                state.run(scope, context, context.getString(R.string.setup_fonts_sent, name), {
                    var length = size
                    var open: () -> java.io.InputStream = { context.contentResolver.openInputStream(uri)!! }
                    if (length <= 0) {
                        // The picker didn't say how big it is: read it first, so the reader gets a body with a length.
                        val bytes = open().use { readCapped(it, FONT_MAX_BYTES) }
                            ?: throw java.io.IOException(context.getString(R.string.setup_fonts_too_big))
                        length = bytes.size.toLong()
                        open = { bytes.inputStream() }
                    }
                    client.uploadFont(fam, name, length, open) { done, total ->
                        if (total > 0) main.post { progress = done.toFloat() / total }
                    }
                }) { picked = null }
            }) { Text(stringResource(R.string.setup_fonts_send)) }
            TextButton(enabled = !state.busy, onClick = { picked = null }) { Text(stringResource(R.string.cancel)) }
        }
    }
    Message(state.message)
    Hint(R.string.setup_fonts_refused_note)
    deleting?.let { f ->
        ConfirmDelete(f.name, { deleting = null }) {
            deleting = null
            state.run(scope, context, context.getString(R.string.deleted), { client.deleteFont(f.name) })
        }
    }
}
