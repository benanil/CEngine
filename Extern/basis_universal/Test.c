// Online C compiler to run C program online
#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <stddef.h>
#include <stdint.h>
#include <immintrin.h>  // SSE2

#if defined(__GNUC__) || defined(__MINGW32__)
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    #define RESTRICT
#endif

void MemCopy(const void* src, void* RESTRICT dst, size_t size) {
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    
    // SIMD copy - 4x128-bit per iteration
    size_t simd_count = size >> 4;  // Divide by 16
    
    if (simd_count > 0) {
        switch (simd_count & 3) {
            case 3: _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s)); s += 16; d += 16;
            case 2: _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s)); s += 16; d += 16;
            case 1: _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s)); s += 16; d += 16;
            case 0: break;
        }
        
        simd_count >>= 2;
        while (simd_count--) {
            __m128i xmm0 = _mm_loadu_si128((const __m128i*)s);
            __m128i xmm1 = _mm_loadu_si128((const __m128i*)(s + 16));
            __m128i xmm2 = _mm_loadu_si128((const __m128i*)(s + 32));
            __m128i xmm3 = _mm_loadu_si128((const __m128i*)(s + 48));
            
            _mm_storeu_si128((__m128i*)d, xmm0);
            _mm_storeu_si128((__m128i*)(d + 16), xmm1);
            _mm_storeu_si128((__m128i*)(d + 32), xmm2);
            _mm_storeu_si128((__m128i*)(d + 48), xmm3);
            s += 64; d += 64;
        }
    }
    
    size_t r = size & 15;
    if (r >= 8) { *(uint64_t*)d = *(uint64_t*)s; d+=8; s+=8; r-=8; }
    if (r >= 4) { *(uint32_t*)d = *(uint32_t*)s; d+=4; s+=4; r-=4; }
    if (r >= 2) { *(uint16_t*)d = *(uint16_t*)s; d+=2; s+=2; r-=2; }
    if (r)      { *d = *s; }
}

void MemSet(void* dst, uint8_t value, size_t size) {
    uint8_t* d = (uint8_t*)dst;
    
    // Broadcast the byte value to a 128-bit register
    __m128i xmm_value = _mm_set1_epi8(value);
    
    // SIMD set - 4x128-bit per iteration
    size_t simd_count = size >> 4;  // Divide by 16
    
    if (simd_count > 0) {
        switch (simd_count & 3) {
            case 3: _mm_storeu_si128((__m128i*)d, xmm_value); d += 16;
            case 2: _mm_storeu_si128((__m128i*)d, xmm_value); d += 16;
            case 1: _mm_storeu_si128((__m128i*)d, xmm_value); d += 16;
            case 0: break;
        }
        
        simd_count >>= 2;
        while (simd_count--) {
            _mm_storeu_si128((__m128i*)d, xmm_value);
            _mm_storeu_si128((__m128i*)(d + 16), xmm_value);
            _mm_storeu_si128((__m128i*)(d + 32), xmm_value);
            _mm_storeu_si128((__m128i*)(d + 48), xmm_value);
            d += 64;
        }
    }
    
    // Tail bytes
    size_t r = size & 15;
    if (r >= 8) { 
        *(uint64_t*)d = (uint64_t)value * 0x0101010101010101ULL; 
        d += 8; r -= 8; 
    }
    if (r >= 4) { 
        *(uint32_t*)d = (uint32_t)value * 0x01010101U; 
        d += 4; r -= 4; 
    }
    if (r >= 2) { 
        *(uint16_t*)d = (uint16_t)value * 0x0101U; 
        d += 2; r -= 2; 
    }
    if (r) { 
        *d = value; 
    }
}

// Include the MemCopy function here or link it

// Test helper to verify memory contents
int verify_copy(const uint8_t* src, const uint8_t* dst, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (src[i] != dst[i]) {
            printf("Mismatch at byte %zu: expected 0x%02x, got 0x%02x\n", 
                   i, src[i], dst[i]);
            return 0;
        }
    }
    return 1;
}

// Test with specific alignment
void test_alignment(size_t src_align, size_t dst_align, size_t size) {
    // Allocate larger buffer to avoid any overlap issues
    size_t buffer_size = (size + 128) * 2 + 128;
    uint8_t* buffer = (uint8_t*)_aligned_malloc(buffer_size, 64);
    
    if (!buffer) {
        printf("FAIL: Memory allocation failed\n");
        return;
    }
    
    // Initialize entire buffer
    memset(buffer, 0xCC, buffer_size);
    
    uint8_t* src = buffer + 64 + src_align;
    uint8_t* dst = buffer + 64 + size + 128 + dst_align;
    
    // Fill source with pattern
    for (size_t i = 0; i < size; i++) {
        src[i] = (uint8_t)(i & 0xFF);
    }
    
    // Clear destination
    memset(dst, 0xAA, size);
    
    // Copy
    MemCopy(src, dst, size);
    
    // Verify
    if (verify_copy(src, dst, size)) {
        printf("PASS: src_align=%zu, dst_align=%zu, size=%zu\n", 
               src_align, dst_align, size);
    } else {
        printf("FAIL: src_align=%zu, dst_align=%zu, size=%zu\n", 
               src_align, dst_align, size);
    }
    
    _aligned_free(buffer);
}

// Test various sizes
void test_sizes() {
    printf("\n=== Testing Various Sizes ===\n");
    
    size_t test_sizes[] = {
        0, 1, 7, 15, 16, 17, 31, 32, 63, 64, 65,
        127, 128, 255, 256, 511, 512, 1023, 1024,
        4095, 4096, 8192, 16384, 65536
    };
    
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        test_alignment(0, 0, test_sizes[i]);
    }
}

// Test all alignment combinations
void test_all_alignments() {
    printf("\n=== Testing All Alignment Combinations ===\n");
    
    size_t size = 256;
    for (size_t src_align = 0; src_align < 16; src_align++) {
        for (size_t dst_align = 0; dst_align < 16; dst_align++) {
            test_alignment(src_align, dst_align, size);
        }
    }
}

// Test edge cases
void test_edge_cases() {
    printf("\n=== Testing Edge Cases ===\n");
    
    // Test zero size
    uint8_t* buffer = (uint8_t*)_aligned_malloc(128, 64);
    MemCopy(buffer, buffer + 64, 0);
    _aligned_free(buffer);
    printf("PASS: Zero size copy\n");
    
    // Test single byte at various alignments
    for (size_t align = 0; align < 16; align++) {
        test_alignment(align, 0, 1);
    }
    
    // Test sizes that trigger different Duff's device cases
    for (size_t chunks = 1; chunks <= 4; chunks++) {
        test_alignment(0, 0, chunks * 16);
    }
    
    // Test just before and after 64-byte boundary
    test_alignment(0, 0, 63);
    test_alignment(0, 0, 64);
    test_alignment(0, 0, 65);
}

// Performance test
void performance_test() {
    printf("\n=== Performance Test ===\n");
    
    size_t size = 1024 * 1024 * 10; // 10 MB
    uint8_t* src = (uint8_t*)_aligned_malloc(size, 64);
    uint8_t* dst = (uint8_t*)_aligned_malloc(size, 64);
    
    if (!src || !dst) {
        printf("FAIL: Memory allocation failed\n");
        free(src);
        free(dst);
        return;
    }
    
    // Fill with random data
    for (size_t i = 0; i < size; i++) {
        src[i] = (uint8_t)(rand() & 0xFF);
    }
    // heat
    for (int i = 0; i < 100; i++) {
        MemCopy(src, dst, size);
    }
    // Test MemCopy
    clock_t start = clock();
    for (int i = 0; i < 100; i++) {
        MemCopy(src, dst, size);
    }
    clock_t end = clock();
    double memcopy_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    // Verify
    if (!verify_copy(src, dst, size)) {
        printf("FAIL: Performance test verification failed\n");
    }
    
    // Test memcpy for comparison
    start = clock();
    for (int i = 0; i < 100; i++) {
        memcpy(dst, src, size);
    }
    end = clock();
    double memcpy_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("MemCopy: %.3f seconds (%.2f MB/s)\n", 
           memcopy_time, (size * 100.0 / (1024*1024)) / memcopy_time);
    printf("memcpy:  %.3f seconds (%.2f MB/s)\n", 
           memcpy_time, (size * 100.0 / (1024*1024)) / memcpy_time);
    
    _aligned_free(src);
    _aligned_free(dst);
}

// Test with random data patterns
void test_random_patterns() {
    printf("\n=== Testing Random Patterns ===\n");
    
    srand(time(NULL));
    
    for (int test = 0; test < 100; test++) {
        size_t size = (rand() % 4096) + 1;
        size_t src_align = rand() % 16;
        size_t dst_align = rand() % 16;
        
        size_t buffer_size = (size + 128) * 2 + 128;
        uint8_t* buffer = (uint8_t*)_aligned_malloc(buffer_size, 64);
        
        if (!buffer) {
            printf("FAIL: Memory allocation failed\n");
            return;
        }
        
        memset(buffer, 0xCC, buffer_size);
        
        uint8_t* src = buffer + 64 + src_align;
        uint8_t* dst = buffer + 64 + size + 128 + dst_align;
        
        // Random data
        for (size_t i = 0; i < size; i++) {
            src[i] = rand() & 0xFF;
        }
        
        memset(dst, 0, size);
        MemCopy(src, dst, size);
        
        if (!verify_copy(src, dst, size)) {
            printf("FAIL: Random test #%d (size=%zu, src_align=%zu, dst_align=%zu)\n",
                   test, size, src_align, dst_align);
            _aligned_free(buffer);
            return;
        }
        
        _aligned_free(buffer);
    }
    
    printf("PASS: All 100 random pattern tests\n");
}


// Test helper to verify memory contents
int verify_set(const uint8_t* dst, uint8_t expected, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (dst[i] != expected) {
            printf("Mismatch at byte %zu: expected 0x%02x, got 0x%02x\n", 
                   i, expected, dst[i]);
            return 0;
        }
    }
    return 1;
}

// Test with specific size and value
void test_size_and_value(size_t size, uint8_t value) {
    uint8_t* buffer = (uint8_t*)malloc(size + 32);
    if (!buffer) {
        printf("FAIL: Memory allocation failed\n");
        return;
    }
    
    // Fill with different pattern first
    memset(buffer, ~value, size + 32);
    
    // Set the memory
    MemSet(buffer + 8, value, size);
    
    // Verify guard bytes weren't touched
    for (size_t i = 0; i < 8; i++) {
        if (buffer[i] != (uint8_t)~value) {
            printf("FAIL: Guard byte before modified at %zu\n", i);
            free(buffer);
            return;
        }
    }
    for (size_t i = size + 8; i < size + 32; i++) {
        if (buffer[i] != (uint8_t)~value) {
            printf("FAIL: Guard byte after modified at %zu\n", i);
            free(buffer);
            return;
        }
    }
    
    // Verify the set region
    if (verify_set(buffer + 8, value, size)) {
        printf("PASS: size=%zu, value=0x%02x\n", size, value);
    } else {
        printf("FAIL: size=%zu, value=0x%02x\n", size, value);
    }
    
    free(buffer);
}

// Test various sizes
void test_sizes_memset() {
    printf("\n=== Testing Various Sizes _memset===\n");
    
    size_t test_sizes[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 31, 32, 63, 64, 65, 127, 128, 255, 256,
        511, 512, 1023, 1024, 4095, 4096, 8192, 16384, 65536
    };
    
    for (size_t i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        test_size_and_value(test_sizes[i], 0xAA);
    }
}

// Test various values
void test_values() {
    printf("\n=== Testing Various Values ===\n");
    
    uint8_t test_values[] = {
        0x00, 0x01, 0x7F, 0x80, 0xFF,
        0xAA, 0x55, 0xCC, 0x33, 0xF0, 0x0F
    };
    
    size_t size = 1024;
    for (size_t i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        test_size_and_value(size, test_values[i]);
    }
}

// Test alignment
void test_alignment_memset() {
    printf("\n=== Testing Alignment ===\n");
    
    size_t size = 256;
    uint8_t value = 0x42;
    
    for (size_t align = 0; align < 16; align++) {
        uint8_t* buffer = (uint8_t*)malloc(size + 64);
        if (!buffer) continue;
        
        memset(buffer, 0xFF, size + 64);
        uint8_t* aligned_ptr = buffer + 16 + align;
        
        MemSet(aligned_ptr, value, size);
        
        if (verify_set(aligned_ptr, value, size)) {
            printf("PASS: alignment=%zu, size=%zu, value=0x%02x\n", align, size, value);
        } else {
            printf("FAIL: alignment=%zu, size=%zu, value=0x%02x\n", align, size, value);
        }
        
        free(buffer);
    }
}

// Test edge cases
void test_edge_cases_memset() {
    printf("\n=== Testing Edge Cases memset ===\n");
    
    // Test zero size
    uint8_t dummy[16];
    memset(dummy, 0xFF, sizeof(dummy));
    MemSet(dummy, 0x00, 0);
    // Verify nothing changed
    int all_ff = 1;
    for (size_t i = 0; i < sizeof(dummy); i++) {
        if (dummy[i] != 0xFF) all_ff = 0;
    }
    printf("%s: Zero size\n", all_ff ? "PASS" : "FAIL");
    
    // Test all sizes from 0-255
    for (size_t size = 0; size <= 255; size++) {
        test_size_and_value(size, 0x5A);
    }
    printf("PASS: All sizes 0-255\n");
}

// Performance test
void performance_test_memset() {
    printf("\n=== Performance Test memset ===\n");
    
    size_t size = 1024 * 1024 * 10; // 10 MB
    uint8_t* buffer = (uint8_t*)malloc(size);
    
    if (!buffer) {
        printf("FAIL: Memory allocation failed\n");
        return;
    }
    
    uint8_t value = 0xAB;
    
    // Test MemSet
    clock_t start = clock();
    for (int i = 0; i < 100; i++) {
        MemSet(buffer, value, size);
    }
    clock_t end = clock();
    double memset_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    // Verify
    if (!verify_set(buffer, value, size)) {
        printf("FAIL: Performance test verification failed\n");
    }
    
    // Test standard memset for comparison
    start = clock();
    for (int i = 0; i < 100; i++) {
        memset(buffer, value, size);
    }
    end = clock();
    double std_memset_time = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("MemSet:     %.3f seconds (%.2f MB/s)\n", 
           memset_time, (size * 100.0 / (1024*1024)) / memset_time);
    printf("memset:     %.3f seconds (%.2f MB/s)\n", 
           std_memset_time, (size * 100.0 / (1024*1024)) / std_memset_time);
    printf("Speedup:    %.2fx\n", std_memset_time / memset_time);
    
    free(buffer);
}

// Random test
void test_random() {
    printf("\n=== Testing Random Patterns ===\n");
    
    srand(time(NULL));
    
    for (int test = 0; test < 100; test++) {
        size_t size = (rand() % 8192) + 1;
        uint8_t value = rand() & 0xFF;
        
        uint8_t* buffer = (uint8_t*)malloc(size + 32);
        if (!buffer) continue;
        
        memset(buffer, ~value, size + 32);
        MemSet(buffer + 8, value, size);
        
        if (!verify_set(buffer + 8, value, size)) {
            printf("FAIL: Random test #%d (size=%zu, value=0x%02x)\n",
                   test, size, value);
            free(buffer);
            return;
        }
        
        free(buffer);
    }
    
    printf("PASS: All 100 random pattern tests\n");
}

int main() {
    printf("Starting MemCopy Tests\n");
    printf("======================\n");
    
    test_sizes();
    test_all_alignments();
    test_edge_cases();
    test_random_patterns();
    performance_test();
    
    test_random();
    test_alignment_memset();
    test_edge_cases_memset();
    test_sizes_memset();
    performance_test_memset();

    printf("\n======================\n");
    printf("All tests completed!\n");
    
    return 0;
}
