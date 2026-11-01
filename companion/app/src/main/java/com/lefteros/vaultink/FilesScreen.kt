package com.lefteros.vaultink

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** A transfer under way: what it is, and how far it got (0..1, or null when the size is unknown). */
data class Transfer(val label: String, val fraction: Float?)

private fun child(dir: String, name: String) = if (dir == "/") "/$name" else "$dir/$name"
private fun parentOf(path: String) = path.substringBeforeLast('/').ifEmpty { "/" }

fun sizeText(bytes: Long): String = when {
    bytes < 1024 -> "$bytes B"
    bytes < 1024 * 1024 -> "%.1f KB".format(bytes / 1024.0)
    else -> "%.1f MB".format(bytes / (1024.0 * 1024.0))
}

private fun displayName(context: Context, uri: Uri): Pair<String, Long> {
    var name = "file"
    var size = -1L
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)
        ?.use { c ->
            if (c.moveToFirst()) {
                name = c.getString(0) ?: name
                if (!c.isNull(1)) size = c.getLong(1)
            }
        }
    return name to size
}

/** What a file is, from its extension, so the list reads at a glance. */
enum class FileKind(val icon: String) { BOOK("📘"), COMIC("📚"), NOTE("📝"), IMAGE("🖼️"), OTHER("📄") }

fun fileKind(name: String): FileKind = when (name.substringAfterLast('.', "").lowercase()) {
    "epub", "txt", "pdf", "fb2", "mobi", "azw3" -> FileKind.BOOK
    "cbz", "cbr", "xtc", "xtch" -> FileKind.COMIC
    "md", "markdown", "canvas", "excalidraw", "json", "yaml", "yml" -> FileKind.NOTE
    "jpg", "jpeg", "png", "gif", "bmp", "webp", "svg" -> FileKind.IMAGE
    else -> FileKind.OTHER
}

private val TOOLBAR_PADDING = PaddingValues(horizontal = 12.dp, vertical = 6.dp)

@Composable
private fun ToolbarLabel(text: String) = Text(text, maxLines = 1, softWrap = false)

/** The extension in upper case on a small rounded chip, tinted by the file's kind. */
@Composable
private fun TypeChip(ext: String, kind: FileKind) {
    val scheme = MaterialTheme.colorScheme
    val (bg, fg) = when (kind) {
        FileKind.BOOK -> scheme.primaryContainer to scheme.onPrimaryContainer
        FileKind.COMIC -> scheme.tertiaryContainer to scheme.onTertiaryContainer
        FileKind.NOTE -> scheme.secondaryContainer to scheme.onSecondaryContainer
        FileKind.IMAGE -> scheme.surfaceVariant to scheme.onSurfaceVariant
        FileKind.OTHER -> scheme.surfaceVariant to scheme.onSurfaceVariant
    }
    Text(ext, Modifier.background(bg, RoundedCornerShape(4.dp)).padding(horizontal = 6.dp, vertical = 1.dp),
        style = MaterialTheme.typography.labelSmall, color = fg, maxLines = 1, softWrap = false)
}

/** The SD card: browse, upload, download, new folder, rename, move and delete. */
@Composable
fun FilesScreen(client: DeviceClient) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var dir by remember { mutableStateOf("/") }
    var entries by remember { mutableStateOf<List<Entry>?>(null) }
    var reload by remember { mutableIntStateOf(0) }
    var message by remember { mutableStateOf("") }
    var transfer by remember { mutableStateOf<Transfer?>(null) }
    var menuFor by remember { mutableStateOf<Entry?>(null) }
    var dialog by remember { mutableStateOf<String?>(null) }  // "mkdir", "rename", "delete", "move"
    var target by remember { mutableStateOf<Entry?>(null) }
    var pendingDownload by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(dir, reload) {
        entries = null
        entries = withContext(Dispatchers.IO) { runCatching { client.list(dir) } }
            .onFailure { message = context.getString(R.string.files_failed, it.message ?: "") }
            .getOrDefault(emptyList())
            .sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() }))
    }

    // Runs a device change off the main thread, then shows the folder again.
    fun act(done: Int, block: () -> Unit) {
        scope.launch {
            message = withContext(Dispatchers.IO) {
                runCatching(block).fold({ context.getString(done) },
                    { context.getString(R.string.action_failed, it.message ?: "") })
            }
            reload++
        }
    }

    val upload = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult
        val folder = dir
        scope.launch {
            var sent = 0
            var failure = ""
            withContext(Dispatchers.IO) {
                uris.forEachIndexed { i, uri ->
                    val (name, size) = displayName(context, uri)
                    val label = context.getString(R.string.uploading, name, i + 1, uris.size)
                    transfer = Transfer(label, if (size > 0) 0f else null)
                    runCatching {
                        client.upload(folder, DeviceClient.safeName(name).ifEmpty { "file" }, size,
                            { context.contentResolver.openInputStream(uri)!! }) { done, total ->
                            transfer = Transfer(label, if (total > 0) done.toFloat() / total else null)
                        }
                    }.onSuccess { sent++ }.onFailure { failure = it.message ?: "" }
                }
            }
            transfer = null
            message = if (failure.isEmpty()) context.resources.getQuantityString(R.plurals.uploaded, sent, sent)
            else context.getString(R.string.upload_partly, sent, uris.size, failure)
            reload++
        }
    }

    val download = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("*/*")) { uri ->
        val path = pendingDownload
        pendingDownload = null
        if (uri == null || path == null) return@rememberLauncherForActivityResult
        scope.launch {
            val label = context.getString(R.string.downloading, path.substringAfterLast('/'))
            transfer = Transfer(label, 0f)
            message = withContext(Dispatchers.IO) {
                runCatching {
                    context.contentResolver.openOutputStream(uri)!!.use { out ->
                        client.download(path, out) { done, total ->
                            transfer = Transfer(label, if (total > 0) done.toFloat() / total else null)
                        }
                    }
                }.fold({ context.getString(R.string.downloaded) },
                    { context.getString(R.string.action_failed, it.message ?: "") })
            }
            transfer = null
        }
    }

    // Where we are, each part tappable.
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
        TextButton(onClick = { dir = "/" }) { Text(stringResource(R.string.sd_card)) }
        var path = ""
        for (part in dir.trim('/').split('/').filter { it.isNotEmpty() }) {
            path = "$path/$part"
            val to = path
            Text("›")
            TextButton(onClick = { dir = to }) { Text(part) }
        }
    }
    // One line each, never wrapped mid-word ("Πά νω"); the row scrolls sideways when the four don't fit.
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Button(enabled = transfer == null, onClick = { upload.launch(arrayOf("*/*")) }, contentPadding = TOOLBAR_PADDING) {
            ToolbarLabel(stringResource(R.string.upload))
        }
        OutlinedButton(onClick = { target = null; dialog = "mkdir" }, contentPadding = TOOLBAR_PADDING) {
            ToolbarLabel(stringResource(R.string.new_folder))
        }
        if (dir != "/") OutlinedButton(onClick = { dir = parentOf(dir) }, contentPadding = TOOLBAR_PADDING) {
            ToolbarLabel("↑ " + stringResource(R.string.up))
        }
        // Banana Comic Converter inside the app; its files go to the reader's /Comics folder.
        OutlinedButton(onClick = { context.startActivity(android.content.Intent(context, ComicsActivity::class.java)) },
            contentPadding = TOOLBAR_PADDING) {
            ToolbarLabel("📚 " + stringResource(R.string.comics_open))
        }
    }
    transfer?.let { t ->
        Text(t.label, style = MaterialTheme.typography.bodyMedium)
        val f = t.fraction
        if (f == null) LinearProgressIndicator(Modifier.fillMaxWidth())
        else {
            LinearProgressIndicator(progress = { f }, modifier = Modifier.fillMaxWidth())
            Text("${(f * 100).toInt()}%", style = MaterialTheme.typography.bodySmall)
        }
    }
    if (message.isNotEmpty()) Text(message, style = MaterialTheme.typography.bodySmall)

    val list = entries
    OutlinedCard(Modifier.fillMaxWidth()) {
        when {
            list == null -> Text(stringResource(R.string.loading), Modifier.padding(16.dp))
            list.isEmpty() -> Text(stringResource(R.string.empty_folder), Modifier.padding(16.dp))
            else -> list.forEachIndexed { i, e ->
                if (i > 0) HorizontalDivider()
                Row(
                    Modifier.fillMaxWidth().heightIn(min = 56.dp)
                        .clickable { if (e.isDirectory) dir = child(dir, e.name) else menuFor = e }
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    val kind = if (e.isDirectory) null else fileKind(e.name)
                    Text(kind?.icon ?: "📁", Modifier.padding(end = 12.dp))
                    Column(Modifier.weight(1f)) {
                        Text(e.name, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        // The type as a chip, since a long name loses its extension to the ellipsis.
                        if (kind != null) Row(verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 2.dp)) {
                            TypeChip(e.name.substringAfterLast('.', "").ifEmpty { "?" }.uppercase(), kind)
                            Text(sizeText(e.size), style = MaterialTheme.typography.bodySmall)
                        }
                    }
                    Column {
                        TextButton(onClick = { menuFor = e }) { Text("⋮") }
                        DropdownMenu(expanded = menuFor == e, onDismissRequest = { menuFor = null }) {
                            if (!e.isDirectory) {
                                DropdownMenuItem(text = { Text(stringResource(R.string.download)) }, onClick = {
                                    menuFor = null
                                    pendingDownload = child(dir, e.name)
                                    download.launch(e.name)
                                })
                                DropdownMenuItem(text = { Text(stringResource(R.string.rename)) },
                                    onClick = { menuFor = null; target = e; dialog = "rename" })
                                DropdownMenuItem(text = { Text(stringResource(R.string.move)) },
                                    onClick = { menuFor = null; target = e; dialog = "move" })
                            }
                            DropdownMenuItem(text = { Text(stringResource(R.string.delete)) },
                                onClick = { menuFor = null; target = e; dialog = "delete" })
                        }
                    }
                }
            }
        }
    }
    if (list != null && list.any { it.isDirectory }) {
        Text(stringResource(R.string.folders_note), style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant)
    }

    val t = target
    when (dialog) {
        "mkdir" -> NameDialog(stringResource(R.string.new_folder), "", onDismiss = { dialog = null }) { name ->
            dialog = null
            act(R.string.folder_made) { client.mkdir(dir, name) }
        }
        "rename" -> if (t != null) NameDialog(stringResource(R.string.rename), t.name, onDismiss = { dialog = null }) { name ->
            dialog = null
            act(R.string.renamed) { client.rename(child(dir, t.name), name) }
        }
        "delete" -> if (t != null) AlertDialog(
            onDismissRequest = { dialog = null },
            text = { Text(stringResource(if (t.isDirectory) R.string.delete_folder_ask else R.string.delete_ask, t.name)) },
            confirmButton = {
                TextButton(onClick = {
                    dialog = null
                    act(R.string.deleted) { client.delete(child(dir, t.name)) }
                }) { Text(stringResource(R.string.delete)) }
            },
            dismissButton = { TextButton(onClick = { dialog = null }) { Text(stringResource(R.string.cancel)) } },
        )
        "move" -> if (t != null) FolderPicker(client, dir, t.name, onDismiss = { dialog = null }) { dest ->
            dialog = null
            act(R.string.moved) { client.move(child(dir, t.name), dest) }
        }
    }
}

@Composable
private fun NameDialog(title: String, initial: String, onDismiss: () -> Unit, onDone: (String) -> Unit) {
    var name by remember { mutableStateOf(initial) }
    val clean = DeviceClient.safeName(name)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { OutlinedTextField(name, { name = it }, singleLine = true, modifier = Modifier.fillMaxWidth()) },
        confirmButton = { TextButton(enabled = clean.isNotEmpty() && clean != initial, onClick = { onDone(clean) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

/** Browse the card's folders and pick where a file goes. */
@Composable
private fun FolderPicker(client: DeviceClient, start: String, fileName: String, onDismiss: () -> Unit,
                         onPick: (String) -> Unit) {
    var dir by remember { mutableStateOf(start) }
    var folders by remember { mutableStateOf<List<String>?>(null) }
    LaunchedEffect(dir) {
        folders = null
        folders = withContext(Dispatchers.IO) {
            runCatching { client.list(dir).filter { it.isDirectory }.map { it.name }.sortedBy { it.lowercase() } }
                .getOrDefault(emptyList())
        }
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.move_title, fileName)) },
        text = {
            Column(Modifier.heightIn(max = 400.dp).verticalScroll(rememberScrollState())) {
                Text(dir, style = MaterialTheme.typography.labelLarge)
                if (dir != "/") TextButton(onClick = { dir = parentOf(dir) }) { Text("↑ " + stringResource(R.string.up)) }
                folders?.forEach { f -> TextButton(onClick = { dir = child(dir, f) }) { Text("📁 $f") } }
            }
        },
        confirmButton = { TextButton(enabled = dir != start, onClick = { onPick(dir) }) { Text(stringResource(R.string.move_here)) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}
