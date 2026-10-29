package com.lefteros.vaultink

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch

/** First-run pages: what the app is, what the reader needs, what each tab does. */
@Composable
fun IntroScreen(onDone: () -> Unit) {
    val pages = 3
    val pager = rememberPagerState { pages }
    val scope = rememberCoroutineScope()
    BackHandler(enabled = pager.currentPage > 0) { scope.launch { pager.animateScrollToPage(pager.currentPage - 1) } }
    Column(Modifier.fillMaxSize().padding(24.dp)) {
        HorizontalPager(pager, Modifier.weight(1f)) { page ->
            // Centred when it fits; a tall page (big font) scrolls from its top instead of clipping.
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()),
                horizontalAlignment = Alignment.CenterHorizontally) {
                when (page) {
                    0 -> {
                        PhoneAndReader()
                        IntroText(R.string.intro_what_title, R.string.intro_what_body)
                    }
                    1 -> {
                        ReaderScreen()
                        IntroText(R.string.intro_reader_title, R.string.intro_reader_body)
                    }
                    else -> {
                        Text(stringResource(R.string.intro_tabs_title), style = MaterialTheme.typography.headlineSmall)
                        Spacer(Modifier.height(16.dp))
                        listOf("✏️" to R.string.intro_tab_note, "📁" to R.string.intro_tab_files,
                            "⚙️" to R.string.intro_tab_settings, "📟" to R.string.intro_tab_device).forEach { (icon, text) ->
                            Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                                Box(Modifier.size(44.dp).background(MaterialTheme.colorScheme.primaryContainer, CircleShape),
                                    contentAlignment = Alignment.Center) { Text(icon, fontSize = 20.sp) }
                                Text(stringResource(text), Modifier.padding(start = 16.dp))
                            }
                        }
                    }
                }
            }
            }
        }
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            val last = pager.currentPage == pages - 1
            Box(Modifier.weight(1f)) {
                if (!last) TextButton(onClick = onDone) { Text(stringResource(R.string.intro_skip)) }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                repeat(pages) { i ->
                    Box(Modifier.size(8.dp).background(
                        if (i == pager.currentPage) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.outlineVariant, CircleShape))
                }
            }
            Box(Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
                Button(onClick = { if (last) onDone() else scope.launch { pager.animateScrollToPage(pager.currentPage + 1) } }) {
                    Text(stringResource(if (last) R.string.intro_start else R.string.intro_next))
                }
            }
        }
    }
}

@Composable
private fun IntroText(title: Int, body: Int) {
    Spacer(Modifier.height(32.dp))
    Text(stringResource(title), style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
    Spacer(Modifier.height(12.dp))
    Text(stringResource(body), style = MaterialTheme.typography.bodyLarge, textAlign = TextAlign.Center)
}

/** A phone and the e-reader with Wi-Fi between them. */
@Composable
private fun PhoneAndReader() {
    val outline = MaterialTheme.colorScheme.outline
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(width = 56.dp, height = 100.dp).border(2.dp, outline, RoundedCornerShape(10.dp)),
            contentAlignment = Alignment.Center) { Text("📱", fontSize = 22.sp) }
        Column(Modifier.width(80.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("📶", fontSize = 22.sp)
            Text("- - - - -", color = outline)
        }
        Box(Modifier.size(width = 96.dp, height = 128.dp)
            .background(Color(0xFFEDEBE6), RoundedCornerShape(8.dp)).border(2.dp, outline, RoundedCornerShape(8.dp)),
            contentAlignment = Alignment.Center) { Text("📖", fontSize = 28.sp) }
    }
}

/** The reader's screen with File Transfer picked. */
@Composable
private fun ReaderScreen() {
    val outline = MaterialTheme.colorScheme.outline
    Column(Modifier.width(220.dp).background(Color(0xFFEDEBE6), RoundedCornerShape(8.dp))
        .border(2.dp, outline, RoundedCornerShape(8.dp)).padding(12.dp)) {
        Text("VaultInk", color = Color(0xFF111111), style = MaterialTheme.typography.labelLarge)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.reader_file_transfer), color = Color.White,
            modifier = Modifier.fillMaxWidth().background(Color(0xFF111111), RoundedCornerShape(4.dp)).padding(8.dp))
        Spacer(Modifier.height(6.dp))
        Text("http://vaultink.local", color = Color(0xFF444444), style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(start = 8.dp))
    }
}
