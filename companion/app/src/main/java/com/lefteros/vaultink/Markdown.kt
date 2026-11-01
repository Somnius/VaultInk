package com.lefteros.vaultink

/**
 * A small Markdown reader for the bundled documentation (Help). Pure Kotlin, no Android, so it is
 * unit tested. It knows what the repo's docs use: ATX headings, paragraphs, bullet and numbered
 * lists (nested by indent, task boxes), fenced code, block quotes, pipe tables, rules, links,
 * images (kept as their alt text), bold, italic and inline code. Raw HTML blocks are reduced to
 * the alt text of any <img> in them.
 */
object Markdown {

    /** One run of inline text with its style; [href] is set on a link. */
    data class Span(
        val text: String,
        val bold: Boolean = false,
        val italic: Boolean = false,
        val code: Boolean = false,
        val href: String? = null,
    )

    sealed class Block
    data class Heading(val level: Int, val spans: List<Span>, val anchor: String) : Block()
    data class Paragraph(val spans: List<Span>) : Block()
    /** One list item; [level] is the nesting depth from 0; [number] is null for a bullet. */
    data class ListItem(val level: Int, val number: Int?, val spans: List<Span>) : Block()
    data class Code(val text: String, val language: String) : Block()
    data class Quote(val spans: List<Span>) : Block()
    data class Table(val header: List<List<Span>>, val rows: List<List<List<Span>>>) : Block()
    /** An image on its own; [alt] is all that is shown. */
    data class Image(val alt: String) : Block()
    object Rule : Block()

    private val HEADING = Regex("""^(#{1,6})\s+(.*?)\s*#*\s*$""")
    private val BULLET = Regex("""^(\s*)[-*+]\s+(.*)$""")
    private val NUMBERED = Regex("""^(\s*)(\d+)[.)]\s+(.*)$""")
    private val RULE = Regex("""^\s*([-*_])(\s*\1){2,}\s*$""")
    private val TABLE_SEP = Regex("""^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$""")
    private val IMG_ALT = Regex("""<img[^>]*\balt="([^"]*)"""")
    private val TASK = Regex("""^\[( |x|X)]\s+""")

    fun parse(text: String): List<Block> {
        val lines = text.replace("\r\n", "\n").split('\n')
        val out = mutableListOf<Block>()
        var i = 0
        val paragraph = mutableListOf<String>()
        fun flush() {
            if (paragraph.isNotEmpty()) {
                out += Paragraph(inline(paragraph.joinToString(" ") { it.trim() }))
                paragraph.clear()
            }
        }
        while (i < lines.size) {
            val line = lines[i]
            val trimmed = line.trim()
            when {
                trimmed.isEmpty() -> { flush(); i++ }
                trimmed.startsWith("```") || trimmed.startsWith("~~~") -> {
                    flush()
                    val fence = trimmed.take(3)
                    val lang = trimmed.drop(3).trim()
                    val body = mutableListOf<String>()
                    i++
                    while (i < lines.size && !lines[i].trim().startsWith(fence)) { body += lines[i]; i++ }
                    i++  // the closing fence (or the end)
                    out += Code(body.joinToString("\n"), lang)
                }
                HEADING.matches(trimmed) -> {
                    flush()
                    val m = HEADING.find(trimmed)!!
                    val title = m.groupValues[2]
                    out += Heading(m.groupValues[1].length, inline(title), anchor(title))
                    i++
                }
                RULE.matches(trimmed) && paragraph.isEmpty() -> { out += Rule; i++ }
                trimmed.startsWith(">") -> {
                    flush()
                    val quoted = mutableListOf<String>()
                    while (i < lines.size && lines[i].trim().startsWith(">")) {
                        quoted += lines[i].trim().removePrefix(">").trim()
                        i++
                    }
                    // Blank quoted lines split paragraphs inside the quote.
                    var part = mutableListOf<String>()
                    for (q in quoted + "") {
                        if (q.isEmpty()) { if (part.isNotEmpty()) out += Quote(inline(part.joinToString(" "))); part = mutableListOf() }
                        else part += q
                    }
                }
                trimmed.startsWith("<") -> {
                    flush()
                    // Raw HTML: keep only what pictures say.
                    while (i < lines.size && lines[i].trim().isNotEmpty()) {
                        IMG_ALT.findAll(lines[i]).forEach { out += Image(it.groupValues[1]) }
                        i++
                    }
                }
                isTableStart(lines, i) -> {
                    flush()
                    val header = cells(lines[i]).map { inline(it) }
                    i += 2
                    val rows = mutableListOf<List<List<Span>>>()
                    while (i < lines.size && lines[i].contains('|') && lines[i].trim().isNotEmpty()) {
                        rows += cells(lines[i]).map { inline(it) }
                        i++
                    }
                    out += Table(header, rows)
                }
                BULLET.matches(line) || NUMBERED.matches(line) -> {
                    flush()
                    val indents = mutableListOf<Int>()  // indent widths of the open levels
                    while (i < lines.size) {
                        val l = lines[i]
                        val b = BULLET.find(l)
                        val n = if (b == null) NUMBERED.find(l) else null
                        if (b == null && n == null) {
                            // A wrapped line of the item above, or the end of the list.
                            if (l.trim().isEmpty() || l.first() != ' ' || out.lastOrNull() !is ListItem) break
                            val last = out.removeAt(out.size - 1) as ListItem
                            out += last.copy(spans = inline(plain(last.spans) + " " + l.trim()))
                            i++
                            continue
                        }
                        val indent = (b ?: n)!!.groupValues[1].replace("\t", "    ").length
                        while (indents.isNotEmpty() && indent < indents.last()) indents.removeAt(indents.size - 1)
                        if (indents.isEmpty() || indent > indents.last()) indents += indent
                        var body = if (b != null) b.groupValues[2] else n!!.groupValues[3]
                        TASK.find(body)?.let { body = (if (it.groupValues[1] == " ") "☐ " else "☑ ") + body.substring(it.range.last + 1) }
                        out += ListItem(indents.size - 1, n?.groupValues?.get(2)?.toIntOrNull(), inline(body))
                        i++
                    }
                }
                else -> { paragraph += line; i++ }
            }
        }
        flush()
        return out
    }

    private fun isTableStart(lines: List<String>, i: Int): Boolean =
        i + 1 < lines.size && lines[i].contains('|') && TABLE_SEP.matches(lines[i + 1]) && lines[i + 1].contains('|')

    private fun cells(line: String): List<String> {
        val t = line.trim().removePrefix("|").removeSuffix("|")
        val out = mutableListOf<String>()
        val cur = StringBuilder()
        var k = 0
        var inCode = false
        while (k < t.length) {
            val c = t[k]
            when {
                c == '\\' && k + 1 < t.length && t[k + 1] == '|' -> { cur.append('|'); k++ }
                c == '`' -> { inCode = !inCode; cur.append(c) }
                c == '|' && !inCode -> { out += cur.toString().trim(); cur.clear() }
                else -> cur.append(c)
            }
            k++
        }
        out += cur.toString().trim()
        return out
    }

    /** GitHub-style heading anchor: lower case, punctuation dropped, spaces to hyphens. */
    fun anchor(title: String): String = plain(inline(title)).lowercase()
        .filter { it.isLetterOrDigit() || it == ' ' || it == '-' || it == '_' }
        .trim().replace(' ', '-')

    fun plain(spans: List<Span>): String = spans.joinToString("") { it.text }

    /** Inline parse: `code`, **bold**, *italic* / _italic_, [text](href), ![alt](src), <http://…>. */
    fun inline(text: String): List<Span> {
        val out = mutableListOf<Span>()
        val cur = StringBuilder()
        var bold = false
        var italic = false
        var i = 0
        fun flush() { if (cur.isNotEmpty()) { out += Span(cur.toString(), bold, italic); cur.clear() } }
        while (i < text.length) {
            val c = text[i]
            val next = text.getOrNull(i + 1)
            when {
                c == '\\' && next != null && !next.isLetterOrDigit() && next != ' ' -> { cur.append(next); i += 2 }
                c == '`' -> {
                    val end = text.indexOf('`', i + 1)
                    if (end < 0) { cur.append(c); i++ } else {
                        flush()
                        out += Span(text.substring(i + 1, end), code = true)
                        i = end + 1
                    }
                }
                c == '!' && next == '[' -> {
                    val link = link(text, i + 1)
                    if (link == null) { cur.append(c); i++ } else { cur.append(link.first); i = link.third }
                }
                c == '[' -> {
                    val link = link(text, i)
                    if (link == null) { cur.append(c); i++ } else {
                        flush()
                        out += Span(link.first, bold, italic, href = link.second)
                        i = link.third
                    }
                }
                c == '<' && (text.startsWith("<http://", i) || text.startsWith("<https://", i)) && text.indexOf('>', i) > 0 -> {
                    val end = text.indexOf('>', i)
                    flush()
                    val url = text.substring(i + 1, end)
                    out += Span(url, bold, italic, href = url)
                    i = end + 1
                }
                c == '*' && next == '*' || c == '_' && next == '_' -> { flush(); bold = !bold; i += 2 }
                c == '*' -> { flush(); italic = !italic; i++ }
                c == '_' && (if (italic) (next == null || !next.isLetterOrDigit()) else (i == 0 || !text[i - 1].isLetterOrDigit())) -> {
                    flush(); italic = !italic; i++
                }
                else -> { cur.append(c); i++ }
            }
        }
        flush()
        return out
    }

    /** `[text](href "title")` at [start]: text, href and the index after it, or null. */
    private fun link(text: String, start: Int): Triple<String, String, Int>? {
        var depth = 0
        var k = start
        while (k < text.length) {
            when (text[k]) {
                '[' -> depth++
                ']' -> { depth--; if (depth == 0) break }
            }
            k++
        }
        if (k >= text.length || text.getOrNull(k + 1) != '(') return null
        val close = text.indexOf(')', k + 2)
        if (close < 0) return null
        val target = text.substring(k + 2, close).trim().substringBefore(' ')
        val label = text.substring(start + 1, k)
        return Triple(plain(inline(label)), target, close + 1)
    }

    /** The first heading of a document, or null. */
    fun title(text: String): String? = text.lineSequence()
        .firstOrNull { HEADING.matches(it.trim()) }
        ?.let { plain(inline(HEADING.find(it.trim())!!.groupValues[2])) }

    /**
     * Resolves a link from a bundled doc to another bundled doc: the file name and the anchor, or
     * null for anything that isn't a local .md file (http, mail, images).
     */
    fun docLink(href: String): Pair<String, String?>? {
        if (href.startsWith("http://") || href.startsWith("https://") || href.startsWith("mailto:")) return null
        val anchor = href.substringAfter('#', "").ifEmpty { null }
        val path = href.substringBefore('#')
        if (path.isEmpty()) return anchor?.let { "" to it }
        val name = path.substringAfterLast('/')
        if (!name.endsWith(".md")) return null
        return name to anchor
    }

    /** The first line of [text] that contains [query] (case-insensitive), trimmed; null when none. */
    fun firstMatch(text: String, query: String): String? {
        if (query.isBlank()) return null
        val q = query.trim().lowercase()
        return text.lineSequence().firstOrNull { it.lowercase().contains(q) }?.trim()
            ?.let { l -> plain(inline(l.trimStart('#', '-', '*', '>', ' ', '|'))) }
    }
}
