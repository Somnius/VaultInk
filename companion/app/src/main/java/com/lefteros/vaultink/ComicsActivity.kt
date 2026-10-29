package com.lefteros.vaultink

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.Executors

/** Banana Comic Converter (an independent tool by u/curseofthebanana) inside the app, live from its site. */
const val CONVERTER_URL = "https://seethroughme.github.io/Banana-Comic-Converter/"
private const val CONVERTER_ORIGIN = "https://seethroughme.github.io"
private const val CONVERTER_HOST = "seethroughme.github.io"
private const val CONVERTER_PATH = "/Banana-Comic-Converter"
private const val MAX_FILE_BYTES = 1L shl 30  // 1 GB: far above anything the reader can open

/** A converted file the page handed over: kept in the cache until it is sent, saved or dropped. */
data class Converted(val file: File, val name: String)

/**
 * The converter's page in a WebView. Its finished files are blob: downloads, which a WebView cannot
 * save by itself, so a small script catches them (keeping the file name) and posts them to the app in
 * 1 MB pieces over a message channel that only the converter's site can use. Then: send to the
 * reader's /Comics folder, or save on the phone. When the site can't be reached, a screen says so.
 */
class ComicsActivity : ComponentActivity() {
    private val queue = mutableStateListOf<Converted>()
    private var receiving by mutableStateOf<String?>(null)
    private var failure by mutableStateOf("")
    private val receiver = Receiver()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        // Files from an earlier visit were sent, saved or dropped already. (Rotation does not come
        // here again: the manifest keeps the activity across configuration changes.)
        File(cacheDir, "comics").listFiles()?.forEach { it.delete() }
        setContent {
            MaterialTheme(colorScheme = if (isSystemInDarkTheme()) InkDark else InkLight) {
                ComicsScreen(
                    receiver = receiver,
                    current = queue.firstOrNull(),
                    receiving = receiving,
                    failure = failure,
                    onDone = { item -> item.file.delete(); queue.remove(item) },
                    onClose = { finish() },
                )
            }
        }
    }

    override fun onDestroy() {
        receiver.close()
        super.onDestroy()
    }

    /**
     * Messages from the page, in order: {t:"begin",n,s}, then {t:"chunk",d} pieces, then {t:"end"} or
     * {t:"fail",m}. Decoding and writing run on one background thread, so they keep their order and the
     * screen stays smooth; results go back to the main thread.
     */
    inner class Receiver {
        private val worker = Executors.newSingleThreadExecutor()
        private val main = Handler(Looper.getMainLooper())
        private var out: FileOutputStream? = null
        private var file: File? = null
        private var written = 0L

        fun onMessage(json: String) = worker.execute {
            runCatching {
                val msg = JSONObject(json)
                when (msg.optString("t")) {
                    "begin" -> {
                        closeQuietly()
                        if (msg.optDouble("s", 0.0) > MAX_FILE_BYTES) error(getString(R.string.comics_too_big))
                        val name = DeviceClient.safeName(msg.optString("n")).ifEmpty { "comic.xtch" }
                        val dir = File(cacheDir, "comics").apply { mkdirs() }
                        val f = unique(dir, name)
                        file = f
                        out = FileOutputStream(f)
                        written = 0
                        main.post { failure = ""; receiving = f.name }
                    }
                    "chunk" -> {
                        val bytes = Base64.decode(msg.getString("d"), Base64.DEFAULT)
                        written += bytes.size
                        if (written > MAX_FILE_BYTES) error(getString(R.string.comics_too_big))
                        (out ?: error("no file open")).write(bytes)
                    }
                    "end" -> {
                        val done = file ?: error("no file open")
                        out?.close()
                        out = null
                        file = null
                        val item = Converted(done, done.name)
                        main.post { receiving = null; queue.add(item) }
                    }
                    "fail" -> error(msg.optString("m"))
                }
            }.onFailure { e ->
                closeQuietly()
                main.post { receiving = null; failure = e.message ?: e.toString() }
            }
        }

        // A second file with the same name doesn't replace the one waiting in the dialog.
        private fun unique(dir: File, wanted: String): File {
            var f = File(dir, wanted)
            val dot = wanted.lastIndexOf('.').let { if (it > 0) it else wanted.length }
            var n = 2
            while (f.exists()) f = File(dir, wanted.substring(0, dot) + "-${n++}" + wanted.substring(dot))
            return f
        }

        // Drops a file that didn't arrive whole.
        private fun closeQuietly() {
            runCatching { out?.close() }
            out = null
            file?.delete()
            file = null
        }

        fun close() {
            worker.execute { closeQuietly() }
            worker.shutdown()
        }
    }
}

// Catches the page's downloads: links with a download name (clicked, or clicked from script, as
// "Download all as one ZIP" does) whose address is a blob: URL. One file at a time.
private const val CATCH_DOWNLOADS = """
(function(){
  if(window.__vaultink || typeof VaultInk === 'undefined') return; window.__vaultink = 1;
  const post = o => VaultInk.postMessage(JSON.stringify(o));
  let queue = Promise.resolve();
  window.__vaultinkSend = (url, name) => { queue = queue.then(() => sendOne(url, name)); return queue; };
  async function sendOne(url, name){
    try {
      const b = await (await fetch(url)).blob();
      post({t: 'begin', n: name || 'comic', s: b.size});
      const C = 1048576;
      for(let o = 0; o < b.size; o += C){
        const buf = new Uint8Array(await b.slice(o, o + C).arrayBuffer());
        let s = '';
        for(let i = 0; i < buf.length; i += 8192) s += String.fromCharCode.apply(null, buf.subarray(i, i + 8192));
        post({t: 'chunk', d: btoa(s)});
      }
      post({t: 'end'});
    } catch(err){ post({t: 'fail', m: String(err)}); }
  }
  const isBlobDownload = a => a && a.href && a.href.indexOf('blob:') === 0 && a.hasAttribute('download');
  document.addEventListener('click', e => {
    const a = e.target.closest && e.target.closest('a');
    if(!isBlobDownload(a)) return;
    e.preventDefault(); e.stopPropagation();
    window.__vaultinkSend(a.href, a.getAttribute('download'));
  }, true);
  const click = HTMLAnchorElement.prototype.click;
  HTMLAnchorElement.prototype.click = function(){
    if(isBlobDownload(this)){ window.__vaultinkSend(this.href, this.getAttribute('download')); return; }
    return click.call(this);
  };
})();
"""

/** The converter itself, not just its site: https, its host, and its own path (not a look-alike). */
private fun isConverter(url: Uri): Boolean {
    if (url.scheme != "https" || url.host != CONVERTER_HOST) return false
    val path = url.path.orEmpty()
    return (path == CONVERTER_PATH || path.startsWith("$CONVERTER_PATH/")) && !path.contains("..")
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
private fun ComicsScreen(
    receiver: ComicsActivity.Receiver,
    current: Converted?,
    receiving: String?,
    failure: String,
    onDone: (Converted) -> Unit,
    onClose: () -> Unit,
) {
    var webView by remember { mutableStateOf<WebView?>(null) }
    var fileCallback by remember { mutableStateOf<ValueCallback<Array<Uri>>?>(null) }
    var offline by remember { mutableStateOf(false) }
    val supported = remember { WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER) }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        // An empty pick must be reported as null, or the page's file box stops responding.
        fileCallback?.onReceiveValue(if (uris.isEmpty()) null else uris.toTypedArray())
        fileCallback = null
    }
    BackHandler {
        val web = webView
        if (!offline && web != null && web.canGoBack()) web.goBack() else onClose()
    }
    DisposableEffect(Unit) {
        onDispose {
            fileCallback?.onReceiveValue(null)
            webView?.apply { stopLoading(); destroy() }
        }
    }

    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.primaryContainer).statusBarsPadding()
        .background(MaterialTheme.colorScheme.background).navigationBarsPadding()) {
        Row(Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.primaryContainer)
            .padding(horizontal = 8.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onClose) { Text("←", color = MaterialTheme.colorScheme.onPrimaryContainer) }
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.comics_title), style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onPrimaryContainer)
                Text(stringResource(R.string.comics_credit), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer)
            }
        }
        if (receiving != null) {
            Text(stringResource(R.string.comics_receiving, receiving), Modifier.padding(horizontal = 16.dp, vertical = 4.dp))
            LinearProgressIndicator(Modifier.fillMaxWidth())
        }
        if (failure.isNotEmpty()) {
            Text(stringResource(R.string.comics_failed, failure), Modifier.padding(16.dp),
                color = MaterialTheme.colorScheme.error)
        }
        Box(Modifier.fillMaxWidth().weight(1f)) {
            if (!supported) {
                Unavailable(stringResource(R.string.comics_webview_old), onRetry = null)
            } else {
                AndroidView(modifier = Modifier.fillMaxSize(), factory = { ctx ->
                    WebView(ctx).apply {
                        settings.javaScriptEnabled = true
                        settings.domStorageEnabled = true
                        settings.allowFileAccess = false
                        settings.allowContentAccess = false
                        // Only the converter's own origin gets the channel; other sites and iframes don't.
                        WebViewCompat.addWebMessageListener(this, "VaultInk", setOf(CONVERTER_ORIGIN)) { _, message, origin, isMainFrame, _ ->
                            if (isMainFrame && origin.toString().trimEnd('/') == CONVERTER_ORIGIN) {
                                message.data?.let { receiver.onMessage(it) }
                            }
                        }
                        webViewClient = object : WebViewClient() {
                            // The converter stays here; its Ko-fi or GitHub links open in the browser.
                            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                                val url = request.url
                                if (isConverter(url)) return false
                                if (url.scheme == "https" || url.scheme == "http") {
                                    runCatching { ctx.startActivity(Intent(Intent.ACTION_VIEW, url)) }
                                }
                                return true
                            }

                            override fun onPageFinished(view: WebView, url: String) {
                                if (isConverter(Uri.parse(url))) view.evaluateJavascript(CATCH_DOWNLOADS, null)
                            }

                            // No connection, no DNS, the site gone: our own screen, not a broken page.
                            override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
                                if (request.isForMainFrame) offline = true
                            }

                            override fun onReceivedHttpError(view: WebView, request: WebResourceRequest,
                                                             response: WebResourceResponse) {
                                if (request.isForMainFrame && response.statusCode >= 400) offline = true
                            }
                        }
                        webChromeClient = object : WebChromeClient() {
                            override fun onShowFileChooser(view: WebView, callback: ValueCallback<Array<Uri>>,
                                                           params: FileChooserParams): Boolean {
                                fileCallback?.onReceiveValue(null)
                                fileCallback = callback
                                pick.launch(arrayOf("*/*"))
                                return true
                            }
                        }
                        // A blob: download the script didn't catch goes the same way.
                        setDownloadListener { url, _, _, _, _ ->
                            if (url.startsWith("blob:")) {
                                evaluateJavascript("window.__vaultinkSend && window.__vaultinkSend(${quote(url)}, 'comic')", null)
                            }
                        }
                        loadUrl(CONVERTER_URL)
                        webView = this
                    }
                })
                if (offline) {
                    Unavailable(stringResource(R.string.comics_unavailable), onRetry = {
                        offline = false
                        webView?.loadUrl(CONVERTER_URL)
                    })
                }
            }
        }
    }
    current?.let { item -> SendDialog(item) { onDone(item) } }
}

/** Shown over the page when the converter can't be reached (or this WebView is too old for it). */
@Composable
private fun Unavailable(message: String, onRetry: (() -> Unit)?) {
    val context = LocalContext.current
    Column(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Text("🍌", style = MaterialTheme.typography.displaySmall)
        Text(message, textAlign = TextAlign.Center)
        if (onRetry != null) Button(onClick = onRetry) { Text(stringResource(R.string.comics_retry)) }
        OutlinedButton(onClick = {
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(CONVERTER_URL))) }
        }) { Text(stringResource(R.string.comics_open_browser)) }
    }
}

private fun quote(s: String) = "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"

/** Send the converted file to the reader's /Comics folder, or save it on the phone. */
@Composable
private fun SendDialog(item: Converted, onDone: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val main = remember { Handler(Looper.getMainLooper()) }
    var progress by remember(item) { mutableStateOf<Float?>(null) }
    var message by remember(item) { mutableStateOf("") }
    var busy by remember(item) { mutableStateOf(false) }
    val save = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        busy = true
        scope.launch {
            message = withContext(Dispatchers.IO) {
                runCatching {
                    context.contentResolver.openOutputStream(uri)!!.use { out -> item.file.inputStream().use { it.copyTo(out) } }
                }.fold({ context.getString(R.string.downloaded) }, { context.getString(R.string.action_failed, it.message ?: "") })
            }
            busy = false
        }
    }
    AlertDialog(
        onDismissRequest = { if (!busy) onDone() },
        title = { Text(stringResource(R.string.comics_ready, item.name, sizeText(item.file.length()))) },
        text = {
            Column {
                Text(stringResource(R.string.comics_ready_hint))
                progress?.let { LinearProgressIndicator(progress = { it }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) }
                if (message.isNotEmpty()) Text(message, Modifier.padding(top = 8.dp))
            }
        },
        confirmButton = {
            TextButton(enabled = !busy, onClick = {
                val host = context.getSharedPreferences("vaultink", Context.MODE_PRIVATE).getString("host", "") ?: ""
                if (host.isEmpty()) { message = context.getString(R.string.comics_no_reader); return@TextButton }
                busy = true
                progress = 0f
                scope.launch {
                    message = withContext(Dispatchers.IO) {
                        runCatching {
                            val client = DeviceClient(host)
                            client.ensureFolder("/Comics")
                            client.upload("/Comics", item.name, item.file.length(), { item.file.inputStream() }) { done, total ->
                                if (total > 0) main.post { progress = done.toFloat() / total }
                            }
                        }.fold({ context.getString(R.string.comics_sent, it) },
                            { context.getString(R.string.comics_send_failed, it.message ?: "") })
                    }
                    busy = false
                }
            }) { Text(stringResource(R.string.comics_send)) }
        },
        dismissButton = {
            Row {
                TextButton(enabled = !busy, onClick = { save.launch(item.name) }) { Text(stringResource(R.string.comics_save)) }
                TextButton(enabled = !busy, onClick = onDone) { Text(stringResource(R.string.close)) }
            }
        },
    )
}
