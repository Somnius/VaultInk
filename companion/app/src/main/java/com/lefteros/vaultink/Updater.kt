package com.lefteros.vaultink

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.File
import java.io.IOException

/** A newer companion release on GitHub: its version and APK download URL. */
data class AppUpdate(val version: String, val apkUrl: String)

/**
 * The companion's releases are tagged "companion-x.y.z" in the firmware's repository and are never
 * marked Latest (the device reads the latest release for firmware), so the whole list is read.
 */
object Updater {
    private const val RELEASES = "https://api.github.com/repos/Somnius/VaultInk/releases?per_page=50"
    private val http = OkHttpClient()

    fun newer(current: String): AppUpdate? {
        val body = http.newCall(Request.Builder().url(RELEASES).header("Accept", "application/vnd.github+json").build())
            .execute().use { if (!it.isSuccessful) throw IOException("HTTP ${it.code}"); it.body!!.string() }
        val releases = JSONArray(body)
        var best: AppUpdate? = null
        for (i in 0 until releases.length()) {
            val r = releases.getJSONObject(i)
            val tag = r.optString("tag_name")
            if (!tag.startsWith("companion-") || r.optBoolean("draft") || r.optBoolean("prerelease")) continue
            val version = tag.removePrefix("companion-")
            if (compare(version, current) <= 0 || (best != null && compare(version, best.version) <= 0)) continue
            val assets = r.optJSONArray("assets") ?: continue
            for (a in 0 until assets.length()) {
                val asset = assets.getJSONObject(a)
                if (asset.optString("name") == "vaultink-companion-$version.apk") {
                    best = AppUpdate(version, asset.optString("browser_download_url"))
                }
            }
        }
        return best
    }

    /** Downloads the APK and hands it to the system installer (the same signing key is needed). */
    fun install(context: Context, update: AppUpdate) {
        val dir = File(context.cacheDir, "updates").apply { mkdirs() }
        val file = File(dir, "vaultink-companion-${update.version}.apk")
        http.newCall(Request.Builder().url(update.apkUrl).build()).execute().use { response ->
            if (!response.isSuccessful) throw IOException("HTTP ${response.code}")
            file.outputStream().use { out -> response.body!!.byteStream().copyTo(out) }
        }
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.files", file)
        context.startActivity(
            Intent(Intent.ACTION_VIEW).setDataAndType(uri, "application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    /** "0.2.0" vs "0.10.1": numbers compared part by part. */
    fun compare(a: String, b: String): Int {
        val x = a.split('.').map { it.toIntOrNull() ?: 0 }
        val y = b.split('.').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(x.size, y.size)) {
            val d = (x.getOrElse(i) { 0 }).compareTo(y.getOrElse(i) { 0 })
            if (d != 0) return d
        }
        return 0
    }
}
