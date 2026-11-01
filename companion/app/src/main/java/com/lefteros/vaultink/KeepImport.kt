package com.lefteros.vaultink

import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/**
 * Google Keep notes (a Takeout export: the JSON files in `Takeout/Keep`) turned into Markdown notes for a vault.
 * Pure Kotlin, no Android: the JSON is parsed here so unit tests run on the JVM.
 */
object KeepImport {
    /** One Keep note, as the Takeout JSON describes it. */
    data class Note(
        val title: String,
        val text: String,
        /** Checklist items: text and whether they're ticked. Empty for a plain text note. */
        val items: List<Pair<String, Boolean>>,
        val labels: List<String>,
        val trashed: Boolean,
        val archived: Boolean,
        val pinned: Boolean,
        val createdUsec: Long,
        val editedUsec: Long,
    )

    /** A note ready for the vault: a file name safe on the card and the Markdown body. */
    data class Converted(val fileName: String, val markdown: String)

    /** What a pass over a Takeout folder found: notes to send, and how many were left out and why. */
    data class Batch(val notes: List<Converted>, val trashed: Int, val unreadable: Int) {
        val total get() = notes.size + trashed + unreadable
    }

    /** Reads one Keep JSON file; null when it isn't a Keep note. */
    fun parse(json: String): Note? {
        val root = (runCatching { Json.parse(json) }.getOrNull() as? Map<*, *>) ?: return null
        if ("textContent" !in root && "listContent" !in root && "title" !in root) return null
        val items = (root["listContent"] as? List<*>).orEmpty().mapNotNull { item ->
            val m = item as? Map<*, *> ?: return@mapNotNull null
            (m["text"] as? String ?: "") to (m["isChecked"] as? Boolean ?: false)
        }
        val labels = (root["labels"] as? List<*>).orEmpty().mapNotNull { (it as? Map<*, *>)?.get("name") as? String }
        return Note(
            title = root["title"] as? String ?: "",
            text = root["textContent"] as? String ?: "",
            items = items,
            labels = labels,
            trashed = root["isTrashed"] as? Boolean ?: false,
            archived = root["isArchived"] as? Boolean ?: false,
            pinned = root["isPinned"] as? Boolean ?: false,
            createdUsec = (root["createdTimestampUsec"] as? Number)?.toLong() ?: 0L,
            editedUsec = (root["userEditedTimestampUsec"] as? Number)?.toLong() ?: 0L,
        )
    }

    /** Converts every JSON given (name to content); trashed notes are counted, not converted. */
    fun convertAll(files: List<Pair<String, String>>): Batch {
        val notes = mutableListOf<Converted>()
        var trashed = 0
        var unreadable = 0
        val used = mutableSetOf<String>()
        for ((_, content) in files) {
            val note = parse(content)
            if (note == null) { unreadable++; continue }
            if (note.trashed) { trashed++; continue }
            var c = convert(note)
            // Two notes with the same title and minute: "-2", "-3" ... so neither is lost.
            if (!used.add(c.fileName.lowercase())) {
                val base = c.fileName.removeSuffix(".md")
                var n = 2
                while (!used.add("$base-$n.md".lowercase())) n++
                c = c.copy(fileName = "$base-$n.md")
            }
            notes += c
        }
        return Batch(notes, trashed, unreadable)
    }

    fun convert(note: Note): Converted {
        val created = isoDate(note.createdUsec)
        val updated = isoDate(if (note.editedUsec > 0) note.editedUsec else note.createdUsec)
        val heading = note.title.trim().ifEmpty { firstLine(note) }.ifEmpty { "Untitled" }
        val sb = StringBuilder()
        sb.append("---\n")
        if (created != null) sb.append("created: ").append(created).append('\n')
        if (updated != null) sb.append("updated: ").append(updated).append('\n')
        if (note.labels.isNotEmpty()) {
            sb.append("tags:\n")
            note.labels.forEach { sb.append("  - ").append(yamlTag(it)).append('\n') }
        }
        if (note.pinned) sb.append("pinned: true\n")
        if (note.archived) sb.append("archived: true\n")
        sb.append("source: keep\n")
        sb.append("---\n\n")
        sb.append("# ").append(heading.replace('\n', ' ')).append("\n\n")
        val text = note.text.replace("\r\n", "\n").trimEnd()
        if (text.isNotEmpty()) sb.append(text).append('\n')
        if (note.items.isNotEmpty()) {
            if (text.isNotEmpty()) sb.append('\n')
            for ((item, checked) in note.items) {
                sb.append(if (checked) "- [x] " else "- [ ] ").append(item.replace('\n', ' ').trim()).append('\n')
            }
        }
        // A date first keeps the folder sorted by time and tells two "Shopping" notes apart.
        val prefix = fileDate(if (note.createdUsec > 0) note.createdUsec else note.editedUsec)
        val name = safeName(heading).take(60).trim().ifEmpty { "Note" }
        return Converted(if (prefix.isEmpty()) "$name.md" else "$prefix $name.md", sb.toString())
    }

    private fun firstLine(note: Note): String =
        note.text.lineSequence().map { it.trim() }.firstOrNull { it.isNotEmpty() }?.take(60)
            ?: note.items.firstOrNull()?.first?.trim()?.take(60) ?: ""

    /** Characters FAT and Obsidian refuse, and '#' (a tag in Obsidian's link syntax), become spaces. */
    fun safeName(title: String): String =
        title.replace(Regex("[\\\\/:*?\"<>|#^\\[\\]\\p{Cntrl}]"), " ").replace(Regex("\\s+"), " ").trim().trimStart('.').trimEnd('.')

    /** A Keep label as an Obsidian tag: spaces become dashes, nothing a tag can't hold. */
    private fun yamlTag(label: String): String {
        val tag = label.trim().replace(Regex("\\s+"), "-").replace(Regex("[#,:\\[\\]{}\"']"), "")
        return tag.ifEmpty { "keep" }
    }

    private fun isoDate(usec: Long): String? =
        if (usec <= 0) null else DateTimeFormatter.ISO_OFFSET_DATE_TIME.format(Instant.ofEpochMilli(usec / 1000).atOffset(ZoneOffset.UTC))

    private fun fileDate(usec: Long): String =
        if (usec <= 0) "" else DateTimeFormatter.ofPattern("yyyy-MM-dd HHmm").format(Instant.ofEpochMilli(usec / 1000).atOffset(ZoneOffset.UTC))

    /** A small JSON reader: objects to Map, arrays to List, numbers to Double/Long, plus String, Boolean and null. */
    internal object Json {
        fun parse(text: String): Any? {
            val p = Parser(text)
            val v = p.value()
            p.ws()
            if (p.i != text.length) throw IllegalArgumentException("trailing data at ${p.i}")
            return v
        }

        private class Parser(val s: String) {
            var i = 0
            fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
            fun value(): Any? {
                ws()
                if (i >= s.length) throw IllegalArgumentException("unexpected end")
                return when (s[i]) {
                    '{' -> obj()
                    '[' -> arr()
                    '"' -> str()
                    't' -> lit("true", true)
                    'f' -> lit("false", false)
                    'n' -> lit("null", null)
                    else -> num()
                }
            }
            private fun lit(word: String, v: Any?): Any? {
                if (!s.startsWith(word, i)) throw IllegalArgumentException("bad literal at $i")
                i += word.length
                return v
            }
            private fun obj(): Map<String, Any?> {
                val m = LinkedHashMap<String, Any?>()
                i++; ws()
                if (s[i] == '}') { i++; return m }
                while (true) {
                    ws()
                    val k = str()
                    ws(); expect(':')
                    m[k] = value()
                    ws()
                    if (s[i] == ',') { i++; continue }
                    expect('}'); return m
                }
            }
            private fun arr(): List<Any?> {
                val l = ArrayList<Any?>()
                i++; ws()
                if (s[i] == ']') { i++; return l }
                while (true) {
                    l += value()
                    ws()
                    if (s[i] == ',') { i++; continue }
                    expect(']'); return l
                }
            }
            private fun expect(c: Char) {
                if (i >= s.length || s[i] != c) throw IllegalArgumentException("expected $c at $i")
                i++
            }
            private fun str(): String {
                expect('"')
                val sb = StringBuilder()
                while (true) {
                    if (i >= s.length) throw IllegalArgumentException("unterminated string")
                    val c = s[i++]
                    when (c) {
                        '"' -> return sb.toString()
                        '\\' -> {
                            val e = s[i++]
                            when (e) {
                                '"' -> sb.append('"'); '\\' -> sb.append('\\'); '/' -> sb.append('/')
                                'b' -> sb.append('\b'); 'f' -> sb.append('\u000C'); 'n' -> sb.append('\n')
                                'r' -> sb.append('\r'); 't' -> sb.append('\t')
                                'u' -> { sb.append(s.substring(i, i + 4).toInt(16).toChar()); i += 4 }
                                else -> throw IllegalArgumentException("bad escape at $i")
                            }
                        }
                        else -> sb.append(c)
                    }
                }
            }
            private fun num(): Any {
                val start = i
                while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
                val t = s.substring(start, i)
                if (t.isEmpty()) throw IllegalArgumentException("unexpected '${s[i]}' at $i")
                return t.toLongOrNull() ?: t.toDouble()
            }
        }
    }
}
