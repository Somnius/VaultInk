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

/** What /api/status says about the device. */
data class DeviceStatus(val version: String, val ip: String, val hotspot: Boolean)

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
        val json = JSONObject(body)
        return DeviceStatus(json.optString("version"), json.optString("ip"), json.optString("mode") == "AP")
    }

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
        /** A name the SD card and Obsidian both accept. */
        fun safeName(title: String): String =
            title.replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), " ").trim().trimStart('.').take(100).trim()
    }
}
