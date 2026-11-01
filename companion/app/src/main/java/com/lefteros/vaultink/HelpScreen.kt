package com.lefteros.vaultink

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A bundled document: its file name under assets/help and its first heading. */
data class HelpDoc(val file: String, val title: String)

private const val HELP_DIR = "help"

/** Reads help/index.txt (made at build time from the repo's docs): "file<TAB>title" per line. */
fun helpIndex(context: Context): List<HelpDoc> = runCatching {
    context.assets.open("$HELP_DIR/index.txt").bufferedReader().readLines()
        .mapNotNull { l -> l.split('\t').takeIf { it.size == 2 }?.let { HelpDoc(it[0], it[1]) } }
}.getOrDefault(emptyList())

fun helpText(context: Context, file: String): String? =
    runCatching { context.assets.open("$HELP_DIR/$file").bufferedReader().readText() }.getOrNull()

/** Where the reader is in Help: the list, or a document (with an optional heading to scroll to). */
private data class HelpPlace(val file: String, val anchor: String?)

/**
 * VaultInk's documentation, offline: a list of documents with a search box (titles and words inside
 * them), and a Markdown view. Links to other bundled documents open in the app, http links in the
 * browser. Back returns along the way the reader came.
 */
@Composable
fun HelpScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val docs = remember { helpIndex(context) }
    var texts by remember { mutableStateOf<Map<String, String>>(emptyMap()) }
    var trail by remember { mutableStateOf<List<HelpPlace>>(emptyList()) }
    var query by remember { mutableStateOf("") }
    LaunchedEffect(Unit) {
        texts = withContext(Dispatchers.IO) { docs.associate { it.file to (helpText(context, it.file) ?: "") } }
    }
    BackHandler { if (trail.isEmpty()) onBack() else trail = trail.dropLast(1) }
    fun openLink(from: String, href: String) {
        val local = Markdown.docLink(href)
        if (local == null) {
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(href))) }
            return
        }
        val (file, anchor) = local
        trail = trail + HelpPlace(file.ifEmpty { from }, anchor)
    }

    val place = trail.lastOrNull()
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { if (trail.isEmpty()) onBack() else trail = trail.dropLast(1) }) {
                Text("‹ " + stringResource(R.string.back))
            }
            Text(if (place == null) stringResource(R.string.menu_help)
                 else docs.firstOrNull { it.file == place.file }?.title ?: place.file,
                style = MaterialTheme.typography.titleMedium, maxLines = 1)
        }
        if (place == null) {
            HelpList(docs, texts, query, onQuery = { query = it }) { trail = trail + HelpPlace(it, null) }
        } else {
            val text = texts[place.file]
            if (text == null) Text(stringResource(R.string.help_missing, place.file), Modifier.padding(16.dp))
            else MarkdownView(text, place.anchor) { openLink(place.file, it) }
        }
    }
}

@Composable
private fun HelpList(docs: List<HelpDoc>, texts: Map<String, String>, query: String, onQuery: (String) -> Unit,
                     onOpen: (String) -> Unit) {
    val q = query.trim()
    // A document matches by title, or by a word inside it: then its first matching line is shown.
    val shown = if (q.isEmpty()) docs.map { it to null } else docs.mapNotNull { d ->
        when {
            d.title.contains(q, ignoreCase = true) -> d to null
            else -> Markdown.firstMatch(texts[d.file].orEmpty(), q)?.let { d to it }
        }
    }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = androidx.compose.foundation.layout.PaddingValues(bottom = 16.dp)) {
        item {
            OutlinedTextField(query, onQuery, label = { Text(stringResource(R.string.help_search)) }, singleLine = true,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp))
            Text(stringResource(R.string.help_english_only), Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (shown.isEmpty()) Text(stringResource(R.string.help_no_results, q), Modifier.padding(16.dp))
        }
        itemsIndexed(shown) { i, (doc, line) ->
            if (i > 0) HorizontalDivider()
            Column(Modifier.fillMaxWidth().clickable { onOpen(doc.file) }.padding(horizontal = 16.dp, vertical = 12.dp)) {
                Text(doc.title, style = MaterialTheme.typography.bodyLarge)
                Text(if (line != null) line else doc.file, style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 2)
            }
        }
    }
}

/** Renders parsed Markdown; [anchor] names the heading to start at. */
@Composable
fun MarkdownView(text: String, anchor: String?, onLink: (String) -> Unit) {
    val blocks = remember(text) { Markdown.parse(text) }
    val state = rememberLazyListState()
    LaunchedEffect(blocks, anchor) {
        val at = if (anchor == null) 0 else blocks.indexOfFirst { it is Markdown.Heading && it.anchor == anchor }
        state.scrollToItem(at.coerceAtLeast(0))
    }
    val linkColor = MaterialTheme.colorScheme.primary
    val codeBackground = MaterialTheme.colorScheme.surfaceVariant
    fun spans(list: List<Markdown.Span>): AnnotatedString = buildAnnotatedString {
        for (s in list) {
            val style = SpanStyle(
                fontWeight = if (s.bold) FontWeight.Bold else null,
                fontStyle = if (s.italic) FontStyle.Italic else null,
                fontFamily = if (s.code) FontFamily.Monospace else null,
                background = if (s.code) codeBackground else androidx.compose.ui.graphics.Color.Unspecified,
            )
            if (s.href != null) {
                val href = s.href
                withLink(LinkAnnotation.Clickable(href,
                    TextLinkStyles(style.copy(color = linkColor, textDecoration = TextDecoration.Underline))) { onLink(href) }) {
                    append(s.text)
                }
            } else withStyle(style) { append(s.text) }
        }
    }
    val body = MaterialTheme.typography.bodyMedium
    LazyColumn(Modifier.fillMaxSize(), state = state,
        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 16.dp, vertical = 8.dp)) {
        itemsIndexed(blocks) { _, b ->
            when (b) {
                is Markdown.Heading -> Text(spans(b.spans), Modifier.padding(top = if (b.level <= 2) 20.dp else 12.dp, bottom = 6.dp),
                    style = when (b.level) {
                        1 -> MaterialTheme.typography.headlineSmall
                        2 -> MaterialTheme.typography.titleLarge
                        3 -> MaterialTheme.typography.titleMedium
                        else -> MaterialTheme.typography.titleSmall
                    })
                is Markdown.Paragraph -> Text(spans(b.spans), Modifier.padding(vertical = 4.dp), style = body)
                is Markdown.ListItem -> Row(Modifier.padding(start = (b.level * 16).dp, top = 2.dp, bottom = 2.dp)) {
                    Text(if (b.number != null) "${b.number}." else "•", Modifier.width(24.dp), style = body)
                    Text(spans(b.spans), style = body)
                }
                is Markdown.Code -> Box(Modifier.fillMaxWidth().padding(vertical = 6.dp).background(codeBackground, RoundedCornerShape(6.dp))
                    .horizontalScroll(rememberScrollState()).padding(10.dp)) {
                    Text(b.text, style = TextStyle(fontFamily = FontFamily.Monospace, fontSize = body.fontSize), softWrap = false)
                }
                is Markdown.Quote -> Row(Modifier.padding(vertical = 4.dp).height(IntrinsicSize.Min)) {
                    Box(Modifier.width(3.dp).fillMaxHeight().background(MaterialTheme.colorScheme.outlineVariant))
                    Text(spans(b.spans), Modifier.padding(start = 10.dp), style = body,
                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                // A table as readable rows: "header: cell" per column, a line between rows.
                is Markdown.Table -> Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                    b.rows.forEachIndexed { r, row ->
                        if (r > 0) HorizontalDivider(Modifier.padding(vertical = 4.dp))
                        row.forEachIndexed { c, cell ->
                            val head = b.header.getOrNull(c)?.let { Markdown.plain(it) }.orEmpty()
                            Text(buildAnnotatedString {
                                if (head.isNotEmpty()) withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append("$head: ") }
                                append(spans(cell))
                            }, style = body)
                        }
                    }
                }
                is Markdown.Image -> Text(stringResource(R.string.help_image, b.alt), Modifier.padding(vertical = 4.dp),
                    style = MaterialTheme.typography.bodySmall, fontStyle = FontStyle.Italic,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Markdown.Rule -> HorizontalDivider(Modifier.padding(vertical = 10.dp))
            }
        }
    }
}
