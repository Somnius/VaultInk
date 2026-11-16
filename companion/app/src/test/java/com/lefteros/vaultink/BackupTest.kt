package com.lefteros.vaultink

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/** The settings backup file: what goes in, what never does, and what a restore sends. */
class BackupTest {
    // The reader's answers, with secrets added that it doesn't send today: none may reach the file.
    private val settingsJson = """[
        {"key":"fontSize","name":"Reader Font Size","category":"Reader","type":"enum","value":1,"options":["12 pt","14 pt","16 pt","18 pt"]},
        {"key":"showHiddenFiles","name":"Show hidden files","category":"Files","type":"toggle","value":1},
        {"key":"lineSpacing","name":"Line spacing","category":"Reader","type":"value","value":110,"min":80,"max":200,"step":5},
        {"key":"koreaderPassword","name":"KOReader password","category":"Sync","type":"string","value":"hunter2"},
        {"key":"opdsToken","name":"Token","category":"Sync","type":"string","value":"tok-123"},
        {"name":"no key","type":"toggle","value":1}]"""
    private val wifiJson = """[{"index":0,"ssid":"HomeWiFi","hasPassword":true,"isLastConnected":true,"password":"wifi-secret"}]"""
    private val opdsJson = """[{"index":0,"name":"My Catalog","url":"http://reader:opds-secret@calibre.local:8080/opds","username":"reader",
        "hasPassword":true,"password":"opds-secret","headers":[{"name":"Authorization","value":"Bearer header-secret"}]}]"""
    private val remotesJson = """[{"index":0,"name":"Nextcloud","protocol":"webdav","url":"https://cloud.example.org/dav","username":"you",
        "hasPassword":true,"verifyCert":false,"plain":false,"password":"dav-secret","token":"dav-token"}]"""

    private fun build(wifi: String? = wifiJson, opds: String? = opdsJson, remotes: String? = remotesJson) =
        Backup.build("0.6.0", "0.77.1", "xteink_x4_pro", "2026-11-16T15:30:00+02:00", settingsJson, wifi, opds, remotes)

    @Test
    fun theFileHasItsHeaderAndTheValues() {
        val o = JSONObject(build())
        assertEquals("vaultink-settings", o.getString("format"))
        assertEquals(1, o.getInt("formatVersion"))
        assertEquals("0.6.0", o.getString("appVersion"))
        assertEquals("0.77.1", o.getJSONObject("reader").getString("version"))
        assertEquals("xteink_x4_pro", o.getJSONObject("reader").getString("device"))
        assertEquals("2026-11-16T15:30:00+02:00", o.getString("date"))
        assertEquals(setOf("fontSize", "showHiddenFiles", "lineSpacing"), o.getJSONObject("settings").keys().asSequence().toSet())
        assertEquals(110, o.getJSONObject("settings").getInt("lineSpacing"))
        assertEquals("14 pt", o.getJSONObject("options").getString("fontSize"))
        assertEquals("vaultink-settings-2026-11-16.json", Backup.fileName("2026-11-16T15:30:00+02:00"))
    }

    @Test
    fun noSecretReachesTheFile() {
        val text = build()
        for (secret in listOf("hunter2", "tok-123", "wifi-secret", "opds-secret", "header-secret", "dav-secret", "dav-token",
            "password", "Password", "hasPassword", "headers", "token")) {
            assertFalse("$secret is in the file", text.contains(secret))
        }
        val o = JSONObject(text)
        assertEquals(setOf("ssid"), o.getJSONArray("wifi").getJSONObject(0).keys().asSequence().toSet())
        assertEquals(setOf("name", "url", "username"), o.getJSONArray("opds").getJSONObject(0).keys().asSequence().toSet())
        assertEquals("http://calibre.local:8080/opds", o.getJSONArray("opds").getJSONObject(0).getString("url"))
        assertEquals(setOf("name", "protocol", "url", "username", "verifyCert"),
            o.getJSONArray("remotes").getJSONObject(0).keys().asSequence().toSet())
    }

    @Test
    fun userInfoIsCutFromAddresses() {
        assertEquals("https://example.org/dav", Backup.stripUserInfo("https://me:pw@example.org/dav"))
        assertEquals("ftp://example.org", Backup.stripUserInfo(" ftp://me@example.org "))
        assertEquals("https://example.org/a@b", Backup.stripUserInfo("https://example.org/a@b"))
        assertEquals("example.org", Backup.stripUserInfo("example.org"))
    }

    @Test
    fun aListTheReaderDoesNotHaveIsLeftOut() {
        val o = JSONObject(build(remotes = null))
        assertFalse(o.has("remotes"))
        assertTrue(o.has("wifi"))
        val file = Backup.parse(o.toString())
        assertNull(file.remotes)
        assertEquals(listOf("HomeWiFi"), file.wifi)
    }

    @Test
    fun whatWasBuiltReadsBack() {
        val file = Backup.parse(build())
        assertEquals(1, file.formatVersion)
        assertEquals("0.77.1", file.readerVersion)
        assertEquals("xteink_x4_pro", file.readerDevice)
        assertEquals(mapOf("fontSize" to 1, "showHiddenFiles" to 1, "lineSpacing" to 110), file.settings)
        assertEquals(mapOf("fontSize" to "14 pt"), file.options)
        assertEquals(listOf(BackupOpds("My Catalog", "http://calibre.local:8080/opds", "reader")), file.opds)
        assertEquals(listOf(BackupRemote("Nextcloud", "webdav", "https://cloud.example.org/dav", "you", false)), file.remotes)
    }

    private fun reason(text: String): BackupError.Reason? = try {
        Backup.parse(text)
        null
    } catch (e: BackupError) {
        e.reason
    }

    @Test
    fun filesThatAreNotBackupsAreRefused() {
        assertEquals(BackupError.Reason.NOT_A_BACKUP, reason("not json"))
        assertEquals(BackupError.Reason.NOT_A_BACKUP, reason("[]"))
        assertEquals(BackupError.Reason.NOT_A_BACKUP, reason("""{"format":"something-else","formatVersion":1,"settings":{}}"""))
        assertEquals(BackupError.Reason.NOT_A_BACKUP, reason("""{"format":"vaultink-settings","formatVersion":1}"""))
        assertEquals(BackupError.Reason.NOT_A_BACKUP, reason("""{"format":"vaultink-settings","settings":{}}"""))
        assertEquals(BackupError.Reason.NEWER_VERSION, reason("""{"format":"vaultink-settings","formatVersion":2,"settings":{}}"""))
        assertNull(reason("""{"format":"vaultink-settings","formatVersion":1,"settings":{}}"""))
    }

    @Test
    fun aHandEditedFileCannotBringSecretsOrOddValuesIn() {
        val file = Backup.parse("""{"format":"vaultink-settings","formatVersion":1,
            "settings":{"fontSize":2,"name":"text","flag":true,"half":1.5},
            "opds":[{"name":"A","url":"http://u:p@host/opds","username":"u","password":"x"},{"name":"no url"}],
            "remotes":[{"name":"B","protocol":"smb","url":"smb://host"},{"name":"C","protocol":"ftp","url":"ftp://host","username":"c"}],
            "wifi":[{"ssid":"Net","password":"x"},{"ssid":""},"junk"]}""")
        assertEquals(mapOf("fontSize" to 2), file.settings)
        assertEquals(listOf(BackupOpds("A", "http://host/opds", "u")), file.opds)
        assertEquals(listOf(BackupRemote("C", "ftp", "ftp://host", "c", true)), file.remotes)
        assertEquals(listOf("Net"), file.wifi)
    }

    private val current = listOf(
        Setting("fontSize", "Reader Font Size", "Reader", "enum", 0, listOf("10 pt", "12 pt", "14 pt"), 0, 0),
        Setting("showHiddenFiles", "Show hidden files", "Files", "toggle", 1, emptyList(), 0, 0),
        Setting("lineSpacing", "Line spacing", "Reader", "value", 100, emptyList(), 80, 105, 5),
        Setting("sleepScreen", "Sleep screen", "Display", "enum", 0, listOf("Dark", "Light"), 0, 0),
    )

    private fun file(settings: Map<String, Int>, options: Map<String, String> = emptyMap(), wifi: List<String>? = null,
                     opds: List<BackupOpds>? = null, remotes: List<BackupRemote>? = null) =
        BackupFile(1, "0.6.0", "0.77.1", "X4", "2026-11-16", settings, options, wifi, opds, remotes)

    @Test
    fun onlyKeysTheReaderKnowsAreSentAndOnlyWhenTheyDiffer() {
        val plan = Backup.plan(file(mapOf("showHiddenFiles" to 1, "sleepScreen" to 1, "goneInThisFirmware" to 3)), current, null, null, null)
        assertEquals(mapOf("sleepScreen" to 1), plan.settings)
        assertEquals(1, plan.settingsSame)
        assertEquals(listOf("goneInThisFirmware"), plan.settingsSkipped)
    }

    @Test
    fun aChoiceIsRestoredByItsTextNotItsNumber() {
        // "14 pt" was number 1 on the old card and is number 2 here.
        val plan = Backup.plan(file(mapOf("fontSize" to 1), mapOf("fontSize" to "14 pt")), current, null, null, null)
        assertEquals(mapOf("fontSize" to 2), plan.settings)
        // A choice this reader doesn't offer is skipped, not sent as whatever has that number now.
        val gone = Backup.plan(file(mapOf("fontSize" to 1), mapOf("fontSize" to "16 pt")), current, null, null, null)
        assertTrue(gone.settings.isEmpty())
        assertEquals(listOf("fontSize"), gone.settingsSkipped)
        // Without a text the number is used when the reader has it.
        assertEquals(mapOf("sleepScreen" to 1), Backup.plan(file(mapOf("sleepScreen" to 1)), current, null, null, null).settings)
        assertEquals(listOf("sleepScreen"), Backup.plan(file(mapOf("sleepScreen" to 7)), current, null, null, null).settingsSkipped)
    }

    @Test
    fun valuesOutOfRangeAreSkipped() {
        val plan = Backup.plan(file(mapOf("lineSpacing" to 110, "showHiddenFiles" to 5)), current, null, null, null)
        assertTrue(plan.settings.isEmpty())
        assertEquals(listOf("lineSpacing", "showHiddenFiles"), plan.settingsSkipped)
        assertEquals(mapOf("lineSpacing" to 85), Backup.plan(file(mapOf("lineSpacing" to 85)), current, null, null, null).settings)
    }

    @Test
    fun onlyMissingEntriesAreAdded() {
        val saved = file(emptyMap(),
            wifi = listOf("HomeWiFi", "Cabin", "Cabin"),
            opds = listOf(BackupOpds("My Catalog", "http://calibre.local:8080/opds", "reader"), BackupOpds("Other", "http://other/opds", "")),
            remotes = listOf(BackupRemote("Nextcloud", "webdav", "https://cloud.example.org/dav", "you", true),
                BackupRemote("Nas", "ftp", "ftp://nas.local", "me", true)))
        val plan = Backup.plan(saved, current,
            wifi = listOf(WifiNetwork(0, "HomeWiFi", true, true)),
            opds = listOf(OpdsServer(0, "Renamed here", "http://calibre.local:8080/opds", "reader", true)),
            remotes = listOf(RemoteServer(0, "Nextcloud", "webdav", "https://cloud.example.org/dav", "you", true, true, false)))
        assertEquals(listOf("Cabin"), plan.wifi)
        assertEquals(listOf(BackupOpds("Other", "http://other/opds", "")), plan.opds)
        assertEquals(listOf(BackupRemote("Nas", "ftp", "ftp://nas.local", "me", true)), plan.remotes)
        assertEquals(3, plan.listsPresent)
    }

    @Test
    fun aListThisReaderDoesNotHaveGetsNothing() {
        val saved = file(emptyMap(), wifi = listOf("Cabin"), remotes = listOf(BackupRemote("Nas", "ftp", "ftp://nas.local", "me", true)))
        val plan = Backup.plan(saved, current, wifi = emptyList(), opds = emptyList(), remotes = null)
        assertEquals(listOf("Cabin"), plan.wifi)
        assertTrue(plan.remotes.isEmpty())
        assertEquals(0, plan.listsPresent)
    }

    @Test
    fun parseNeverThrowsAnythingButBackupError() {
        try {
            Backup.parse("""{"format":"vaultink-settings","formatVersion":1,"settings":{},"wifi":"x","opds":{},"remotes":7,"reader":"y","options":[]}""")
        } catch (e: BackupError) {
            fail("a file with odd extra values is still a backup")
        }
    }
}
