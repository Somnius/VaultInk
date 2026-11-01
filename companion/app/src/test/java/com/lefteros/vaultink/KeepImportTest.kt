package com.lefteros.vaultink

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class KeepImportTest {
    // 2024-03-05 10:20:30 UTC in microseconds.
    private val created = 1709634030000000L
    private val edited = 1709720430000000L

    private fun keep(
        title: String = "Shopping",
        text: String? = "milk",
        list: String? = null,
        labels: String = "",
        trashed: Boolean = false,
        archived: Boolean = false,
        pinned: Boolean = false,
    ): String = buildString {
        append("{\"color\":\"DEFAULT\",\"isTrashed\":$trashed,\"isPinned\":$pinned,\"isArchived\":$archived,")
        if (list != null) append("\"listContent\":$list,") else if (text != null) append("\"textContent\":${quote(text)},")
        append("\"title\":${quote(title)},\"userEditedTimestampUsec\":$edited,\"createdTimestampUsec\":$created")
        if (labels.isNotEmpty()) append(",\"labels\":$labels")
        append("}")
    }

    private fun quote(s: String) = "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n") + "\""

    @Test
    fun plainNoteHasFrontMatterHeadingAndText() {
        val c = KeepImport.convert(KeepImport.parse(keep())!!)
        assertEquals("2024-03-05 1020 Shopping.md", c.fileName)
        assertEquals(
            "---\ncreated: 2024-03-05T10:20:30Z\nupdated: 2024-03-06T10:20:30Z\nsource: keep\n---\n\n# Shopping\n\nmilk\n",
            c.markdown,
        )
    }

    @Test
    fun checklistBecomesTaskList() {
        val list = "[{\"text\":\"eggs\",\"isChecked\":true},{\"text\":\"bread\",\"isChecked\":false}]"
        val c = KeepImport.convert(KeepImport.parse(keep(list = list))!!)
        assertTrue(c.markdown.endsWith("# Shopping\n\n- [x] eggs\n- [ ] bread\n"))
    }

    @Test
    fun labelsBecomeTags() {
        val c = KeepImport.convert(KeepImport.parse(keep(labels = "[{\"name\":\"Work\"},{\"name\":\"to read\"}]"))!!)
        assertTrue(c.markdown.contains("tags:\n  - Work\n  - to-read\n"))
    }

    @Test
    fun pinnedAndArchivedAreKeptInFrontMatter() {
        val c = KeepImport.convert(KeepImport.parse(keep(archived = true, pinned = true))!!)
        assertTrue(c.markdown.contains("pinned: true\narchived: true\n"))
    }

    @Test
    fun trashedNotesAreSkippedAndCounted() {
        val batch = KeepImport.convertAll(listOf("a.json" to keep(), "b.json" to keep(trashed = true), "c.json" to keep(title = "Other")))
        assertEquals(2, batch.notes.size)
        assertEquals(1, batch.trashed)
        assertEquals(0, batch.unreadable)
        assertEquals(3, batch.total)
    }

    @Test
    fun oddCharactersInTitleMakeASafeFileName() {
        val c = KeepImport.convert(KeepImport.parse(keep(title = "Re: a/b\\c*?\"<>|#1 [x]  ...  "))!!)
        assertEquals("2024-03-05 1020 Re a b c 1 x.md", c.fileName)
        assertTrue(c.markdown.contains("# Re: a/b\\c*?\"<>|#1 [x]  ...\n"))
    }

    @Test
    fun emptyNoteGetsUntitled() {
        val c = KeepImport.convert(KeepImport.parse(keep(title = "", text = ""))!!)
        assertEquals("2024-03-05 1020 Untitled.md", c.fileName)
        assertTrue(c.markdown.contains("# Untitled\n"))
        assertTrue(c.markdown.endsWith("# Untitled\n\n"))
    }

    @Test
    fun untitledNoteTakesItsFirstLine() {
        val c = KeepImport.convert(KeepImport.parse(keep(title = "", text = "\n\nCall the dentist\nTomorrow"))!!)
        assertEquals("2024-03-05 1020 Call the dentist.md", c.fileName)
    }

    @Test
    fun greekTextSurvives() {
        val c = KeepImport.convert(KeepImport.parse(keep(title = "Λίστα αγορών", text = "γάλα, ψωμί\nκαφές"))!!)
        assertEquals("2024-03-05 1020 Λίστα αγορών.md", c.fileName)
        assertTrue(c.markdown.contains("# Λίστα αγορών\n\nγάλα, ψωμί\nκαφές\n"))
    }

    @Test
    fun unicodeEscapesInJsonAreDecoded() {
        val json = "{\"title\":\"\\u03b1\\u03b2 \\\"q\\\"\",\"textContent\":\"a\\nb\",\"createdTimestampUsec\":$created}"
        val c = KeepImport.convert(KeepImport.parse(json)!!)
        assertEquals("2024-03-05 1020 αβ q.md", c.fileName)
        assertTrue(c.markdown.contains("# αβ \"q\"\n\na\nb\n"))
    }

    @Test
    fun sameTitleAndMinuteGetNumbered() {
        val batch = KeepImport.convertAll(listOf("a.json" to keep(), "b.json" to keep()))
        assertEquals(listOf("2024-03-05 1020 Shopping.md", "2024-03-05 1020 Shopping-2.md"), batch.notes.map { it.fileName })
    }

    @Test
    fun notAKeepNoteIsUnreadable() {
        assertNull(KeepImport.parse("[1,2,3]"))
        assertNull(KeepImport.parse("{\"foo\":1}"))
        assertNull(KeepImport.parse("not json"))
        val batch = KeepImport.convertAll(listOf("x.json" to "{}"))
        assertEquals(1, batch.unreadable)
        assertTrue(batch.notes.isEmpty())
    }

    @Test
    fun missingTimestampsLeaveNoDates() {
        val c = KeepImport.convert(KeepImport.parse("{\"title\":\"T\",\"textContent\":\"x\"}")!!)
        assertEquals("T.md", c.fileName)
        assertFalse(c.markdown.contains("created:"))
        assertEquals("---\nsource: keep\n---\n\n# T\n\nx\n", c.markdown)
    }
}
