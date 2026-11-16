package com.lefteros.vaultink

import okhttp3.FormBody
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.asRequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import okio.BufferedSink
import java.util.concurrent.TimeUnit

/** One entry of /api/files. */
data class Entry(val name: String, val isDirectory: Boolean, val size: Long = 0)

/** Called with bytes done and the total (-1 when the size isn't known). */
typealias Progress = (Long, Long) -> Unit

/** One row of /api/settings. */
data class Setting(
    val key: String,
    val name: String,
    val category: String,
    val type: String,
    val value: Int,
    val options: List<String>,
    val min: Int,
    val max: Int,
    val step: Int = 1,
)

/**
 * The device's web server (it runs while File Transfer is open). Plain HTTP on the local network;
 * the server refuses to overwrite a file, so uploads retry with "-2", "-3", ... on a name clash.
 */
class DeviceClient(host: String) {
    private val base: HttpUrl = "http://$host/".toHttpUrl()
    private val http = OkHttpClient.Builder()
        .connectTimeout(3, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .writeTimeout(60, TimeUnit.SECONDS)
        .build()

    private fun url(path: String, vararg query: Pair<String, String>): HttpUrl =
        base.newBuilder().addPathSegments(path).apply { query.forEach { (k, v) -> addQueryParameter(k, v) } }.build()

    private fun call(request: Request): Pair<Int, String> =
        http.newCall(request).execute().use { it.code to (it.body?.string() ?: "") }

    fun status(): DeviceStatus {
        val (code, body) = call(Request.Builder().url(url("api/status")).build())
        if (code != 200) throw IOException("HTTP $code")
        return ReaderApi.parseStatus(body)
    }

    /** Null when the firmware has no LiveSync status endpoint (older than 0.63.0). */
    fun liveSyncStatus(): LiveSyncStatus? {
        val (code, body) = call(Request.Builder().url(url("api/livesync/status")).build())
        if (code == 404) return null
        if (code != 200) throw IOException("HTTP $code")
        return ReaderApi.parseLiveSyncStatus(body)
    }

    /** Asks the device to sync now; only offered when `canSync` is true. */
    fun liveSyncNow() {
        val (code, text) = call(Request.Builder().url(url("api/livesync/sync")).post(ByteArray(0).toRequestBody()).build())
        if (code != 200) throw IOException(text.ifBlank { "HTTP $code" })
    }

    /**
     * Asks for one LiveSync sync at the reader's next wake (firmware 0.64.0+, `syncRequest`): a file the
     * reader looks for after File Transfer is left and deletes once it has run.
     */
    fun requestLiveSync() = putFile(SYNC_REQUEST_PATH, "sync\n".toByteArray(), "text/plain")

    /** Leaves text for the reader's next text field (its "From phone" button); replaces an older one. */
    fun sendPhoneText(text: String) = putFile(PHONE_TEXT_PATH, text.toByteArray(), "text/plain; charset=utf-8")

    /**
     * Writes `path` on the card with exactly this name, replacing a file already there (the server
     * never overwrites, so the old one is deleted first; a missing old file is fine).
     */
    fun putFile(path: String, bytes: ByteArray, mime: String) {
        val folder = path.substringBeforeLast('/').ifEmpty { "/" }
        val name = path.substringAfterLast('/')
        runCatching { delete(path) }
        val form = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("file", name, bytes.toRequestBody(mime.toMediaType())).build()
        val (code, text) = call(Request.Builder().url(url("upload", "path" to folder)).post(form).build())
        if (code != 200) throw Refused(text.ifBlank { "HTTP $code" })
    }

    /** The reader answered and said no (unlike a plain IOException, which means it couldn't be reached). */
    class Refused(message: String) : IOException(message)

    fun list(path: String): List<Entry> {
        val (code, body) = call(Request.Builder().url(url("api/files", "path" to path)).build())
        if (code != 200) throw IOException("HTTP $code")
        val array = JSONArray(body)
        return (0 until array.length()).map {
            val o = array.getJSONObject(it)
            Entry(o.optString("name"), o.optBoolean("isDirectory"), o.optLong("size"))
        }
    }

    /** Folders under /Vaults (the device's vaults). */
    fun vaults(): List<String> = list("/Vaults").filter { it.isDirectory }.map { it.name }.sorted()

    /** Makes every missing folder of `path` ("/Vaults/Notes/Inbox"). */
    fun ensureFolder(path: String) {
        var parent = ""
        for (part in path.trim('/').split('/').filter { it.isNotEmpty() }) {
            val form = FormBody.Builder().add("name", part).add("path", parent.ifEmpty { "/" }).build()
            call(Request.Builder().url(url("mkdir")).post(form).build())  // 400 = already there
            parent = "$parent/$part"
        }
    }

    /** Uploads into `folder`; returns the name it was saved under. */
    fun upload(folder: String, name: String, bytes: ByteArray, mime: String): String =
        uploadWith(folder, name) { bytes.toRequestBody(mime.toMediaType()) }

    fun upload(folder: String, name: String, file: File, mime: String): String =
        uploadWith(folder, name) { file.asRequestBody(mime.toMediaType()) }

    /** Uploads a stream of `length` bytes, reporting progress; returns the name it was saved under. */
    fun upload(folder: String, name: String, length: Long, open: () -> InputStream, progress: Progress): String =
        uploadWith(folder, name) { StreamBody(length, open, progress) }

    /** Copies a file from the card into `out`. */
    fun download(path: String, out: OutputStream, progress: Progress) {
        http.newCall(Request.Builder().url(url("download", "path" to path)).build()).execute().use { response ->
            if (response.code != 200) throw IOException(response.body?.string()?.ifBlank { null } ?: "HTTP ${response.code}")
            val body = response.body ?: throw IOException("empty reply")
            val total = body.contentLength()
            body.byteStream().use { input ->
                val buffer = ByteArray(16 * 1024)
                var done = 0L
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    out.write(buffer, 0, n)
                    done += n
                    progress(done, total)
                }
            }
        }
    }

    fun mkdir(parent: String, name: String) =
        post("mkdir", FormBody.Builder().add("name", name).add("path", parent).build())

    /** Files only: the device can't rename or move folders. */
    fun rename(path: String, name: String) =
        post("rename", FormBody.Builder().add("path", path).add("name", name).build())

    fun move(path: String, dest: String) =
        post("move", FormBody.Builder().add("path", path).add("dest", dest).build())

    /** A file, or an empty folder. */
    fun delete(path: String) = post("delete", FormBody.Builder().add("path", path).build())

    private fun post(path: String, form: FormBody) {
        val (code, text) = call(Request.Builder().url(url(path)).post(form).build())
        if (code != 200) throw IOException(text.ifBlank { "HTTP $code" })
    }

    private fun uploadWith(folder: String, name: String, body: () -> RequestBody): String {
        val dot = name.lastIndexOf('.').let { if (it > 0) it else name.length }
        for (n in 1..50) {
            val candidate = if (n == 1) name else name.substring(0, dot) + "-$n" + name.substring(dot)
            val form = MultipartBody.Builder().setType(MultipartBody.FORM)
                .addFormDataPart("file", candidate, body()).build()
            val (code, text) = call(Request.Builder().url(url("upload", "path" to folder)).post(form).build())
            if (code == 200) return candidate
            if (!text.contains("already exists")) throw IOException(text.ifBlank { "HTTP $code" })
        }
        throw IOException("too many notes named $name")
    }

    fun settings(): List<Setting> {
        val (code, body) = call(Request.Builder().url(url("api/settings")).build())
        if (code != 200) throw IOException("HTTP $code")
        val array = JSONArray(body)
        return (0 until array.length()).map {
            val o = array.getJSONObject(it)
            val opts = o.optJSONArray("options")
            Setting(
                key = o.optString("key"),
                name = o.optString("name"),
                category = o.optString("category"),
                type = o.optString("type"),
                value = o.optInt("value"),
                options = if (opts == null) emptyList() else (0 until opts.length()).map { i -> opts.getString(i) },
                min = o.optInt("min"),
                max = o.optInt("max"),
                step = o.optInt("step", 1),
            )
        }.filter { it.key.isNotEmpty() && it.type != "string" }
    }

    fun setSetting(key: String, value: Int) {
        val json = JSONObject().put(key, value).toString()
        val (code, text) = call(
            Request.Builder().url(url("api/settings"))
                .post(json.toRequestBody("application/json".toMediaType())).build(),
        )
        if (code != 200) throw IOException(text.ifBlank { "HTTP $code" })
    }

    // ---- Reader setup (0.6.0): Wi-Fi networks, LiveSync, OPDS, remote servers, fonts ----
    // Passwords go out in the request body and nowhere else: not logged, not kept.

    private fun getJson(path: String): String {
        val (code, body) = call(Request.Builder().url(url(path)).build())
        if (code != 200) throw Refused(ReaderApi.errorText(code, body))
        return body
    }

    private fun postJson(path: String, json: String): String {
        val (code, body) = call(
            Request.Builder().url(url(path)).post(json.toRequestBody("application/json".toMediaType())).build(),
        )
        if (code != 200) throw Refused(ReaderApi.errorText(code, body))
        return body
    }

    fun wifiNetworks(): List<WifiNetwork> = ReaderApi.parseWifi(getJson("api/wifi"))

    /** Adds a network, or with `index` updates one (an empty password then keeps the saved one). */
    fun saveWifi(ssid: String, password: String, index: Int? = null) {
        postJson("api/wifi", ReaderApi.wifiBody(ssid, password, index))
    }

    fun deleteWifi(index: Int) { postJson("api/wifi/delete", ReaderApi.indexBody(index)) }

    fun opdsServers(): List<OpdsServer> = ReaderApi.parseOpds(getJson("api/opds"))

    fun saveOpds(name: String, url: String, username: String, password: String, index: Int? = null) {
        postJson("api/opds", ReaderApi.opdsBody(name, url, username, password, index))
    }

    fun deleteOpds(index: Int) { postJson("api/opds/delete", ReaderApi.indexBody(index)) }

    /** Firmware 0.74.0+. */
    fun remotes(): List<RemoteServer> = ReaderApi.parseRemotes(getJson("api/remotes"))

    fun saveRemote(name: String, protocol: String, url: String, username: String, password: String,
                   verifyCert: Boolean, index: Int? = null) {
        postJson("api/remotes", ReaderApi.remoteBody(name, protocol, url, username, password, verifyCert, index))
    }

    fun deleteRemote(index: Int) { postJson("api/remotes/delete", ReaderApi.indexBody(index)) }

    /**
     * Leaves a LiveSync Setup URI on the reader; the passphrase is typed there, never here. Null
     * when the firmware (before 0.77.0) only answers `OK`.
     */
    fun liveSyncSetup(uri: String, vault: String, profile: Int?): LiveSyncSetupReply? =
        ReaderApi.parseLiveSyncSetupReply(postJson("api/livesync/setup", ReaderApi.liveSyncSetupBody(uri, vault, profile)))

    fun fonts(): FontList = ReaderApi.parseFonts(getJson("api/fonts"))

    /** Uploads one font file (.ttf, .otf or .cpfont) into `family`. */
    fun uploadFont(family: String, name: String, length: Long, open: () -> InputStream, progress: Progress) {
        val form = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("family", family)
            .addFormDataPart("file", name, StreamBody(length, open, progress)).build()
        val (code, body) = call(Request.Builder().url(url("api/fonts/upload")).post(form).build())
        if (code != 200) throw Refused(ReaderApi.errorText(code, body))
    }

    fun deleteFont(family: String) { postJson("api/fonts/delete", ReaderApi.familyBody(family)) }

    /** A request body read from a stream in chunks, so progress follows what has really gone out. */
    private class StreamBody(val length: Long, val open: () -> InputStream, val progress: Progress) : RequestBody() {
        override fun contentType() = "application/octet-stream".toMediaType()
        override fun contentLength() = length
        override fun writeTo(sink: BufferedSink) {
            open().use { input ->
                val buffer = ByteArray(16 * 1024)
                var done = 0L
                while (true) {
                    val n = input.read(buffer)
                    if (n < 0) break
                    sink.write(buffer, 0, n)
                    done += n
                    progress(done, length)
                }
            }
        }
    }

    companion object {
        /** The reader runs one LiveSync sync at the wake after this file appears (docs/webserver-endpoints.md). */
        const val SYNC_REQUEST_PATH = "/.vaultink/sync-request"
        /** Text for the reader's next text-entry screen, UTF-8, up to [PhoneText.MAX_BYTES]. */
        const val PHONE_TEXT_PATH = "/.vaultink/phone-text.txt"

        /** A name the SD card and Obsidian both accept. */
        fun safeName(title: String): String =
            title.replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), " ").trim().trimStart('.').take(100).trim()
    }
}
