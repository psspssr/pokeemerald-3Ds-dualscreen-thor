package com.emerald3ds.android

/** Synthetic sector-format data only, never a live-game or natural-play fixture.
 * Two fixed payload words exercise unsigned 32-bit wrap; their independent
 * folded checksum is 0xACDE+marker. No production validation helper is used. */
internal object EmeraldSaveFixture {
    const val SECTOR = 4096
    val dataSizes = intArrayOf(0xF2C, 0xF80, 0xF80, 0xF80, 0xF08,
        0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0)

    fun create(seed: Int = 1, counter: Long = 2, bytes: Int = 128 * 1024): ByteArray =
        ByteArray(bytes) { 0xff.toByte() }.also { writeSlot(it, 0, seed, counter) }

    fun writeSlot(image: ByteArray, slot: Int, seed: Int, counter: Long, rotation: Int = 0) {
        require(slot in 0..1 && image.size >= (slot + 1) * 14 * SECTOR)
        require(counter in 0L..0xFFFFFFFFL && counter and 1L == slot.toLong())
        for (physical in 0 until 14) {
            val section = (physical + rotation) % 14
            val start = (slot * 14 + physical) * SECTOR
            val size = dataSizes[section]
            val marker = (seed + section) and 0xff
            image.fill(0, start, start + size)
            put32(image, start + size - 8, 0xABCD0000L or marker.toLong())
            put32(image, start + size - 4, 0xF0001111L)
            put16(image, start + 0xFF4, section)
            put16(image, start + 0xFF6, 0xACDE + marker)
            put32(image, start + 0xFF8, 0x08012025L)
            put32(image, start + 0xFFC, counter)
        }
    }

    fun put16(image: ByteArray, at: Int, value: Int) {
        image[at] = value.toByte()
        image[at + 1] = (value ushr 8).toByte()
    }

    fun put32(image: ByteArray, at: Int, value: Long) {
        for (byte in 0..3) image[at + byte] = (value ushr (8 * byte)).toByte()
    }

    fun u16(image: ByteArray, at: Int): Int =
        (image[at].toInt() and 0xff) or ((image[at + 1].toInt() and 0xff) shl 8)
}
