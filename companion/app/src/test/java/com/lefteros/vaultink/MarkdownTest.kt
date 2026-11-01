package com.lefteros.vaultink

import com.lefteros.vaultink.Markdown.Code
import com.lefteros.vaultink.Markdown.Heading
import com.lefteros.vaultink.Markdown.Image
import com.lefteros.vaultink.Markdown.ListItem
import com.lefteros.vaultink.Markdown.Paragraph
import com.lefteros.vaultink.Markdown.Quote
import com.lefteros.vaultink.Markdown.Rule
import com.lefteros.vaultink.Markdown.Table
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MarkdownTest {
    private fun plain(spans: List<Markdown.Span>) = Markdown.plain(spans)

    @Test
    fun headingsWithAnchors() {
        val blocks = Markdown.parse("# VaultInk companion (Android)\n\n## First start\n### Tabs: Note & Files")
        assertEquals(3, blocks.size)
        val h1 = blocks[0] as Heading
        assertEquals(1, h1.level)
        assertEquals("VaultInk companion (Android)", plain(h1.spans))
        assertEquals("vaultink-companion-android", h1.anchor)
        assertEquals("first-start", (blocks[1] as Heading).anchor)
        assertEquals("tabs-note--files", (blocks[2] as Heading).anchor)
        assertEquals("VaultInk companion (Android)", Markdown.title("intro\n# VaultInk companion (Android)\n"))
    }

    @Test
    fun paragraphsJoinWrappedLines() {
        val blocks = Markdown.parse("One line\nand the next.\n\nSecond paragraph.")
        assertEquals(listOf("One line and the next.", "Second paragraph."), blocks.map { plain((it as Paragraph).spans) })
    }

    @Test
    fun inlineStyles() {
        val spans = Markdown.inline("Open **File Transfer** and *wait*, then `vaultink.local` or _this_.")
        assertEquals(Markdown.Span("File Transfer", bold = true), spans[1])
        assertEquals(Markdown.Span("wait", italic = true), spans[3])
        assertEquals(Markdown.Span("vaultink.local", code = true), spans[5])
        assertEquals(Markdown.Span("this", italic = true), spans[7])
        // Underscores inside words are not emphasis.
        assertEquals("a snake_case_name here", plain(Markdown.inline("a snake_case_name here")))
    }

    @Test
    fun bulletAndNumberedLists() {
        val md = """
            - Tap a folder to open it; the path at the top
              takes you back up.
            - **Upload** picks files.
              - nested one
            1. first
            2. second
            - [ ] open task
            - [x] done task
        """.trimIndent()
        val items = Markdown.parse(md).map { it as ListItem }
        assertEquals(7, items.size)
        assertEquals("Tap a folder to open it; the path at the top takes you back up.", plain(items[0].spans))
        assertEquals(0, items[0].level)
        assertTrue(items[1].spans.first().bold)
        assertEquals(1, items[2].level)
        assertEquals("nested one", plain(items[2].spans))
        assertEquals(1, items[3].number)
        assertEquals(2, items[4].number)
        assertNull(items[5].number)
        assertEquals("☐ open task", plain(items[5].spans))
        assertEquals("☑ done task", plain(items[6].spans))
    }

    @Test
    fun pipeTable() {
        val md = "| Device | Xteink X4 Pro |\n| --- | --- |\n| Firmware | C++20 on Arduino, `PlatformIO` |\n| Base | [CrossPoint](https://example.org) |\n\nAfter."
        val blocks = Markdown.parse(md)
        val table = blocks[0] as Table
        assertEquals(listOf("Device", "Xteink X4 Pro"), table.header.map { plain(it) })
        assertEquals(2, table.rows.size)
        assertEquals("C++20 on Arduino, PlatformIO", plain(table.rows[0][1]))
        assertTrue(table.rows[0][1].last().code)
        assertEquals("https://example.org", table.rows[1][1].first().href)
        assertEquals("After.", plain((blocks[1] as Paragraph).spans))
    }

    @Test
    fun linksAndDocLinks() {
        val spans = Markdown.inline("See [comics.md](comics.md) and [the site](https://lefteros.com/en/apps/vaultink) and <https://x.org>.")
        assertEquals(Markdown.Span("comics.md", href = "comics.md"), spans[1])
        assertEquals("https://lefteros.com/en/apps/vaultink", spans[3].href)
        assertEquals("https://x.org", spans[5].href)
        assertEquals("comics.md" to null, Markdown.docLink("comics.md"))
        assertEquals("livesync.md" to "setup", Markdown.docLink("docs/livesync.md#setup"))
        assertEquals("companion.md" to null, Markdown.docLink("../docs/companion.md"))
        assertEquals("" to "tabs", Markdown.docLink("#tabs"))
        assertNull(Markdown.docLink("https://github.com/Somnius/VaultInk"))
        assertNull(Markdown.docLink("images/companion/1-note.png"))
    }

    @Test
    fun codeFencesKeepTextVerbatim() {
        val blocks = Markdown.parse("Build:\n\n```bash\ncd companion\n./gradlew assembleRelease  # **not bold**\n```\nDone.")
        val code = blocks[1] as Code
        assertEquals("bash", code.language)
        assertEquals("cd companion\n./gradlew assembleRelease  # **not bold**", code.text)
        assertEquals("Done.", plain((blocks[2] as Paragraph).spans))
    }

    @Test
    fun quotesAndRules() {
        val blocks = Markdown.parse("> A quoted line\n> continues here.\n\n---\n\nText")
        assertEquals("A quoted line continues here.", plain((blocks[0] as Quote).spans))
        assertEquals(Rule, blocks[1])
        assertEquals("Text", plain((blocks[2] as Paragraph).spans))
    }

    @Test
    fun imagesAreSkippedKeepingAltText() {
        val blocks = Markdown.parse("![The Note tab](images/companion/1-note.png)\n\n<p>\n  <img src=\"images/a.png\" width=\"300\" alt=\"A shared book\">\n</p>\n\nText with ![inline pic](x.png) inside.")
        assertEquals("The Note tab", plain((blocks[0] as Paragraph).spans))
        assertEquals(Image("A shared book"), blocks[1])
        assertEquals("Text with inline pic inside.", plain((blocks[2] as Paragraph).spans))
        assertNull(blocks[2].let { (it as Paragraph).spans.firstOrNull { s -> s.href != null } })
    }

    @Test
    fun greekText() {
        val blocks = Markdown.parse("# Σημειώσεις στη συσκευή\n\n- **Αρχεία**: όλη η κάρτα SD.\n\n| Στήλη | Τιμή |\n|---|---|\n| Ελληνικά | ναι |")
        assertEquals("Σημειώσεις στη συσκευή", plain((blocks[0] as Heading).spans))
        assertEquals("σημειώσεις-στη-συσκευή", (blocks[0] as Heading).anchor)
        assertEquals("Αρχεία: όλη η κάρτα SD.", plain((blocks[1] as ListItem).spans))
        assertEquals("ναι", plain((blocks[2] as Table).rows[0][1]))
        assertEquals("Αρχεία: όλη η κάρτα SD.", Markdown.firstMatch("# Τίτλος\n- **Αρχεία**: όλη η κάρτα SD.", "κάρτα"))
    }

    @Test
    fun firstMatchIsCaseInsensitive() {
        assertEquals("the LiveSync card", Markdown.firstMatch("# Title\n\n  the LiveSync card\n", "livesync"))
        assertNull(Markdown.firstMatch("nothing here", "zebra"))
        assertNull(Markdown.firstMatch("nothing here", "  "))
    }
}
