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
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Surface
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Something another app shared: text, and pictures copied into the cache right away. Books and
 * comics go to the reader as files instead (`books`); other files are named in `refused`, and files
 * the sharing app didn't let us read in `unreadable`.
 */
data class Draft(
    val title: String = "",
    val text: String = "",
    val pictures: List<File> = emptyList(),
    val books: List<File> = emptyList(),
    val refused: List<String> = emptyList(),
    val unreadable: List<String> = emptyList(),
)

private val PICTURE_EXTENSIONS = setOf("jpg", "jpeg", "png", "gif", "bmp", "webp")

/** Where a shared file goes on the card, by its extension; null when the reader can't open it. */
fun shareFolderFor(name: String): String? = when (name.substringAfterLast('.', "").lowercase()) {
    "xtc", "xtch", "cbz" -> "/Comics"
    "epub", "txt" -> "/Books"
    else -> null
}

class MainActivity : ComponentActivity() {
    private var draft by mutableStateOf(Draft())

    override fun onCreate(savedInstanceState: Bundle?) {
        // Dark status bar icons on the light theme and light ones on the dark, so the clock stays visible.
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        draft = readShare(intent) ?: Draft()
        setContent {
            VaultInkTheme {
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
        val unreadable = mutableListOf<String>()
        val files = uris.mapNotNull { uri -> copyToCache(uri) ?: null.also { unreadable.add(displayName(uri)) } }
        val (books, others) = files.partition { shareFolderFor(it.name) != null }
        val (pictures, refused) = others.partition { it.extension.lowercase() in PICTURE_EXTENSIONS || it.extension.isEmpty() }
        refused.forEach { it.delete() }
        return Draft(DeviceClient.safeName(title), text, pictures, books, refused.map { it.name }, unreadable)
    }

    private fun displayName(uri: Uri): String = runCatching {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/') ?: uri.toString()

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

internal fun prefs(context: Context) = context.getSharedPreferences("vaultink", Context.MODE_PRIVATE)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun NoteScreen(client: DeviceClient?, draft: Draft, onDraft: (Draft) -> Unit, onOutbox: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var vaults by remember { mutableStateOf<List<String>?>(null) }
    var vault by remember { mutableStateOf(prefs(context).getString("vault", "") ?: "") }
    var folder by remember { mutableStateOf(prefs(context).getString("folder", "Inbox") ?: "Inbox") }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf("") }
    // A listing that failed is an error with a way to ask again, never "no vaults".
    var vaultsError by remember { mutableStateOf("") }
    var asked by remember { mutableIntStateOf(0) }
    LaunchedEffect(client, asked) {
        vaultsError = ""
        // Offline the only vault offered is the last one used (its name is all the outbox needs).
        val found = if (client == null) Result.success(listOf(vault).filter { it.isNotEmpty() })
        else withContext(Dispatchers.IO) { runCatching { client.vaults() } }
        found.onSuccess {
            vaults = it
            if (vault !in it) vault = it.firstOrNull().orEmpty()
        }.onFailure {
            vaults = null
            vaultsError = context.getString(R.string.vaults_failed, it.message ?: "")
        }
    }
    if (vaultsError.isNotEmpty()) {
        Text(vaultsError)
        OutlinedButton(onClick = { asked++ }) { Text(stringResource(R.string.try_again)) }
        return
    }
    val list = vaults ?: run {
        Text(stringResource(R.string.loading))
        return
    }
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
            // Keeps the note (and its pictures) on the phone until the reader is found.
            fun keep(): String {
                Outbox.addNote(context, vault, ReaderApi.noteFolder(folder), title, draft.text, draft.pictures)
                prefs(context).edit().putString("vault", vault).putString("folder", folder).apply()
                onDraft(Draft())
                onOutbox()
                return context.getString(R.string.outbox_kept)
            }
            message = withContext(Dispatchers.IO) {
                if (client == null) return@withContext keep()
                runCatching {
                    val vaultPath = "/Vaults/$vault"
                    if (!ReaderApi.isVaultName(vault)) throw IllegalArgumentException(vault)
                    val notePath = "$vaultPath/" + ReaderApi.noteFolder(folder)
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
                    val body = Outbox.noteBody(title, draft.text, links.toString())
                    val saved = client.upload(notePath, "$title.md", body.toByteArray(), "text/markdown")
                    prefs(context).edit().putString("vault", vault).putString("folder", folder).apply()
                    draft.pictures.forEach { it.delete() }
                    onDraft(Draft())
                    context.getString(R.string.sent, saved)
                }.getOrElse { if (it is java.io.IOException) keep() else context.getString(R.string.send_failed, it.message ?: "") }
            }
            busy = false
        }
    }) { Text(stringResource(R.string.send)) }
    if (message.isNotEmpty()) Text(message)
    // Bring notes in (0.5.0): a Google Keep Takeout into Imported/Keep of this vault.
    var importing by remember { mutableStateOf(false) }
    OutlinedButton(onClick = { importing = true }) { Text(stringResource(R.string.import_title)) }
    if (importing) KeepImportDialog(client, vault, onOutbox = onOutbox) { importing = false }
}

/**
 * Type into the reader (firmware 0.64.0+): the text goes to `/.vaultink/phone-text.txt` on
 * the card and the reader's next text-entry screen (a note line, a search, a habit name) shows a
 * "From phone" button that inserts it. Without a reader it waits in the outbox like a note.
 */
@Composable
fun PhoneTextSection(client: DeviceClient?, onOutbox: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var text by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf("") }
    SectionLabel(stringResource(R.string.phone_text_title))
    Text(stringResource(R.string.phone_text_hint), style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant)
    OutlinedTextField(text, { text = PhoneText.clip(it) }, label = { Text(stringResource(R.string.phone_text_label)) },
        supportingText = { Text(stringResource(R.string.phone_text_count, PhoneText.bytes(text), PhoneText.MAX_BYTES)) },
        modifier = Modifier.fillMaxWidth().heightIn(min = 120.dp))
    OutlinedButton(enabled = !busy && text.isNotBlank(), onClick = {
        busy = true
        scope.launch {
            val sending = text
            var done = true
            message = withContext(Dispatchers.IO) {
                fun keep(): String {
                    Outbox.addPath(context, DeviceClient.PHONE_TEXT_PATH, sending.toByteArray())
                    onOutbox()
                    return context.getString(R.string.phone_text_kept)
                }
                if (client == null) return@withContext keep()
                runCatching { client.sendPhoneText(sending) }.fold(
                    { context.getString(R.string.phone_text_sent) },
                    {
                        when (it) {
                            // Unreachable or refused: the old card file may already be gone, so the outbox keeps the text.
                            is java.io.IOException -> keep()
                            else -> { done = false; context.getString(R.string.send_failed, it.message ?: "") }
                        }
                    })
            }
            if (done) text = ""
            busy = false
        }
    }) { Text(stringResource(R.string.phone_text_send)) }
    if (message.isNotEmpty()) Text(message)
}

/** Shared books and comics: send them to /Books or /Comics on the reader, one after the other. */
@Composable
fun ShareBooksDialog(client: DeviceClient, books: List<File>, refused: List<String>, unreadable: List<String>,
                     onBusy: (Boolean) -> Unit, onKept: () -> Unit, onDone: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val main = remember { android.os.Handler(android.os.Looper.getMainLooper()) }
    var progress by remember { mutableStateOf<Float?>(null) }
    var messages by remember { mutableStateOf(emptyList<String>()) }
    var busy by remember { mutableStateOf(false) }
    var sent by remember { mutableStateOf(false) }
    // Closed in the middle of an upload (the screen went away): the upload is over for the caller too.
    androidx.compose.runtime.DisposableEffect(Unit) { onDispose { if (busy) onBusy(false) } }
    AlertDialog(
        onDismissRequest = { if (!busy) onDone() },
        title = { Text(stringResource(R.string.share_books_title, books.size)) },
        text = {
            Column {
                books.forEach { Text("${it.name}  (${sizeText(it.length())})") }
                if (refused.isNotEmpty()) {
                    Text(stringResource(R.string.share_refused, refused.joinToString(", ")), Modifier.padding(top = 8.dp))
                }
                if (unreadable.isNotEmpty()) {
                    Text(stringResource(R.string.share_unreadable, unreadable.joinToString(", ")), Modifier.padding(top = 8.dp))
                }
                progress?.let { LinearProgressIndicator(progress = { it }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) }
                messages.forEach { Text(it, Modifier.padding(top = 4.dp)) }
            }
        },
        confirmButton = {
            if (books.isNotEmpty() && !sent) {
                TextButton(enabled = !busy, onClick = {
                    busy = true
                    onBusy(true)
                    scope.launch {
                        val results = mutableListOf<String>()
                        books.forEachIndexed { i, book ->
                            val folder = shareFolderFor(book.name) ?: return@forEachIndexed
                            results += withContext(Dispatchers.IO) {
                                runCatching {
                                    client.ensureFolder(folder)
                                    client.upload(folder, book.name, book.length(), { book.inputStream() }) { done, total ->
                                        if (total > 0) main.post { progress = (i + done.toFloat() / total) / books.size }
                                    }
                                }.fold({ context.getString(R.string.share_sent, it, folder.trim('/')) }, {
                                    // Not lost: the outbox sends it when the reader is back.
                                    Outbox.addFile(context, folder, book)
                                    main.post { onKept() }
                                    context.getString(R.string.outbox_file_kept, book.name)
                                })
                            }
                            messages = results.toList()
                        }
                        busy = false
                        onBusy(false)
                        sent = true
                    }
                }) { Text(stringResource(R.string.comics_send)) }
            }
        },
        dismissButton = { TextButton(enabled = !busy, onClick = onDone) { Text(stringResource(R.string.close)) } },
    )
}

