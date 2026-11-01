package com.lefteros.vaultink

/**
 * Text typed on the phone for a text field on the reader (`/.vaultink/phone-text.txt`). The reader
 * reads at most [MAX_BYTES] of UTF-8, so the box counts bytes, not characters, and cuts on a
 * character boundary.
 */
object PhoneText {
    const val MAX_BYTES = 2000

    fun bytes(text: String): Int = text.toByteArray(Charsets.UTF_8).size

    /** `text` cut to at most [MAX_BYTES] of UTF-8 without splitting a character (or a surrogate pair). */
    fun clip(text: String): String {
        if (bytes(text) <= MAX_BYTES) return text
        var end = text.length
        while (end > 0 && bytes(text.substring(0, end)) > MAX_BYTES) {
            end--
            if (end > 0 && Character.isLowSurrogate(text[end]) && Character.isHighSurrogate(text[end - 1])) end--
        }
        return text.substring(0, end)
    }
}
