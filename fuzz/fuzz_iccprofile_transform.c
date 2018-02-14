/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// This fuzz target parses an ICCProfile and attempts to do some transforms
// between a known profile and that profile.

#include "../skcms.h"

// This is profiles/sRGB_Facebook.icc, copied here (xxd -i ...) so that this
// binary does not need any files as input, other than the fuzzed input.
static unsigned char profiles_sRGB_Facebook_icc[] = {
  0x00, 0x00, 0x02, 0x0c, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00,
  0x6d, 0x6e, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5a, 0x20,
  0x07, 0xdc, 0x00, 0x01, 0x00, 0x19, 0x00, 0x03, 0x00, 0x29, 0x00, 0x39,
  0x61, 0x63, 0x73, 0x70, 0x41, 0x50, 0x50, 0x4c, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6, 0xd6,
  0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xd3, 0x2d, 0x6c, 0x63, 0x6d, 0x73,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a,
  0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x5e,
  0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x5c, 0x00, 0x00, 0x00, 0x0b,
  0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x68, 0x00, 0x00, 0x00, 0x14,
  0x62, 0x6b, 0x70, 0x74, 0x00, 0x00, 0x01, 0x7c, 0x00, 0x00, 0x00, 0x14,
  0x72, 0x58, 0x59, 0x5a, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x14,
  0x67, 0x58, 0x59, 0x5a, 0x00, 0x00, 0x01, 0xa4, 0x00, 0x00, 0x00, 0x14,
  0x62, 0x58, 0x59, 0x5a, 0x00, 0x00, 0x01, 0xb8, 0x00, 0x00, 0x00, 0x14,
  0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x40,
  0x67, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x40,
  0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x40,
  0x64, 0x65, 0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
  0x63, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x74, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x00, 0x46, 0x42, 0x00, 0x00,
  0x58, 0x59, 0x5a, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf6, 0xd6,
  0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xd3, 0x2d, 0x58, 0x59, 0x5a, 0x20,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x16, 0x00, 0x00, 0x03, 0x33,
  0x00, 0x00, 0x02, 0xa4, 0x58, 0x59, 0x5a, 0x20, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x6f, 0xa2, 0x00, 0x00, 0x38, 0xf5, 0x00, 0x00, 0x03, 0x90,
  0x58, 0x59, 0x5a, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x99,
  0x00, 0x00, 0xb7, 0x85, 0x00, 0x00, 0x18, 0xda, 0x58, 0x59, 0x5a, 0x20,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xa0, 0x00, 0x00, 0x0f, 0x84,
  0x00, 0x00, 0xb6, 0xcf, 0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0xcb, 0x01, 0xc9, 0x03, 0x63,
  0x05, 0x92, 0x08, 0x6b, 0x0b, 0xf6, 0x10, 0x3f, 0x15, 0x51, 0x1b, 0x34,
  0x21, 0xf1, 0x29, 0x90, 0x32, 0x18, 0x3b, 0x92, 0x46, 0x05, 0x51, 0x77,
  0x5d, 0xed, 0x6b, 0x70, 0x7a, 0x05, 0x89, 0xb1, 0x9a, 0x7c, 0xac, 0x69,
  0xbf, 0x7d, 0xd3, 0xc3, 0xe9, 0x30, 0xff, 0xff
};

void exit(int);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    skcms_ICCProfile p;
    if (!skcms_Parse(data, size, &p)) {
        return 0;
    }
    skcms_ICCProfile srgb;
    if (!skcms_Parse(profiles_sRGB_Facebook_icc, sizeof(profiles_sRGB_Facebook_icc), &srgb)) {
        // This should never happen, but if it does, we want to make
        // a big fuss about it (exiting kills libfuzzer, as if it crashed).
        exit(1);
    }
    uint8_t src[256],
            dst[256];
    for (int i = 0; i < 256; i++) {
        src[i] = (uint8_t)i;
    }
    skcms_Transform(src, skcms_PixelFormat_RGBA_8888, &srgb,
                    dst, skcms_PixelFormat_RGBA_8888, &p,
                    64);

    skcms_Transform(src, skcms_PixelFormat_RGBA_8888, &p,
                    dst, skcms_PixelFormat_RGBA_8888, &srgb,
                    64);

    return 0;
}
