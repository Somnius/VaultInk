package com.lefteros.vaultink

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.Surface
import androidx.compose.material3.darkColorScheme
import androidx.compose.foundation.clickable
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/** Something another app shared: text, and pictures copied into the cache right away. */
data class Draft(val title: String = "", val text: String = "", val pictures: List<File> = emptyList())

class MainActivity : ComponentActivity() {
    private var draft by mutableStateOf(Draft())

    override fun onCreate(savedInstanceState: Bundle?) {
        // Dark status bar icons on the light theme and light ones on the dark, so the clock stays visible.
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        draft = readShare(intent) ?: Draft()
        setContent {
            MaterialTheme(colorScheme = if (isSystemInDarkTheme()) InkDark else InkLight) {
                Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    App(draft) { draft = it }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        readShare(intent)?.let { draft = it }
    }

    private fun readShare(intent: Intent?): Draft? {
        if (intent == null) return null
        val uris = mutableListOf<Uri>()
        when (intent.action) {
            Intent.ACTION_SEND ->
                @Suppress("DEPRECATION") (intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM))?.let { uris.add(it) }
            Intent.ACTION_SEND_MULTIPLE ->
                @Suppress("DEPRECATION") intent.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)?.let { uris.addAll(it) }
            else -> return null
        }
        val text = intent.getStringExtra(Intent.EXTRA_TEXT).orEmpty()
        val subject = intent.getStringExtra(Intent.EXTRA_SUBJECT).orEmpty()
        val title = subject.ifBlank { text.lineSequence().firstOrNull { it.isNotBlank() }.orEmpty().take(60) }
        return Draft(DeviceClient.safeName(title), text, uris.mapNotNull { copyToCache(it) })
    }

    // The sharing app's permission ends with this intent: copy the picture now.
    private fun copyToCache(uri: Uri): File? = runCatching {
        var name = "picture.jpg"
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) name = c.getString(0) ?: name
        }
        val dir = File(cacheDir, "shared").apply { mkdirs() }
        val file = File(dir, DeviceClient.safeName(name).ifEmpty { "picture.jpg" })
        contentResolver.openInputStream(uri)!!.use { input -> file.outputStream().use { input.copyTo(it) } }
        file
    }.getOrNull()
}

// Ink blue on paper, like the device's own look; the dark scheme follows the phone's dark mode.
private val InkLight = lightColorScheme(
    primary = Color(0xFF1F3A5F), onPrimary = Color.White,
    primaryContainer = Color(0xFFD6E2F0), onPrimaryContainer = Color(0xFF0B1E33),
    secondaryContainer = Color(0xFFE4E9EF),
    background = Color(0xFFF3F1EC), surface = Color(0xFFFBFAF7),
)
private val InkDark = darkColorScheme(
    primary = Color(0xFFA9C7EA), onPrimary = Color(0xFF0B1E33),
    primaryContainer = Color(0xFF1F3A5F), onPrimaryContainer = Color(0xFFD6E2F0),
    background = Color(0xFF121416), surface = Color(0xFF1A1D20),
)

private fun prefs(context: Context) = context.getSharedPreferences("vaultink", Context.MODE_PRIVATE)

@Composable
fun App(draft: Draft, onDraft: (Draft) -> Unit) {
    val context = LocalContext.current
    var host by remember { mutableStateOf(prefs(context).getString("host", "") ?: "") }
    var status by remember { mutableStateOf<DeviceStatus?>(null) }
    var searching by remember { mutableStateOf(false) }
    var tab by remember { mutableIntStateOf(0) }
    var intro by remember { mutableStateOf(!prefs(context).getBoolean("intro_seen", false)) }
    val scope = rememberCoroutineScope()

    // Tries the remembered address, then mDNS; remembers what answered.
    var search by remember { mutableStateOf<Job?>(null) }
    fun connect(manual: String? = null) {
        search?.cancel()  // a slow older search must not overwrite a newer result
        search = scope.launch {
            searching = true
            status = null
            val candidates = listOfNotNull(manual?.trim()?.ifEmpty { null }, host.ifEmpty { null }, "vaultink.local")
            var found: Pair<String, DeviceStatus>? = null
            for (h in candidates.distinct()) {
                val s = withContext(Dispatchers.IO) { runCatching { DeviceClient(h).status() }.getOrNull() }
                if (s != null) { found = h to s; break }
            }
            if (found == null) {
                val discovered = discoverDevice(context)
                if (discovered != null) {
                    val s = withContext(Dispatchers.IO) { runCatching { DeviceClient(discovered).status() }.getOrNull() }
                    if (s != null) found = discovered to s
                }
            }
            found?.let { (h, s) ->
                host = h
                status = s
                prefs(context).edit().putString("host", h).apply()
            }
            searching = false
        }
    }
    LaunchedEffect(Unit) { connect() }

    if (intro) {
        Box(Modifier.fillMaxSize().safeDrawingPadding()) {
            IntroScreen(onDone = {
                prefs(context).edit().putBoolean("intro_seen", true).apply()
                intro = false
            })
        }
        return
    }

    // The header colour runs up behind the status bar; only the content keeps clear of it.
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.primaryContainer).statusBarsPadding()
        .background(MaterialTheme.colorScheme.background).navigationBarsPadding().imePadding()) {
        Text(stringResource(R.string.app_name), style = MaterialTheme.typography.titleLarge,
            color = MaterialTheme.colorScheme.onPrimaryContainer,
            modifier = Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.primaryContainer)
                .padding(horizontal = 16.dp, vertical = 12.dp))
        TabRow(selectedTabIndex = tab, containerColor = MaterialTheme.colorScheme.primaryContainer,
            contentColor = MaterialTheme.colorScheme.onPrimaryContainer) {
            listOf(R.string.tab_note, R.string.tab_files, R.string.tab_settings, R.string.tab_device).forEachIndexed { i, label ->
                // One line, shrunk to fit: Greek labels at a large phone font otherwise broke mid-word.
                Tab(selected = tab == i, onClick = { tab = i }, text = {
                    val color = LocalContentColor.current
                    BasicText(stringResource(label), maxLines = 1, color = { color },
                        style = MaterialTheme.typography.titleSmall,
                        autoSize = TextAutoSize.StepBased(minFontSize = 9.sp, maxFontSize = 14.sp))
                })
            }
        }
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)) {
            val connected = status
            when {
                searching -> Searching()
                connected == null -> DeviceSearch(host, onConnect = { connect(it) }, onSearch = { connect() })
                else -> {
                    Hint(listOf(R.string.hint_note, R.string.hint_files, R.string.settings_hint, R.string.hint_device)[tab])
                    when (tab) {
                        0 -> NoteScreen(DeviceClient(host), draft, onDraft)
                        1 -> FilesScreen(remember(host) { DeviceClient(host) })
                        2 -> SettingsScreen(remember(host) { DeviceClient(host) })
                        else -> DeviceScreen(host, connected, onSearch = { connect() }, onIntro = { intro = true })
                    }
                }
            }
        }
    }
}

@Composable
fun Hint(text: Int) {
    Text(stringResource(text), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
}

@Composable
fun Searching() {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 3.dp)
        Text(stringResource(R.string.searching))
    }
    Hint(R.string.connect_checklist)
}

@Composable
fun DeviceSearch(host: String, onConnect: (String) -> Unit, onSearch: () -> Unit) {
    var address by remember { mutableStateOf(host) }
    Text(stringResource(R.string.not_found_title), style = MaterialTheme.typography.titleLarge)
    Text(stringResource(R.string.not_found_step1))
    Text(stringResource(R.string.not_found_step2))
    Button(onClick = onSearch) { Text(stringResource(R.string.search_again)) }
    Text(stringResource(R.string.not_found_step3), style = MaterialTheme.typography.bodySmall)
    OutlinedTextField(address, { address = it }, label = { Text(stringResource(R.string.address)) },
        placeholder = { Text(stringResource(R.string.address_hint)) }, singleLine = true, modifier = Modifier.fillMaxWidth())
    OutlinedButton(onClick = { onConnect(address) }) { Text(stringResource(R.string.connect)) }
}

@Composable
fun DeviceScreen(host: String, status: DeviceStatus, onSearch: () -> Unit, onIntro: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var update by remember { mutableStateOf<AppUpdate?>(null) }
    var message by remember { mutableStateOf("") }
    Text(stringResource(R.string.found, host, status.version))
    if (status.hotspot) Text(stringResource(R.string.hotspot_warning), color = MaterialTheme.colorScheme.error)
    OutlinedButton(onClick = onSearch) { Text(stringResource(R.string.search_again)) }
    OutlinedButton(onClick = {
        scope.launch {
            message = withContext(Dispatchers.IO) {
                runCatching { Updater.newer(BuildConfig.VERSION_NAME) }.fold(
                    onSuccess = { found ->
                        update = found
                        if (found == null) context.getString(R.string.update_none, BuildConfig.VERSION_NAME) else ""
                    },
                    onFailure = { context.getString(R.string.update_failed, it.message ?: "") },
                )
            }
        }
    }) { Text(stringResource(R.string.check_update)) }
    if (message.isNotEmpty()) Text(message)
    update?.let { found ->
        AlertDialog(
            onDismissRequest = { update = null },
            text = { Text(stringResource(R.string.update_found, found.version)) },
            confirmButton = {
                TextButton(onClick = {
                    update = null
                    scope.launch(Dispatchers.IO) {
                        runCatching { Updater.install(context, found) }.onFailure {
                            message = context.getString(R.string.update_failed, it.message ?: "")
                        }
                    }
                }) { Text(stringResource(R.string.update_install)) }
            },
        )
    }
    TextButton(onClick = onIntro) { Text(stringResource(R.string.intro_show_again)) }
    Text(stringResource(R.string.credit), style = MaterialTheme.typography.bodySmall)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NoteScreen(client: DeviceClient, draft: Draft, onDraft: (Draft) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var vaults by remember { mutableStateOf<List<String>?>(null) }
    var vault by remember { mutableStateOf(prefs(context).getString("vault", "") ?: "") }
    var folder by remember { mutableStateOf(prefs(context).getString("folder", "Inbox") ?: "Inbox") }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf("") }
    LaunchedEffect(client) {
        vaults = withContext(Dispatchers.IO) { runCatching { client.vaults() }.getOrDefault(emptyList()) }
        if (vault !in vaults.orEmpty()) vault = vaults.orEmpty().firstOrNull().orEmpty()
    }
    val list = vaults ?: return
    if (list.isEmpty()) {
        Text(stringResource(R.string.no_vaults))
        return
    }
    var open by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }) {
        OutlinedTextField(vault, {}, readOnly = true, label = { Text(stringResource(R.string.vault)) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(open) },
            modifier = Modifier.fillMaxWidth().menuAnchor(MenuAnchorType.PrimaryNotEditable))
        ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            list.forEach { v -> DropdownMenuItem(text = { Text(v) }, onClick = { vault = v; open = false }) }
        }
    }
    OutlinedTextField(folder, { folder = it }, label = { Text(stringResource(R.string.folder)) }, singleLine = true,
        modifier = Modifier.fillMaxWidth())
    OutlinedTextField(draft.title, { onDraft(draft.copy(title = it)) }, label = { Text(stringResource(R.string.title)) },
        singleLine = true, modifier = Modifier.fillMaxWidth())
    OutlinedTextField(draft.text, { onDraft(draft.copy(text = it)) }, label = { Text(stringResource(R.string.text)) },
        modifier = Modifier.fillMaxWidth().heightIn(min = 200.dp))
    if (draft.pictures.isNotEmpty()) Text(pluralStringResource(R.plurals.pictures, draft.pictures.size, draft.pictures.size))
    Button(enabled = !busy, onClick = {
        val title = DeviceClient.safeName(draft.title)
        if (title.isEmpty()) { message = context.getString(R.string.need_title); return@Button }
        busy = true
        message = context.getString(R.string.sending)
        scope.launch {
            message = withContext(Dispatchers.IO) {
                runCatching {
                    val vaultPath = "/Vaults/$vault"
                    val notePath = "$vaultPath/" + folder.trim('/').ifEmpty { "" }
                    client.ensureFolder(notePath)
                    // Pictures first, so the note links the names they were saved under.
                    val links = StringBuilder()
                    if (draft.pictures.isNotEmpty()) {
                        client.ensureFolder("$vaultPath/attachments")
                        for (p in draft.pictures) {
                            val saved = client.upload("$vaultPath/attachments", p.name, p, "application/octet-stream")
                            links.append("\n![[").append(saved).append("]]")
                        }
                    }
                    val body = "# $title\n\n${draft.text.trimEnd()}\n" + (if (links.isEmpty()) "" else "$links\n")
                    val saved = client.upload(notePath, "$title.md", body.toByteArray(), "text/markdown")
                    prefs(context).edit().putString("vault", vault).putString("folder", folder).apply()
                    draft.pictures.forEach { it.delete() }
                    onDraft(Draft())
                    context.getString(R.string.sent, saved)
                }.getOrElse { context.getString(R.string.send_failed, it.message ?: "") }
            }
            busy = false
        }
    }) { Text(stringResource(R.string.send)) }
    if (message.isNotEmpty()) Text(message)
}

@Composable
fun SettingsScreen(client: DeviceClient) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var settings by remember { mutableStateOf<List<Setting>?>(null) }
    var error by remember { mutableStateOf("") }
    var openGroup by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(client) {
        withContext(Dispatchers.IO) { runCatching { client.settings() } }
            .onSuccess { settings = it; openGroup = it.firstOrNull()?.category }
            .onFailure { error = context.getString(R.string.settings_failed, it.message ?: "") }
    }
    if (error.isNotEmpty()) { Text(error); return }
    val list = settings ?: run { Text(stringResource(R.string.loading)); return }
    // One card per device category, in the device's order; one open at a time keeps the page short.
    for ((category, rows) in list.groupBy { it.category }) {
        val open = openGroup == category
        OutlinedCard(Modifier.fillMaxWidth()) {
            Row(Modifier.fillMaxWidth().clickable { openGroup = if (open) null else category }.padding(16.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(category, style = MaterialTheme.typography.titleMedium)
                    Text(pluralStringResource(R.plurals.settings_count, rows.size, rows.size),
                        style = MaterialTheme.typography.bodySmall)
                }
                Text(if (open) "▲" else "▼")
            }
            if (open) rows.forEach { s ->
                HorizontalDivider()
                Box(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
                    SettingRow(s) { value ->
                        scope.launch {
                            withContext(Dispatchers.IO) { runCatching { client.setSetting(s.key, value) } }
                                .onSuccess { settings = settings?.map { if (it.key == s.key) it.copy(value = value) else it } }
                                .onFailure { error = context.getString(R.string.action_failed, it.message ?: "") }
                        }
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingRow(s: Setting, onChange: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(s.name, Modifier.weight(1f))
        when (s.type) {
            "toggle" -> Switch(checked = s.value != 0, onCheckedChange = { onChange(if (it) 1 else 0) })
            "enum" -> {
                var open by remember { mutableStateOf(false) }
                ExposedDropdownMenuBox(expanded = open, onExpandedChange = { open = it }, modifier = Modifier.weight(1f)) {
                    OutlinedTextField(s.options.getOrElse(s.value) { "" }, {}, readOnly = true, singleLine = true,
                        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(open) },
                        modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable))
                    ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                        s.options.forEachIndexed { i, o ->
                            DropdownMenuItem(text = { Text(o) }, onClick = { open = false; onChange(i) })
                        }
                    }
                }
            }
            "value" -> Row(verticalAlignment = Alignment.CenterVertically) {
                val step = s.step.coerceAtLeast(1)
                OutlinedButton(enabled = s.value > s.min, onClick = { onChange((s.value - step).coerceAtLeast(s.min)) }) { Text("−") }
                Text(s.value.toString(), Modifier.padding(horizontal = 12.dp))
                OutlinedButton(enabled = s.value < s.max, onClick = { onChange((s.value + step).coerceAtMost(s.max)) }) { Text("+") }
            }
        }
    }
}
