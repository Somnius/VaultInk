package com.lefteros.vaultink

import org.json.JSONArray
import org.json.JSONObject

// Settings backup (0.6.0): one JSON file with the reader's settings and its lists of Wi-Fi
// networks, OPDS catalogues and remote servers, and the plan for putting one back. Pure functions,
// unit tested (BackupTest).
//
// No secret can get into the file, because it is built from a fixed list of fields and nothing
// else is copied: of the settings only switches, choices and numbers (text settings can hold a
// password or a key, so none is taken), of a network only its name, of a catalogue its name,
// address and user name, of a server those plus the protocol and the certificate switch. A
// `user:password@` inside an address is cut out. Passwords, passphrases, Setup URIs and tokens are
// never read into it, so a restore adds entries without a password.

/** A catalogue as the file keeps it. */
data class BackupOpds(val name: String, val url: String, val username: String)

/** A remote server as the file keeps it. */
data class BackupRemote(val name: String, val protocol: String, val url: String, val username: String, val verifyCert: Boolean)

/**
 * A backup file. `settings` is key to value; `options` has, for a choice, the text of the chosen
 * option, because the reader's own number for it can mean something else on another card (fonts,
 * dictionaries). A list is null when the reader it came from had no such list.
 */
data class BackupFile(
    val formatVersion: Int,
    val appVersion: String,
    val readerVersion: String,
    val readerDevice: String,
    val date: String,
    val settings: Map<String, Int>,
    val options: Map<String, String>,
    val wifi: List<String>?,
    val opds: List<BackupOpds>?,
    val remotes: List<BackupRemote>?,
)

/** What a restore will do, worked out before anything is sent. */
data class RestorePlan(
    /** Key to the value to post: known to the reader, in range and different from what it has. */
    val settings: Map<String, Int>,
    val settingsSame: Int,
    /** Keys left alone: not known to this reader, out of its range, or a choice it doesn't offer. */
    val settingsSkipped: List<String>,
    val wifi: List<String>,
    val opds: List<BackupOpds>,
    val remotes: List<BackupRemote>,
    /** Networks, catalogues and servers the reader already has. */
    val listsPresent: Int,
)

class BackupError(val reason: Reason) : Exception(reason.name) {
    enum class Reason { NOT_A_BACKUP, NEWER_VERSION }
}

object Backup {
    const val FORMAT = "vaultink-settings"
    const val FORMAT_VERSION = 1
    /** A backup is a few kilobytes; a file over this is not one. */
    const val MAX_BYTES = 1024 * 1024

    fun fileName(date: String): String = "vaultink-settings-${date.take(10)}.json"

    /**
     * An address without a `user:password@` part, whether it starts with a scheme or not: everything
     * up to the last `@` of the host part (before the first `/`, `?` or `#`) goes.
     */
    fun stripUserInfo(url: String): String =
        url.trim().replace(Regex("^((?:[A-Za-z][A-Za-z0-9+.-]*://)?)[^/?#]*@"), "$1")

    /**
     * Builds the file's text from the reader's own answers (the bodies of GET /api/settings,
     * /api/wifi, /api/opds and /api/remotes; null for a list the reader doesn't have). `date` is
     * an ISO date and time.
     */
    fun build(appVersion: String, readerVersion: String, readerDevice: String, date: String, settingsJson: String,
              wifiJson: String?, opdsJson: String?, remotesJson: String?): String {
        val settings = JSONObject()
        val options = JSONObject()
        for (o in objects(JSONArray(settingsJson))) {
            val key = o.optString("key")
            val type = o.optString("type")
            if (key.isEmpty() || type !in VALUE_TYPES || o.opt("value") !is Int) continue
            val value = o.getInt("value")
            settings.put(key, value)
            if (type == "enum") o.optJSONArray("options")?.optString(value, "")?.takeIf { it.isNotEmpty() }?.let { options.put(key, it) }
        }
        val out = JSONObject()
            .put("format", FORMAT).put("formatVersion", FORMAT_VERSION).put("appVersion", appVersion)
            .put("reader", JSONObject().put("version", readerVersion).put("device", readerDevice))
            .put("date", date).put("settings", settings).put("options", options)
        if (wifiJson != null) {
            out.put("wifi", JSONArray(objects(JSONArray(wifiJson)).map { it.optString("ssid") }.filter { it.isNotEmpty() }
                .map { JSONObject().put("ssid", it) }))
        }
        if (opdsJson != null) {
            out.put("opds", JSONArray(objects(JSONArray(opdsJson)).filter { it.optString("url").isNotEmpty() }.map {
                JSONObject().put("name", it.optString("name")).put("url", stripUserInfo(it.optString("url")))
                    .put("username", it.optString("username"))
            }))
        }
        if (remotesJson != null) {
            out.put("remotes", JSONArray(objects(JSONArray(remotesJson)).filter { it.optString("url").isNotEmpty() }.map {
                JSONObject().put("name", it.optString("name")).put("protocol", it.optString("protocol"))
                    .put("url", stripUserInfo(it.optString("url"))).put("username", it.optString("username"))
                    .put("verifyCert", it.optBoolean("verifyCert", true))
            }))
        }
        return out.toString(2)
    }

    /** Reads a backup file; [BackupError] when it isn't one or comes from a newer app. Unknown keys are ignored. */
    fun parse(text: String): BackupFile {
        val o = runCatching { JSONObject(text) }.getOrNull() ?: throw BackupError(BackupError.Reason.NOT_A_BACKUP)
        val version = o.optInt("formatVersion", 0)
        val settings = o.optJSONObject("settings")
        if (o.optString("format") != FORMAT || version < 1 || settings == null) throw BackupError(BackupError.Reason.NOT_A_BACKUP)
        if (version > FORMAT_VERSION) throw BackupError(BackupError.Reason.NEWER_VERSION)
        val reader = o.optJSONObject("reader")
        val options = o.optJSONObject("options")
        return BackupFile(
            formatVersion = version,
            appVersion = o.optString("appVersion"),
            readerVersion = reader?.optString("version").orEmpty(),
            readerDevice = reader?.optString("device").orEmpty(),
            date = o.optString("date"),
            settings = settings.keys().asSequence().filter { settings.opt(it) is Int }.associateWith { settings.getInt(it) },
            options = options?.keys()?.asSequence()?.filter { options.opt(it) is String }?.associateWith { options.getString(it) }
                ?: emptyMap(),
            wifi = o.optJSONArray("wifi")?.let { a -> objects(a).map { it.optString("ssid") }.filter { it.isNotEmpty() } },
            opds = o.optJSONArray("opds")?.let { a ->
                objects(a).filter { it.optString("url").isNotEmpty() }
                    .map { BackupOpds(it.optString("name"), stripUserInfo(it.optString("url")), it.optString("username")) }
            },
            remotes = o.optJSONArray("remotes")?.let { a ->
                objects(a).filter { it.optString("url").isNotEmpty() && it.optString("protocol") in PROTOCOLS }.map {
                    BackupRemote(it.optString("name"), it.optString("protocol"), stripUserInfo(it.optString("url")),
                        it.optString("username"), it.optBoolean("verifyCert", true))
                }
            },
        )
    }

    /**
     * What to send for `file`, given what the reader has now (a fresh GET of each; a list is null
     * when this reader doesn't have it, and then nothing of that kind is added).
     */
    fun plan(file: BackupFile, current: List<Setting>, wifi: List<WifiNetwork>?, opds: List<OpdsServer>?,
             remotes: List<RemoteServer>?): RestorePlan {
        val known = current.associateBy { it.key }
        val apply = LinkedHashMap<String, Int>()
        val skipped = mutableListOf<String>()
        var same = 0
        for ((key, saved) in file.settings) {
            val now = known[key]
            if (now == null) {
                skipped += key
                continue
            }
            val value = when (now.type) {
                "toggle" -> saved.takeIf { it == 0 || it == 1 }
                "value" -> saved.takeIf { it in now.min..now.max }
                // By the option's text when the file has it: the same number can be another font on this card.
                "enum" -> file.options[key]?.let { text -> now.options.indexOf(text).takeIf { it >= 0 } }
                    ?: saved.takeIf { key !in file.options && it in now.options.indices }
                else -> null
            }
            when {
                value == null -> skipped += key
                value == now.value -> same++
                else -> apply[key] = value
            }
        }
        var present = 0
        fun <T> missing(saved: List<T>?, has: ((T) -> Boolean)?): List<T> {
            if (saved == null || has == null) return emptyList()
            val (there, absent) = saved.distinct().partition(has)
            present += there.size
            return absent
        }
        return RestorePlan(
            settings = apply,
            settingsSame = same,
            settingsSkipped = skipped,
            wifi = missing(file.wifi, wifi?.let { list -> { ssid: String -> list.any { it.ssid == ssid } } }),
            opds = missing(file.opds, opds?.let { list -> { s: BackupOpds -> list.any { stripUserInfo(it.url) == s.url } } }),
            remotes = missing(file.remotes, remotes?.let { list ->
                { s: BackupRemote -> list.any { it.protocol == s.protocol && stripUserInfo(it.url) == s.url && it.username == s.username } }
            }),
            listsPresent = present,
        )
    }

    private val VALUE_TYPES = setOf("toggle", "enum", "value")
    private val PROTOCOLS = setOf("webdav", "ftp")

    private fun objects(array: JSONArray): List<JSONObject> = (0 until array.length()).mapNotNull { array.optJSONObject(it) }
}
