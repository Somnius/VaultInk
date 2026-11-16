package com.lefteros.vaultink

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

private fun JSONObject.fieldNames(): Set<String> = keys().asSequence().toSet()

/** The JSON of docs/webserver-endpoints.md, read and written by ReaderApi. */
class ReaderApiTest {
    @Test
    fun statusAsDocumented() {
        val s = ReaderApi.parseStatus(
            """{"version":"0.77.1","firmwareFamily":"crosspoint","pluginApi":1,"pluginCapabilities":"catalog.lists",
               "ip":"192.168.1.100","mode":"STA","rssi":-45,"freeHeap":123456,"uptime":3600,"device":"X4","serial":"Not found"}""",
        )
        assertEquals(DeviceStatus("0.77.1", "192.168.1.100", hotspot = false, device = "X4", rssi = -45, uptime = 3600,
            freeHeap = 123456), s)
    }

    @Test
    fun statusInHotspotModeAndWithMissingFields() {
        val ap = ReaderApi.parseStatus("""{"version":"0.29.0","ip":"192.168.4.1","mode":"AP","rssi":0}""")
        assertTrue(ap.hotspot)
        assertEquals(0, ap.rssi)
        assertEquals("", ap.device)
        assertEquals(0L, ap.uptime)
    }

    @Test(expected = org.json.JSONException::class)
    fun statusThatIsNotJsonThrows() {
        ReaderApi.parseStatus("<html>not the reader</html>")
    }

    @Test
    fun liveSyncStatusWithProfiles() {
        val s = ReaderApi.parseLiveSyncStatus(
            """{"configured":true,"pendingSetup":false,"unlocked":true,"running":false,"canSync":false,"syncRequest":true,
               "vault":"Constantinople By Night","lastResult":"Synced: 12 updated, 0 removed. Sent 1 notes from here",
               "lastAttempt":1762000000,"autoHours":6,"canPush":true,"profile":1,"maxProfiles":4,
               "profiles":[
                 {"index":1,"vault":"Constantinople By Night","configured":true,"pendingSetup":false,"unlocked":true,
                  "lastResult":"Synced: 12 updated, 0 removed. Sent 1 notes from here","lastAttempt":1762000000,"autoHours":6,"canPush":true},
                 {"index":2,"vault":"Lefteris-RPG","configured":false,"pendingSetup":true,"unlocked":false}]}""",
        )
        assertTrue(s.configured)
        assertTrue(s.syncRequest)
        assertFalse(s.canSync)
        assertEquals("Constantinople By Night", s.vault)
        assertEquals(1762000000L, s.lastAttempt)
        assertEquals(6, s.autoHours)
        assertEquals(2, s.profiles.size)
        assertEquals(LiveSyncProfile(2, "Lefteris-RPG", configured = false, pendingSetup = true, lastResult = ""), s.profiles[1])
    }

    @Test
    fun liveSyncStatusBeforeProfiles() {
        val s = ReaderApi.parseLiveSyncStatus("""{"configured":false,"pendingSetup":true,"unlocked":false,"running":false,"canSync":false}""")
        assertFalse(s.configured)
        assertTrue(s.pendingSetup)
        assertFalse(s.syncRequest)
        assertTrue(s.profiles.isEmpty())
    }

    @Test
    fun liveSyncSetupReply() {
        assertEquals(LiveSyncSetupReply(2, "Constantinople By Night", folderExists = false),
            ReaderApi.parseLiveSyncSetupReply("""{"ok":true,"profile":2,"vault":"Constantinople By Night","folderExists":false}"""))
        assertTrue(ReaderApi.parseLiveSyncSetupReply("""{"ok":true,"profile":1,"vault":"Notes","folderExists":true}""")!!.folderExists)
        // Firmware before profiles answers the text OK.
        assertNull(ReaderApi.parseLiveSyncSetupReply("OK"))
    }

    @Test
    fun wifiList() {
        val list = ReaderApi.parseWifi("""[{"index":0,"ssid":"HomeWiFi","hasPassword":true,"isLastConnected":true},
            {"index":1,"ssid":"Καφέ","hasPassword":false,"isLastConnected":false}]""")
        assertEquals(listOf(WifiNetwork(0, "HomeWiFi", true, true), WifiNetwork(1, "Καφέ", false, false)), list)
        assertTrue(ReaderApi.parseWifi("[]").isEmpty())
    }

    @Test
    fun opdsList() {
        assertEquals(listOf(OpdsServer(0, "My Catalog", "http://calibre.local:8080/opds", "reader", true)),
            ReaderApi.parseOpds("""[{"index":0,"name":"My Catalog","url":"http://calibre.local:8080/opds","username":"reader","hasPassword":true}]"""))
    }

    @Test
    fun remotesList() {
        val r = ReaderApi.parseRemotes("""[{"index":0,"name":"Nextcloud","protocol":"webdav",
            "url":"https://cloud.example.org/remote.php/dav/files/you","username":"you","hasPassword":true,"verifyCert":true,"plain":false}]""")
        assertEquals(listOf(RemoteServer(0, "Nextcloud", "webdav", "https://cloud.example.org/remote.php/dav/files/you", "you",
            hasPassword = true, verifyCert = true, plain = false)), r)
    }

    @Test
    fun fontList() {
        val fonts = ReaderApi.parseFonts("""{"maxFamilies":128,"families":[
            {"name":"Literata","sizes":[12,14,16,18],"files":[{"name":"Literata_12.cpfont","size":123456},{"name":"Literata_14.cpfont","size":1000}]},
            {"name":"FiraSans","sizes":[0],"files":[{"name":"FiraSans-Regular.ttf","size":500000}]}]}""")
        assertEquals(128, fonts.maxFamilies)
        assertEquals(FontFamilyInfo("Literata", listOf(12, 14, 16, 18), 2, 124456), fonts.families[0])
        assertFalse(fonts.families[0].scalable)
        assertTrue(fonts.families[1].scalable)
        assertTrue(ReaderApi.parseFonts("""{"maxFamilies":128,"families":[]}""").families.isEmpty())
    }

    @Test
    fun errorTextUsesTheReadersWords() {
        assertEquals("Invalid font file", ReaderApi.errorText(400, """{"error":"Invalid font file"}"""))
        assertEquals("All four profiles are in use", ReaderApi.errorText(409, "All four profiles are in use\n"))
        assertEquals("HTTP 404", ReaderApi.errorText(404, ""))
        assertEquals("HTTP 500", ReaderApi.errorText(500, "<html><body>oops</body></html>"))
    }

    @Test
    fun aNewEntryCarriesItsPasswordAndNoIndex() {
        val o = JSONObject(ReaderApi.wifiBody(" HomeWiFi ", "secret"))
        assertEquals("HomeWiFi", o.getString("ssid"))
        assertEquals("secret", o.getString("password"))
        assertFalse(o.has("index"))
    }

    @Test
    fun anUpdateWithAnEmptyPasswordLeavesItOutSoTheReaderKeepsItsOwn() {
        val wifi = JSONObject(ReaderApi.wifiBody("HomeWiFi", "", index = 0))
        assertEquals(0, wifi.getInt("index"))
        assertFalse(wifi.has("password"))
        val opds = JSONObject(ReaderApi.opdsBody("My Catalog", "http://calibre.local:8080/opds", "reader", "", index = 2))
        assertEquals(2, opds.getInt("index"))
        assertFalse(opds.has("password"))
        assertEquals(setOf("index", "name", "url", "username"), opds.fieldNames())
    }

    @Test
    fun remoteBodyHasTheDocumentedFields() {
        val o = JSONObject(ReaderApi.remoteBody("Nextcloud", "webdav", "https://cloud.example.org/dav", "you", "app-password", true))
        assertEquals(setOf("name", "protocol", "url", "username", "password", "verifyCert"), o.fieldNames())
        assertEquals("webdav", o.getString("protocol"))
        assertTrue(o.getBoolean("verifyCert"))
    }

    @Test
    fun liveSyncSetupBodyLeavesOutWhatIsNotChosen() {
        val bare = JSONObject(ReaderApi.liveSyncSetupBody("obsidian://setuplivesync?settings=abc", "", null))
        assertEquals(setOf("uri"), bare.fieldNames())
        val full = JSONObject(ReaderApi.liveSyncSetupBody("obsidian://setuplivesync?settings=abc", "Constantinople By Night", 2))
        assertEquals("Constantinople By Night", full.getString("vault"))
        assertEquals(2, full.getInt("profile"))
        // Never a passphrase: the reader's endpoint has no such field.
        assertEquals(setOf("uri", "vault", "profile"), full.fieldNames())
    }

    @Test
    fun deleteBodies() {
        assertEquals(3, JSONObject(ReaderApi.indexBody(3)).getInt("index"))
        assertEquals("Literata", JSONObject(ReaderApi.familyBody("Literata")).getString("family"))
    }

    @Test
    fun setupUriCheck() {
        assertTrue(ReaderApi.isSetupUri("obsidian://setuplivesync?settings=%24abc"))
        assertTrue(ReaderApi.isSetupUri("  %\$abcdef  "))
        assertFalse(ReaderApi.isSetupUri(""))
        assertFalse(ReaderApi.isSetupUri("https://example.org"))
        assertFalse(ReaderApi.isSetupUri("%$" + "a".repeat(32 * 1024)))
    }

    @Test
    fun vaultFolderCheck() {
        assertTrue(ReaderApi.isVaultFolder(""))
        assertTrue(ReaderApi.isVaultFolder("Constantinople By Night"))
        assertTrue(ReaderApi.isVaultFolder("Σημειώσεις_2026-v1.2"))
        assertFalse(ReaderApi.isVaultFolder("a/b"))
        assertFalse(ReaderApi.isVaultFolder(".hidden"))
        assertFalse(ReaderApi.isVaultFolder("trailing "))
        assertFalse(ReaderApi.isVaultFolder("ends."))
        assertFalse(ReaderApi.isVaultFolder("α".repeat(33)))  // 66 bytes
        assertTrue(ReaderApi.isVaultFolder("α".repeat(32)))
    }

    @Test
    fun fontNames() {
        assertEquals("Literata-BoldItalic.ttf", ReaderApi.fontFileName("Literata-BoldItalic.ttf"))
        assertEquals("Fira_Sans__1_.TTF".lowercase(), ReaderApi.fontFileName("Fira Sans (1).TTF")!!.lowercase())
        assertEquals("Literata_12.cpfont", ReaderApi.fontFileName("Literata_12.cpfont"))
        assertNull(ReaderApi.fontFileName("book.epub"))
        assertNull(ReaderApi.fontFileName("noextension"))
        assertNull(ReaderApi.fontFileName(".ttf"))
        assertEquals("Literata", ReaderApi.fontFamilyFrom("Literata-BoldItalic.ttf"))
        assertEquals("Literata", ReaderApi.fontFamilyFrom("Literata_12.cpfont"))
        assertEquals("Vollkorn", ReaderApi.fontFamilyFrom("Vollkorn.otf"))
        assertEquals("", ReaderApi.fontFamilyFrom("notes.md"))
        assertTrue(ReaderApi.isFontFamily("Fira-Sans_2"))
        assertFalse(ReaderApi.isFontFamily("Fira Sans"))
        assertFalse(ReaderApi.isFontFamily("Γραμματοσειρά"))
        assertFalse(ReaderApi.isFontFamily(""))
    }

    @Test
    fun pollWaitsTenSecondsBetweenTries() {
        val p = PollPolicy()
        assertEquals(0L, p.delayFrom(0))  // nothing tried yet
        p.record(1_000, ok = true)
        assertEquals(10_000L, p.delayFrom(1_000))
        assertEquals(4_000L, p.delayFrom(7_000))
        assertEquals(0L, p.delayFrom(60_000))  // back in the foreground after a while: at once, not sooner than 10 s
        assertFalse(p.stopped)
    }

    @Test
    fun pollBacksOffThenStopsUntilRetry() {
        val p = PollPolicy()
        p.record(0, ok = true)
        p.record(10_000, ok = false)
        assertFalse(p.stopped)
        assertEquals(20_000L, p.delayFrom(10_000))  // one miss: twice the wait
        p.record(30_000, ok = false)
        assertTrue(p.stopped)
        assertNull(p.delayFrom(30_000))
        assertNull(p.delayFrom(999_000))  // stays stopped however long it waits
        p.retry()
        assertFalse(p.stopped)
        assertEquals(9_000L, p.delayFrom(31_000))  // the retry still keeps 10 s from the last try
    }

    @Test
    fun pollForgetsAMissAfterAnAnswer() {
        val p = PollPolicy()
        p.record(0, ok = false)
        p.record(20_000, ok = true)
        assertEquals(10_000L, p.delayFrom(20_000))
        p.record(30_000, ok = false)
        assertFalse(p.stopped)
    }

    @Test
    fun readCappedStopsAtTheLimit() {
        assertEquals(100, readCapped(ByteArray(100).inputStream(), 100)!!.size)
        assertNull(readCapped(ByteArray(101).inputStream(), 100))
        assertEquals(0, readCapped(ByteArray(0).inputStream(), 100)!!.size)
        assertEquals(40_000, readCapped(ByteArray(40_000) { it.toByte() }.inputStream(), FONT_MAX_BYTES)!!.size)
    }

    @Test
    fun uptimeAndSignal() {
        assertEquals("45 s", uptimeText(45))
        assertEquals("12 min", uptimeText(12 * 60 + 5))
        assertEquals("3 h 12 min", uptimeText(3 * 3600 + 12 * 60))
        assertEquals("2 d 1 h", uptimeText(2 * 86_400 + 3700))
        assertEquals("3 ώ. 12 λ.", uptimeText(3 * 3600 + 12 * 60, "ημ.", "ώ.", "λ.", "δ."))
        assertEquals(0, signalBars(0))
        assertEquals(4, signalBars(-45))
        assertEquals(3, signalBars(-60))
        assertEquals(2, signalBars(-70))
        assertEquals(1, signalBars(-85))
    }
}
