// test/test_wav_header/test_wav_header.cpp
// Byte-exact checks for the shared 44-byte PCM16-mono WAV header writer.
#include <unity.h>
#include <string.h>
#include "hal/WavHeader.h"

using stkchan::writeWavHeader;
using stkchan::kWavHeaderBytes;

void setUp() {}
void tearDown() {}

static void test_header_size_constant() {
  TEST_ASSERT_EQUAL_UINT32(44u, (uint32_t)kWavHeaderBytes);
}

static void test_riff_wave_fmt_data_magics() {
  uint8_t h[44];
  writeWavHeader(h, 16000, 16000);   // 1 s @ 16 kHz
  TEST_ASSERT_EQUAL_MEMORY("RIFF", h + 0, 4);
  TEST_ASSERT_EQUAL_MEMORY("WAVE", h + 8, 4);
  TEST_ASSERT_EQUAL_MEMORY("fmt ", h + 12, 4);
  TEST_ASSERT_EQUAL_MEMORY("data", h + 36, 4);
}

static void test_sizes_and_format_fields() {
  uint8_t h[44];
  writeWavHeader(h, 16000, 16000);
  uint32_t fileBytes, dataBytes, sampleRate, byteRate;
  uint16_t fmt, ch, bits, align;
  memcpy(&fileBytes, h + 4, 4);
  memcpy(&fmt,  h + 20, 2);
  memcpy(&ch,   h + 22, 2);
  memcpy(&sampleRate, h + 24, 4);
  memcpy(&byteRate,   h + 28, 4);
  memcpy(&align, h + 32, 2);
  memcpy(&bits,  h + 34, 2);
  memcpy(&dataBytes, h + 40, 4);
  TEST_ASSERT_EQUAL_UINT32(32000u, dataBytes);          // 16000 samples * 2 B
  TEST_ASSERT_EQUAL_UINT32(32000u + 44u - 8u, fileBytes);
  TEST_ASSERT_EQUAL_UINT16(1, fmt);                      // PCM
  TEST_ASSERT_EQUAL_UINT16(1, ch);                       // mono
  TEST_ASSERT_EQUAL_UINT32(16000u, sampleRate);
  TEST_ASSERT_EQUAL_UINT32(32000u, byteRate);            // rate * 2
  TEST_ASSERT_EQUAL_UINT16(2, align);
  TEST_ASSERT_EQUAL_UINT16(16, bits);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_header_size_constant);
  RUN_TEST(test_riff_wave_fmt_data_magics);
  RUN_TEST(test_sizes_and_format_fields);
  return UNITY_END();
}
