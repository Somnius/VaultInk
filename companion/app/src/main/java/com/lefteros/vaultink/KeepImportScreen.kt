package com.lefteros.vaultink

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
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
import java.io.IOException
import java.util.zip.ZipInputStream

/** Where imported Keep notes go inside the vault. */
const val KEEP_FOLDER = "Imported/Keep"

/**
 * Bring notes in: a Google Keep Takeout (the .zip, or the unzipped Takeout/Keep folder) converted to
 * Markdown and sent into `Imported/Keep` of `vault`. With no reader (`client` null) or when it goes
 * away mid-way, the rest waits in the outbox.
 */
@Composable
fun KeepImportDialog(client: DeviceClient?, vault: String, onOutbox: () -> Unit, onDone: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var batch by remember { mutableStateOf<KeepImport.Batch?>(null) }
    var message by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var progress by remember { mutableStateOf<Float?>(null) }
    var finished by remember { mutableStateOf(false) }

    fun read(load: suspend () -> List<Pair<String, String>>) {
        busy = true
        message = context.getString(R.string.import_reading)
        scope.launch {
            val result = withContext(Dispatchers.IO) { runCatching { KeepImport.convertAll(load()) } }
            result.onSuccess {
                batch = it
                message = if (it.total == 0) context.getString(R.string.import_nothing) else ""
            }.onFailure { message = context.getString(R.string.import_failed, it.message ?: "") }
            busy = false
        }
    }
    val pickZip = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) read { readZip(context, uri) }
    }
    val pickFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) read { readTree(context, uri) }
    }

    AlertDialog(
        onDismissRequest = { if (!busy) onDone() },
        title = { Text(stringResource(R.string.import_title)) },
        text = {
            Column {
                Text(stringResource(R.string.import_keep_intro, vault))
                val b = batch
                if (b == null || finished) {
                    if (!finished) {
                        OutlinedButton(enabled = !busy, onClick = { pickZip.launch(arrayOf("application/zip", "application/x-zip-compressed")) },
                            modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) { Text(stringResource(R.string.import_pick_zip)) }
                        OutlinedButton(enabled = !busy, onClick = { pickFolder.launch(null) },
                            modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.import_pick_folder)) }
                    }
                } else if (b.total > 0) {
                    Text(pluralStringResource(R.plurals.import_ready, b.notes.size, b.notes.size), Modifier.padding(top = 8.dp))
                    if (b.trashed > 0 || b.unreadable > 0) {
                        Text(stringResource(R.string.import_skipped, b.trashed, b.unreadable))
                    }
                }
                progress?.let { LinearProgressIndicator(progress = { it }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) }
                if (message.isNotEmpty()) Text(message, Modifier.padding(top = 8.dp))
            }
        },
        confirmButton = {
            val b = batch
            if (b != null && b.notes.isNotEmpty() && !finished) {
                TextButton(enabled = !busy, onClick = {
                    busy = true
                    scope.launch {
                        message = withContext(Dispatchers.IO) { send(context, client, vault, b.notes) { done -> progress = done } }
                        progress = null
                        busy = false
                        finished = true
                        onOutbox()
                    }
                }) { Text(stringResource(R.string.import_send, b.notes.size)) }
            }
        },
        dismissButton = { TextButton(enabled = !busy, onClick = onDone) { Text(stringResource(R.string.close)) } },
    )
}

/** Uploads one note after the other; from the first reader failure on, the rest go to the outbox. */
private fun send(context: Context, client: DeviceClient?, vault: String, notes: List<KeepImport.Converted>,
                 progress: (Float) -> Unit): String {
    val folder = "/Vaults/$vault/$KEEP_FOLDER"
    var sent = 0
    var reader = client
    if (reader != null) {
        try {
            reader.ensureFolder(folder)
            for ((i, note) in notes.withIndex()) {
                reader.upload(folder, note.fileName, note.markdown.toByteArray(), "text/markdown")
                sent++
                progress((i + 1).toFloat() / notes.size)
            }
        } catch (e: IOException) {
            reader = null  // the reader went away: keep the rest
        }
    }
    val kept = notes.size - sent
    for (note in notes.drop(sent)) Outbox.addMarkdown(context, vault, KEEP_FOLDER, note.fileName, note.markdown)
    return when {
        kept == 0 -> context.getString(R.string.import_done, sent, "$vault/$KEEP_FOLDER")
        sent == 0 -> context.getString(R.string.import_kept, kept)
        else -> context.getString(R.string.import_done, sent, "$vault/$KEEP_FOLDER") + " " + context.getString(R.string.import_kept, kept)
    }
}

/** The JSON files of a Takeout .zip: those in a `Keep` folder when there is one, else every .json. */
private fun readZip(context: Context, uri: Uri): List<Pair<String, String>> {
    val all = mutableListOf<Pair<String, String>>()
    val input = context.contentResolver.openInputStream(uri) ?: throw IOException("can't open")
    ZipInputStream(input.buffered()).use { zip ->
        while (true) {
            val entry = zip.nextEntry ?: break
            if (!entry.isDirectory && entry.name.endsWith(".json", ignoreCase = true) && entry.size < 4L * 1024 * 1024) {
                all += entry.name to zip.readBytes().toString(Charsets.UTF_8)
            }
            zip.closeEntry()
        }
    }
    val inKeep = all.filter { it.first.contains("/Keep/") }
    return if (inKeep.isNotEmpty()) inKeep else all
}

/** The JSON files of a picked folder and its subfolders (the unzipped `Takeout/Keep`, or `Takeout`). */
private fun readTree(context: Context, tree: Uri): List<Pair<String, String>> {
    val out = mutableListOf<Pair<String, String>>()
    fun walk(documentId: String, depth: Int) {
        if (depth > 3) return
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId)
        val columns = arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE)
        context.contentResolver.query(children, columns, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0)
                val name = c.getString(1) ?: continue
                val mime = c.getString(2)
                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    walk(id, depth + 1)
                } else if (name.endsWith(".json", ignoreCase = true)) {
                    val file = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                    val text = context.contentResolver.openInputStream(file)?.use { it.readBytes().toString(Charsets.UTF_8) } ?: continue
                    out += name to text
                }
            }
        }
    }
    walk(DocumentsContract.getTreeDocumentId(tree), 0)
    return out
}
