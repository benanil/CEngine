#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../Include/Bitset.h"

// Helper to fill a bitset block with 1s or 0s
static void ClearBuffer(u64* buffer, size_t wordCount, u64 pattern) {
    for (size_t i = 0; i < wordCount; ++i) {
        buffer[i] = pattern;
    }
}

// -----------------------------------------------------------------------------
// Basic Operations Tests
// -----------------------------------------------------------------------------
void Test_BasicOperations() {
    u64 bits[2] = {0, 0};

    // Test Set and Get
    BitsetSet(bits, 5);
    BitsetSet(bits, 67);
    assert(BitsetGet(bits, 5) == true);
    assert(BitsetGet(bits, 67) == true);
    assert(BitsetGet(bits, 0) == false);
    assert(BitsetGet(bits, 64) == false);

    // Test Reset
    BitsetReset(bits, 5);
    assert(BitsetGet(bits, 5) == false);
    assert(BitsetGet(bits, 67) == true);

    // Test FindFirstSet
    assert(FindFirstSet(0) == -1);
    assert(FindFirstSet(1ull << 12) == 12);
    assert(FindFirstSet(0x8000000000000000ull) == 63);

    printf("[PASS] Basic Operations\n");
}

// -----------------------------------------------------------------------------
// BitsetSetRange Tests
// -----------------------------------------------------------------------------
void Test_BitsetSetRange() {
    u64 bits[4];

    // Case 1: Fill range spanning partial words
    ClearBuffer(bits, 4, 0ull);
    BitsetSetRange(bits, 10, 100, true); // Span across word boundary
    for (s32 i = 0; i < 256; ++i) {
        bool expected = (i >= 10 && i < 110);
        assert(BitsetGet(bits, i) == expected);
    }

    // Case 2: Clear subset range
    BitsetSetRange(bits, 20, 30, false);
    for (s32 i = 0; i < 256; ++i) {
        bool expected = (i >= 10 && i < 110) && !(i >= 20 && i < 50);
        assert(BitsetGet(bits, i) == expected);
    }

    // Case 3: Exact word-aligned operations
    ClearBuffer(bits, 4, 0ull);
    BitsetSetRange(bits, 64, 128, true); // Words idx 1 and 2 entirely
    assert(bits[0] == 0ull);
    assert(bits[1] == ~0ull);
    assert(bits[2] == ~0ull);
    assert(bits[3] == 0ull);

    printf("[PASS] BitsetSetRange\n");
}

// -----------------------------------------------------------------------------
// BitsetFindFirstEmpty & Range Searches
// -----------------------------------------------------------------------------
void Test_FindEmpty() {
    // 16 words = 1024 bits (Triggers SIMD branches)
    u64 bits[16]; 
    ClearBuffer(bits, 16, ~0ull); // Everything is full

    // Case 1: Fully occupied
    assert(BitsetFindFirstEmpty(bits, 1024) == -1);
    assert(BitsetFindEmptyRange(bits, 1024, 5) == -1);

    // Case 2: Free 1 bit at offset 70
    BitsetReset(bits, 70);
    assert(BitsetFindFirstEmpty(bits, 1024) == 70);
    assert(BitsetFindEmptyRange(bits, 1024, 1) == 70);

    // Case 3: Clear a range that spans exactly 4 full words to test SIMD pass over blocks
    ClearBuffer(bits, 16, ~0ull);
    // Clear 256 bits starting from index 256 (word index 4 through 7)
    BitsetSetRange(bits, 256, 256, false);
    assert(BitsetFindFirstEmpty(bits, 1024) == 256);
    assert(BitsetFindEmptyRange(bits, 1024, 200) == 256);

    // Case 4: Broken mixed-word sequence ranges
    ClearBuffer(bits, 16, ~0ull);
    BitsetSetRange(bits, 10, 5, false);  // Empty space 10..14 (Len 5)
    BitsetSetRange(bits, 20, 10, false); // Empty space 20..29 (Len 10)
    assert(BitsetFindEmptyRange(bits, 1024, 8) == 20); // Should pick the one that fits

    printf("[PASS] FindEmpty and FindEmptyRange\n");
}

// -----------------------------------------------------------------------------
// PopCount and Capacity Capacity Tests
// -----------------------------------------------------------------------------
void Test_PopCounts() {
    u64 bits[16];
    ClearBuffer(bits, 16, 0ull);

    // Set arbitrary scattered bits
    BitsetSet(bits, 2);
    BitsetSet(bits, 65);
    BitsetSet(bits, 511);
    BitsetSet(bits, 1023);

    // Total bits counted up to 1024 bounds
    assert(BitsetPopCount(bits, 1024) == 4);
    assert(BitsetPopCount(bits, 500) == 2); // Excludes 511 and 1023

    // Check modern vector threshold properties
    assert(BitsetHasAtLeastEmptyBits(bits, 1024, 1020) == true);
    assert(BitsetHasAtLeastEmptyBits(bits, 1024, 1021) == false);

    // Check tail bits functionality (non-64 multiple sizes)
    ClearBuffer(bits, 16, ~0ull); // Fill all 1s
    // 70 bits total, means word 0 (64 bits) + 6 bits from word 1. All are 1s.
    assert(BitsetPopCount(bits, 70) == 70);
    assert(BitsetHasAtLeastEmptyBits(bits, 70, 1) == false);

    printf("[PASS] PopCount and BitsetHasAtLeastEmptyBits\n");
}

// -----------------------------------------------------------------------------
// Main execution harness
// -----------------------------------------------------------------------------
int main() {
    printf("Executing Bitset.h verification suite...\n");
    printf("----------------------------------------\n");

    Test_BasicOperations();
    Test_BitsetSetRange();
    Test_FindEmpty();
    Test_PopCounts();

    printf("----------------------------------------\n");
    printf("ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}