package com.lefteros.vaultink

import org.json.JSONArray
import org.json.JSONObject

// The reader's JSON, as docs/webserver-endpoints.md describes it, read into plain values. Nothing
// here touches the network or Android, so it is all unit tested (ReaderApiTest). Only fields the
// firmware really sends are read; passwords never come back from the reader, only `hasPassword`.

/**
 * What /api/status says about the device. `device` is the board ("X3", "X4", or the board's name),
 * `rssi` the Wi-Fi signal in dBm (0 in hotspot mode), `uptime` seconds since boot, `freeHeap` bytes.
 */
data class DeviceStatus(
    val version: String,
    val ip: String,
    val hotspot: Boolean,
    val device: String = "",
    val rssi: Int = 0,
    val uptime: Long = 0,
    val freeHeap: Long = 0,
)

/** One profile of /api/livesync/status `profiles` (firmware 0.77.0+). */
data class LiveSyncProfile(
    val index: Int,
    val vault: String,
    val configured: Boolean,
    val pendingSetup: Boolean,
    val lastResult: String,
)

/**
 * What /api/livesync/status says (firmware 0.63.0+). `lastResult` is the LiveSync screen's own text;
 * `lastAttempt` is the epoch second of the last automatic try (0 when there was none); `canSync`
 * says whether the device would accept a sync request from here (it doesn't, so far); `syncRequest`
 * (firmware 0.64.0+) says a sync can be asked for with a file the reader picks up at its next wake.
 * `profiles` is empty on firmware older than 0.77.0.
 */
data class LiveSyncStatus(
    val configured: Boolean,
    val pendingSetup: Boolean,
    val unlocked: Boolean,
    val running: Boolean,
    val canSync: Boolean,
    val syncRequest: Boolean,
    val vault: String,
    val lastResult: String,
    val lastAttempt: Long,
    val autoHours: Int,
    val profiles: List<LiveSyncProfile> = emptyList(),
)

/** The answer to POST /api/livesync/setup (0.77.0+; older firmware answers the text `OK`). */
data class LiveSyncSetupReply(val profile: Int, val vault: String, val folderExists: Boolean)

/** One entry of GET /api/wifi. */
data class WifiNetwork(val index: Int, val ssid: String, val hasPassword: Boolean, val isLastConnected: Boolean)

/** One entry of GET /api/opds. */
data class OpdsServer(val index: Int, val name: String, val url: String, val username: String, val hasPassword: Boolean)

/** One entry of GET /api/remotes; `plain` means the connection isn't encrypted. */
data class RemoteServer(
    val index: Int,
    val name: String,
    val protocol: String,
    val url: String,
    val username: String,
    val hasPassword: Boolean,
    val verifyCert: Boolean,
    val plain: Boolean,
)

/** One family of GET /api/fonts. A size of 0 is a TTF/OTF, which has no fixed size. */
data class FontFamilyInfo(val name: String, val sizes: List<Int>, val files: Int, val bytes: Long) {
    val scalable: Boolean get() = sizes.contains(0)
}

data class FontList(val maxFamilies: Int, val families: List<FontFamilyInfo>)

object ReaderApi {
    fun parseStatus(body: String): DeviceStatus {
        val o = JSONObject(body)
        return DeviceStatus(
            version = o.optString("version"),
            ip = o.optString("ip"),
            hotspot = o.optString("mode") == "AP",
            device = o.optString("device"),
            rssi = o.optInt("rssi"),
            uptime = o.optLong("uptime"),
            freeHeap = o.optLong("freeHeap"),
        )
    }

    fun parseLiveSyncStatus(body: String): LiveSyncStatus {
        val o = JSONObject(body)
        val profiles = o.optJSONArray("profiles")
        return LiveSyncStatus(
            configured = o.optBoolean("configured"),
            pendingSetup = o.optBoolean("pendingSetup"),
            unlocked = o.optBoolean("unlocked"),
            running = o.optBoolean("running"),
            canSync = o.optBoolean("canSync"),
            syncRequest = o.optBoolean("syncRequest"),
            vault = o.optString("vault"),
            lastResult = o.optString("lastResult"),
            lastAttempt = o.optLong("lastAttempt"),
            autoHours = o.optInt("autoHours"),
            profiles = objects(profiles).map {
                LiveSyncProfile(it.optInt("index"), it.optString("vault"), it.optBoolean("configured"),
                    it.optBoolean("pendingSetup"), it.optString("lastResult"))
            },
        )
    }

    /** Null for the plain `OK` of firmware before profiles: saved, with nothing more to say. */
    fun parseLiveSyncSetupReply(body: String): LiveSyncSetupReply? {
        val o = runCatching { JSONObject(body) }.getOrNull() ?: return null
        if (!o.optBoolean("ok")) return null
        return LiveSyncSetupReply(o.optInt("profile"), o.optString("vault"), o.optBoolean("folderExists"))
    }

    fun parseWifi(body: String): List<WifiNetwork> = objects(JSONArray(body)).map {
        WifiNetwork(it.optInt("index"), it.optString("ssid"), it.optBoolean("hasPassword"), it.optBoolean("isLastConnected"))
    }

    fun parseOpds(body: String): List<OpdsServer> = objects(JSONArray(body)).map {
        OpdsServer(it.optInt("index"), it.optString("name"), it.optString("url"), it.optString("username"),
            it.optBoolean("hasPassword"))
    }

    fun parseRemotes(body: String): List<RemoteServer> = objects(JSONArray(body)).map {
        RemoteServer(it.optInt("index"), it.optString("name"), it.optString("protocol"), it.optString("url"),
            it.optString("username"), it.optBoolean("hasPassword"), it.optBoolean("verifyCert"), it.optBoolean("plain"))
    }

    fun parseFonts(body: String): FontList {
        val o = JSONObject(body)
        return FontList(o.optInt("maxFamilies"), objects(o.optJSONArray("families")).map { f ->
            val sizes = f.optJSONArray("sizes")
            val files = objects(f.optJSONArray("files"))
            FontFamilyInfo(f.optString("name"), if (sizes == null) emptyList() else (0 until sizes.length()).map { sizes.optInt(it) },
                files.size, files.sumOf { it.optLong("size") })
        })
    }

    /** The reader's own words when it says no: `{"error":"..."}`, a line of text, or just the status code. */
    fun errorText(code: Int, body: String): String {
        val json = runCatching { JSONObject(body).optString("error") }.getOrNull()
        return when {
            !json.isNullOrBlank() -> json
            body.isNotBlank() && body.length <= 200 && !body.trimStart().startsWith("<") -> body.trim()
            else -> "HTTP $code"
        }
    }

    // ---- request bodies ----
    // A password that is left empty is left out, which on an update keeps the one the reader has.

    fun wifiBody(ssid: String, password: String, index: Int? = null): String = JSONObject().apply {
        if (index != null) put("index", index)
        put("ssid", ssid.trim())
        if (password.isNotEmpty()) put("password", password)
    }.toString()

    fun opdsBody(name: String, url: String, username: String, password: String, index: Int? = null): String =
        JSONObject().apply {
            if (index != null) put("index", index)
            put("name", name.trim())
            put("url", url.trim())
            put("username", username.trim())
            if (password.isNotEmpty()) put("password", password)
        }.toString()

    fun remoteBody(name: String, protocol: String, url: String, username: String, password: String,
                   verifyCert: Boolean, index: Int? = null): String = JSONObject().apply {
        if (index != null) put("index", index)
        put("name", name.trim())
        put("protocol", protocol)
        put("url", url.trim())
        put("username", username.trim())
        if (password.isNotEmpty()) put("password", password)
        put("verifyCert", verifyCert)
    }.toString()

    /** `vault` empty and `profile` null are left out: the reader then picks the database's name and the next free profile. */
    fun liveSyncSetupBody(uri: String, vault: String, profile: Int?): String = JSONObject().apply {
        put("uri", uri.trim())
        if (vault.isNotEmpty()) put("vault", vault)
        if (profile != null) put("profile", profile)
    }.toString()

    fun indexBody(index: Int): String = JSONObject().put("index", index).toString()

    fun familyBody(family: String): String = JSONObject().put("family", family).toString()

    // ---- checks the reader makes too, done first so the message is clear ----

    /** A Setup URI, or its `%$...` part; 32 KB at most. */
    fun isSetupUri(uri: String): Boolean {
        val u = uri.trim()
        return u.toByteArray().size <= 32 * 1024 && (u.startsWith("obsidian://setuplivesync?settings=") || u.startsWith("%$"))
    }

    /**
     * A LiveSync folder under /Vaults, or empty for "the database's name": up to 64 bytes of letters
     * (any script), digits, spaces, `-`, `_`, `.`; no dot or space at either end.
     */
    fun isVaultFolder(name: String): Boolean {
        if (name.isEmpty()) return true
        if (name.toByteArray().size > 64) return false
        if (name.first() in " ." || name.last() in " .") return false
        return name.all { it.isLetterOrDigit() || it in " -_." }
    }

    /** A font family folder: ASCII letters and digits, `-` and `_`. */
    fun isFontFamily(name: String): Boolean =
        name.isNotEmpty() && name.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '-' || it == '_' }

    private val FONT_EXTENSIONS = setOf("ttf", "otf", "cpfont")

    /**
     * The name a picked font file is sent under, or null when it isn't a font the reader takes
     * (.ttf, .otf, .cpfont). The reader allows ASCII letters, digits, `-` and `_` before the
     * extension, so anything else becomes `_`.
     */
    fun fontFileName(picked: String): String? {
        val ext = picked.substringAfterLast('.', "").lowercase()
        if (ext !in FONT_EXTENSIONS) return null
        val base = picked.substringBeforeLast('.').map { if (isFontFamily(it.toString())) it else '_' }.joinToString("")
        return if (base.isEmpty()) null else "$base.$ext"
    }

    /** A family name suggested from a file name: "Literata-BoldItalic.ttf" and "Literata_12.cpfont" give "Literata". */
    fun fontFamilyFrom(picked: String): String {
        val base = fontFileName(picked)?.substringBeforeLast('.') ?: return ""
        return base.substringBefore('-').substringBefore('_').ifEmpty { base }
    }

    private fun objects(array: JSONArray?): List<JSONObject> =
        if (array == null) emptyList() else (0 until array.length()).mapNotNull { array.optJSONObject(it) }
}

/**
 * When to ask the reader for /api/status again. Never sooner than [intervalMs] after the last
 * try; a failed try doubles the wait, and after [maxFailures] failed tries in a row polling stops
 * until [retry] (the user tapping Try again). Times are passed in, so the phone's clock isn't read here.
 */
class PollPolicy(private val intervalMs: Long = MIN_INTERVAL_MS, private val maxFailures: Int = 2) {
    private var failures = 0
    private var lastTry: Long? = null

    val stopped: Boolean get() = failures >= maxFailures

    /** Milliseconds to wait from `now` before the next try; null when polling has stopped. */
    fun delayFrom(now: Long): Long? {
        if (stopped) return null
        val last = lastTry ?: return 0
        return (last + (intervalMs shl failures) - now).coerceAtLeast(0)
    }

    /** A try just ended at `now`. */
    fun record(now: Long, ok: Boolean) {
        lastTry = now
        failures = if (ok) 0 else failures + 1
    }

    /** The user asked again: start over, still no sooner than the interval after the last try. */
    fun retry() { failures = 0 }

    companion object {
        const val MIN_INTERVAL_MS = 10_000L
    }
}

/** The most a font of unknown size is read into memory before it is sent. */
const val FONT_MAX_BYTES = 8 * 1024 * 1024

/** Everything `input` holds, or null when that is more than `max` bytes. */
fun readCapped(input: java.io.InputStream, max: Int): ByteArray? {
    val out = java.io.ByteArrayOutputStream()
    val buffer = ByteArray(16 * 1024)
    while (true) {
        val n = input.read(buffer)
        if (n < 0) return out.toByteArray()
        if (out.size() + n > max) return null
        out.write(buffer, 0, n)
    }
}

/** "3 h 12 min", "12 min", "45 s": the reader's uptime. */
fun uptimeText(seconds: Long, day: String = "d", hour: String = "h", minute: String = "min", second: String = "s"): String {
    val s = seconds.coerceAtLeast(0)
    val d = s / 86_400
    val h = s % 86_400 / 3600
    val m = s % 3600 / 60
    return when {
        d > 0 -> "$d $day $h $hour"
        h > 0 -> "$h $hour $m $minute"
        m > 0 -> "$m $minute"
        else -> "$s $second"
    }
}

/** Wi-Fi signal in four steps (4 strongest), from dBm; 0 when there is no reading. */
fun signalBars(rssi: Int): Int = when {
    rssi >= 0 -> 0
    rssi >= -55 -> 4
    rssi >= -67 -> 3
    rssi >= -78 -> 2
    else -> 1
}
