package com.emerald3ds.android

/** Raw Emerald flash-sector integrity, independent of player or Pokémon data.
 * Sizes and checksum match save.c and the GBA ABI assertions in save_abi.c.
 * A complete older slot remains usable when the other write was interrupted.
 */
internal object EmeraldSaveFormat {
    private const val SECTOR_BYTES = 4096
    private const val SECTIONS = 14
    private val dataSizes = intArrayOf(0xF2C, 0xF80, 0xF80, 0xF80, 0xF08,
        0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0)

    fun hasValidSlot(bytes: ByteArray): Boolean {
        if (bytes.size != 64 * 1024 && bytes.size != 128 * 1024) return false
        val first = readSlot(bytes, 0)
        val second = readSlot(bytes, 1)
        // Match GetSaveValidStatus's selection, including its wrap boundary.
        // A good backup must not hide a newer checksum-complete slot with
        // inconsistent counters: native would choose that malformed slot.
        val selected = when {
            first == null -> second
            second == null -> first
            first.counter == -1 && second.counter == 0 -> second
            first.counter == 0 && second.counter == -1 -> first
            (first.counter.toLong() and 0xFFFFFFFFL) < (second.counter.toLong() and 0xFFFFFFFFL) -> second
            else -> first
        }
        return selected?.coherent == true
    }

    private data class Slot(val counter: Int, val coherent: Boolean)

    private fun readSlot(bytes: ByteArray, slot: Int): Slot? {
        val start = slot * SECTIONS * SECTOR_BYTES
        if (bytes.size < start + SECTIONS * SECTOR_BYTES) return null
        val firstCounter = read32(bytes, start + 0xFFC)
        var counter = firstCounter
        var consistent = true
        var seen = 0
        for (physical in 0 until SECTIONS) {
            val sector = start + physical * SECTOR_BYTES
            val section = read16(bytes, sector + 0xFF4)
            if (section >= SECTIONS || seen and (1 shl section) != 0 ||
                read32(bytes, sector + 0xFF8) != 0x08012025) return null
            counter = read32(bytes, sector + 0xFFC)
            consistent = consistent && counter == firstCounter
            var sum = 0
            for (offset in 0 until dataSizes[section] step 4)
                sum += read32(bytes, sector + offset) // GBA unsigned 32-bit wrap.
            val checksum = ((sum and 0xFFFF) + (sum ushr 16)) and 0xFFFF
            if (checksum != read16(bytes, sector + 0xFF6)) return null
            seen = seen or (1 shl section)
        }
        // Native CopySaveSlotData selects the flash half from this parity.
        return Slot(counter, consistent && counter and 1 == slot)
    }

    private fun read16(bytes: ByteArray, offset: Int): Int =
        (bytes[offset].toInt() and 0xFF) or ((bytes[offset + 1].toInt() and 0xFF) shl 8)

    private fun read32(bytes: ByteArray, offset: Int): Int =
        read16(bytes, offset) or (read16(bytes, offset + 2) shl 16)
}
