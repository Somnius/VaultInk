package com.lefteros.vaultink

import android.os.SystemClock
import androidx.compose.animation.Crossfade
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** The last /api/status answer as a list, so it survives the activity being made again. */
private val StatusSaver = listSaver<DeviceStatus?, Any>(
    save = { s -> if (s == null) emptyList() else listOf(s.version, s.ip, s.hotspot, s.device, s.rssi, s.uptime, s.freeHeap) },
    restore = { l ->
        if (l.size != 7) null
        else DeviceStatus(l[0] as String, l[1] as String, l[2] as Boolean, l[3] as String, l[4] as Int, l[5] as Long, l[6] as Long)
    },
)

private const val TAB_HOME = 0
private const val TAB_NOTE = 1
private const val TAB_FILES = 2
private const val TAB_SETTINGS = 3

@Composable
fun App(draft: Draft, onDraft: (Draft) -> Unit) {
    val context = LocalContext.current
    var host by remember { mutableStateOf(prefs(context).getString("host", "") ?: "") }
    // Kept over a rotation or a light/dark switch, so the open screen doesn't fall back to a new search.
    var status by rememberSaveable(stateSaver = StatusSaver) { mutableStateOf<DeviceStatus?>(null) }
    var searching by remember { mutableStateOf(false) }
    var tab by rememberSaveable { mutableIntStateOf(TAB_HOME) }
    var intro by remember { mutableStateOf(!prefs(context).getBoolean("intro_seen", false)) }
    // What draws over the tabs: "help", "about", or one of SETUP_PAGES.
    var page by rememberSaveable { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    // The outbox (notes and shares waiting for the reader) and the last line about it.
    var waiting by remember { mutableIntStateOf(Outbox.count(context)) }
    var outboxNote by remember { mutableStateOf("") }
    // A status that came back with the screen counts as just asked: the next try is 10 s away.
    val policy = remember { PollPolicy().apply { if (status != null) record(SystemClock.elapsedRealtime(), true) } }
    // Uploads running right now (the outbox, shared books). The reader's web server does one thing
    // at a time, so /api/status isn't asked meanwhile, and a poll cut short by one is not a miss.
    var transfers by remember { mutableIntStateOf(0) }
    // One client per address; null while there is none to talk to.
    val client = remember(host, status != null) {
        if (status == null || host.isEmpty()) null else runCatching { DeviceClient(host) }.getOrNull()
    }

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
            // What answered may have been a name (vaultink.local) that Android won't resolve the next
            // time: from here on the reader's own IP is used, once it has answered there too.
            found?.let { (h, s) ->
                val numeric = numericHost(h, s.ip)
                if (numeric != null && numeric != h) {
                    val again = withContext(Dispatchers.IO) { runCatching { DeviceClient(numeric).status() }.getOrNull() }
                    if (again != null) found = numeric to again
                }
            }
            policy.retry()
            found?.let { (h, s) ->
                host = h
                status = s
                policy.record(SystemClock.elapsedRealtime(), true)
                prefs(context).edit().putString("host", h).apply()
            }
            searching = false
            // Reader found: send what waited for it.
            if (found != null && Outbox.count(context) > 0) {
                transfers++
                val sent = try {
                    withContext(Dispatchers.IO) { Outbox.send(context, DeviceClient(found!!.first)) }
                } finally {
                    transfers--
                }
                waiting = Outbox.count(context)
                if (sent > 0) outboxNote = context.resources.getQuantityString(R.plurals.outbox_sent, sent, sent)
            }
        }
    }
    LaunchedEffect(Unit) { if (status == null) connect() }

    // Something shared from another app opens in the Note tab.
    LaunchedEffect(draft.text, draft.pictures) {
        if (draft.text.isNotEmpty() || draft.pictures.isNotEmpty()) tab = TAB_NOTE
    }

    // Keeps the Home figures fresh: /api/status while the app is in the foreground and Home is the
    // screen shown, no sooner than every 10 s (PollPolicy). The reader's web server does one thing
    // at a time, so the other tabs (a transfer in Files) are left alone. When the reader stops
    // answering, polling stops and Home says how to open the connection again; Try again starts over.
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val polling = client != null && tab == TAB_HOME && !intro && page == null && transfers == 0
    LaunchedEffect(client, polling) {
        val c = client
        if (!polling || c == null) return@LaunchedEffect
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (true) {
                val wait = policy.delayFrom(SystemClock.elapsedRealtime()) ?: break
                delay(wait)
                val s = withContext(Dispatchers.IO) { runCatching { c.status() }.getOrNull() }
                policy.record(SystemClock.elapsedRealtime(), s != null)
                if (s != null) status = s else if (policy.stopped) { status = null; break }
            }
        }
    }

    // The intro, Help, About and the setup screens fade in over the tabs; the connection and the tab stay as they are.
    val screen = if (intro) "intro" else page ?: "tabs"
    Crossfade(screen, Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background), animationSpec = fade(),
        label = "screen") { shown ->
        when (shown) {
            "intro" -> Box(Modifier.fillMaxSize().safeDrawingPadding()) {
                IntroScreen(onDone = {
                    prefs(context).edit().putBoolean("intro_seen", true).apply()
                    intro = false
                })
            }
            "help" -> Box(Modifier.fillMaxSize().safeDrawingPadding()) { HelpScreen(onBack = { page = null }) }
            "about" -> Box(Modifier.fillMaxSize().safeDrawingPadding()) { AboutScreen(onBack = { page = null }) }
            "tabs" -> Tabs(tab, { tab = it }, onIntro = { intro = true }, onPage = { page = it }) { current ->
                val connected = status
                // Books and comics shared with no reader around go to the outbox at once.
                if (!searching && connected == null && draft.books.isNotEmpty()) {
                    LaunchedEffect(draft.books) {
                        val n = withContext(Dispatchers.IO) {
                            draft.books.count { book ->
                                shareFolderFor(book.name)?.let { Outbox.addFile(context, it, book); true } ?: false
                            }
                        }
                        onDraft(draft.copy(books = emptyList()))
                        waiting = Outbox.count(context)
                        outboxNote = context.resources.getQuantityString(R.plurals.outbox_files_kept, n, n)
                    }
                }
                if (client != null && (draft.books.isNotEmpty() || draft.refused.isNotEmpty() || draft.unreadable.isNotEmpty())) {
                    ShareBooksDialog(client, draft.books, draft.refused, draft.unreadable,
                        onBusy = { if (it) transfers++ else transfers-- },
                        onKept = { waiting = Outbox.count(context) }) {
                        draft.books.forEach { it.delete() }
                        onDraft(draft.copy(books = emptyList(), refused = emptyList(), unreadable = emptyList()))
                    }
                }
                if (outboxNote.isNotEmpty()) Text(outboxNote, style = MaterialTheme.typography.bodyMedium)
                Crossfade(current, animationSpec = fade(), label = "tab") { t ->
                    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        when {
                            t == TAB_HOME -> HomeScreen(host, client, connected, searching, waiting,
                                onRetry = { connect() }, onConnect = { connect(it) })
                            searching -> Searching()
                            t == TAB_NOTE -> {
                                OutboxLine(waiting)
                                if (client != null) {
                                    Hint(R.string.hint_note)
                                    NoteScreen(client, draft, onDraft) { waiting = Outbox.count(context) }
                                } else if (!prefs(context).getString("vault", "").isNullOrEmpty()) {
                                    // A note can still be written: it waits in the outbox (needs a vault used before).
                                    Hint(R.string.offline_note_hint)
                                    NoteScreen(null, draft, onDraft) { waiting = Outbox.count(context) }
                                } else {
                                    OfflineNotice { connect() }
                                }
                                PhoneTextSection(client) { waiting = Outbox.count(context) }
                            }
                            client == null -> OfflineNotice { connect() }
                            t == TAB_FILES -> {
                                Hint(R.string.hint_files)
                                FilesScreen(client)
                            }
                            else -> SettingsScreen(client, connected, onOpen = { page = it })
                        }
                    }
                }
            }
            else -> Box(Modifier.fillMaxSize().safeDrawingPadding().imePadding()) {
                // A setup screen needs the reader; if it went away, back to the tabs.
                when {
                    client != null -> SetupScreen(shown, client, onBack = { page = null })
                    searching -> Column(Modifier.padding(16.dp)) { Searching() }
                    else -> LaunchedEffect(Unit) { page = null }
                }
            }
        }
    }
}

/** The paper header with the wordmark and the ⋮ menu, the four tabs, and the scrolling content of the one shown. */
@Composable
private fun Tabs(tab: Int, onTab: (Int) -> Unit, onIntro: () -> Unit, onPage: (String) -> Unit,
                 content: @Composable (Int) -> Unit) {
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding()) {
        Row(Modifier.fillMaxWidth().padding(start = 20.dp, end = 4.dp, top = 6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium,
                color = MaterialTheme.colorScheme.primary, modifier = Modifier.weight(1f))
            var menu by remember { mutableStateOf(false) }
            Box {
                TextButton(onClick = { menu = true }) {
                    Text("⋮", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text(stringResource(R.string.menu_intro)) }, onClick = { menu = false; onIntro() })
                    DropdownMenuItem(text = { Text(stringResource(R.string.menu_help)) }, onClick = { menu = false; onPage("help") })
                    DropdownMenuItem(text = { Text(stringResource(R.string.menu_about)) }, onClick = { menu = false; onPage("about") })
                }
            }
        }
        TabRow(selectedTabIndex = tab, containerColor = MaterialTheme.colorScheme.background,
            contentColor = MaterialTheme.colorScheme.primary,
            divider = { HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant) }) {
            listOf(R.string.tab_home, R.string.tab_note, R.string.tab_files, R.string.tab_settings).forEachIndexed { i, label ->
                // One line, shrunk to fit: Greek labels at a large phone font otherwise broke mid-word.
                Tab(selected = tab == i, onClick = { onTab(i) },
                    unselectedContentColor = MaterialTheme.colorScheme.onSurfaceVariant, text = {
                    val color = LocalContentColor.current
                    BasicText(stringResource(label), maxLines = 1, color = { color },
                        style = MaterialTheme.typography.titleSmall,
                        autoSize = TextAutoSize.StepBased(minFontSize = 9.sp, maxFontSize = 14.sp))
                })
            }
        }
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)) {
            content(tab)
        }
    }
}

/**
 * The Settings tab: Reader setup first (the screens of SetupScreens.kt), then the reader's own
 * settings as grouped lists, one group per category in the reader's order.
 */
@Composable
fun SettingsScreen(client: DeviceClient, status: DeviceStatus?, onOpen: (String) -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var settings by remember { mutableStateOf<List<Setting>?>(null) }
    var error by remember { mutableStateOf("") }
    var reload by remember { mutableIntStateOf(0) }  // a restore changed things: read the list again
    LaunchedEffect(client, reload) {
        withContext(Dispatchers.IO) { runCatching { client.settings() } }
            .onSuccess { settings = it }
            .onFailure { error = context.getString(R.string.settings_failed, it.message ?: "") }
    }
    SectionLabel(stringResource(R.string.setup_section))
    Group {
        SETUP_PAGES.forEachIndexed { i, (key, title) ->
            if (i > 0) Hairline()
            GroupRow(stringResource(title), onClick = { onOpen(key) }) { Chevron() }
        }
    }
    BackupSection(client, status, onRestored = { reload++ })
    Hint(R.string.settings_hint)
    if (error.isNotEmpty()) Text(error, style = MaterialTheme.typography.bodyMedium)
    val list = settings ?: run {
        if (error.isEmpty()) Text(stringResource(R.string.loading))
        return
    }
    for ((category, rows) in list.groupBy { it.category }) {
        SectionLabel(category)
        Group {
            rows.forEachIndexed { i, s ->
                if (i > 0) Hairline()
                SettingRow(s) { value ->
                    scope.launch {
                        withContext(Dispatchers.IO) { runCatching { client.setSetting(s.key, value) } }
                            .onSuccess { error = ""; settings = settings?.map { if (it.key == s.key) it.copy(value = value) else it } }
                            .onFailure { error = context.getString(R.string.action_failed, it.message ?: "") }
                    }
                }
            }
        }
    }
}

/** One setting: its name, and a switch, the chosen option (tap for the list) or a number with − and +. */
@Composable
fun SettingRow(s: Setting, onChange: (Int) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).padding(start = 16.dp, end = 8.dp, top = 4.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(s.name, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
        when (s.type) {
            "toggle" -> Switch(checked = s.value != 0, onCheckedChange = { onChange(if (it) 1 else 0) },
                modifier = Modifier.padding(end = 8.dp))
            "enum" -> {
                var open by remember { mutableStateOf(false) }
                Box(Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
                    Text(s.options.getOrElse(s.value) { "" } + "  ▾", Modifier.clickable { open = true }.padding(8.dp),
                        style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.primary,
                        textAlign = androidx.compose.ui.text.style.TextAlign.End)
                    DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                        s.options.forEachIndexed { i, o ->
                            DropdownMenuItem(text = { Text(o) }, onClick = { open = false; onChange(i) })
                        }
                    }
                }
            }
            "value" -> Row(verticalAlignment = Alignment.CenterVertically) {
                val step = s.step.coerceAtLeast(1)
                TextButton(enabled = s.value > s.min, onClick = { onChange((s.value - step).coerceAtLeast(s.min)) }) { Text("−") }
                Text(s.value.toString(), style = MaterialTheme.typography.bodyMedium.copy(fontFeatureSettings = TABULAR))
                TextButton(enabled = s.value < s.max, onClick = { onChange((s.value + step).coerceAtMost(s.max)) }) { Text("+") }
            }
        }
    }
}
