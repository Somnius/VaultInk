package com.lefteros.vaultink

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PhoneTextTest {
    @Test
    fun countsUtf8Bytes() {
        assertEquals(5, PhoneText.bytes("hello"))
        assertEquals(9, PhoneText.bytes("γειά!"))  // four Greek letters at 2 bytes each + "!"
        assertEquals(4, PhoneText.bytes("😀"))
    }

    @Test
    fun shortTextIsUnchanged() {
        val t = "a short line\nand another"
        assertEquals(t, PhoneText.clip(t))
    }

    @Test
    fun clipsAsciiToTheLimit() {
        val t = "x".repeat(PhoneText.MAX_BYTES + 50)
        assertEquals(PhoneText.MAX_BYTES, PhoneText.clip(t).length)
    }

    @Test
    fun clipsOnACharacterBoundary() {
        // 2-byte letters: an odd limit must not split one.
        val t = "α".repeat(1200)  // 2400 bytes
        val clipped = PhoneText.clip(t)
        assertTrue(PhoneText.bytes(clipped) <= PhoneText.MAX_BYTES)
        assertEquals(1000, clipped.length)
        assertTrue(clipped.all { it == 'α' })
    }

    @Test
    fun keepsSurrogatePairsWhole() {
        val t = "x".repeat(PhoneText.MAX_BYTES - 2) + "😀" + "yyy"  // the emoji is 4 bytes, straddling the limit
        val clipped = PhoneText.clip(t)
        assertEquals(PhoneText.MAX_BYTES - 2, clipped.length)
        assertTrue(PhoneText.bytes(clipped) <= PhoneText.MAX_BYTES)
    }
}
