/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../skcms.h"
#include "Macros.h"
#include "TransferFunction.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


static uint32_t make_signature(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)(a << 24)
         | (uint32_t)(b << 16)
         | (uint32_t)(c <<  8)
         | (uint32_t)(d <<  0);
}

static uint16_t read_big_u16(const uint8_t* ptr) {
    uint16_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_ushort(be);
#else
    return __builtin_bswap16(be);
#endif
}

static uint32_t read_big_u32(const uint8_t* ptr) {
    uint32_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_ulong(be);
#else
    return __builtin_bswap32(be);
#endif
}

static int32_t read_big_i32(const uint8_t* ptr) {
    return (int32_t)read_big_u32(ptr);
}

static uint64_t read_big_u64(const uint8_t* ptr) {
    uint64_t be;
    memcpy(&be, ptr, sizeof(be));
#if defined(_MSC_VER)
    return _byteswap_uint64(be);
#else
    return __builtin_bswap64(be);
#endif
}

static float read_big_fixed(const uint8_t* ptr) {
    return read_big_i32(ptr) * (1.0f / 65536.0f);
}

static skcms_ICCDateTime read_big_date_time(const uint8_t* ptr) {
    skcms_ICCDateTime date_time;
    date_time.year   = read_big_u16(ptr + 0);
    date_time.month  = read_big_u16(ptr + 2);
    date_time.day    = read_big_u16(ptr + 4);
    date_time.hour   = read_big_u16(ptr + 6);
    date_time.minute = read_big_u16(ptr + 8);
    date_time.second = read_big_u16(ptr + 10);
    return date_time;
}

// Maps to an in-memory profile so that fields line up to the locations specified
// in ICC.1:2010, section 7.2
typedef struct {
    uint8_t size                [ 4];
    uint8_t cmm_type            [ 4];
    uint8_t version             [ 4];
    uint8_t profile_class       [ 4];
    uint8_t data_color_space    [ 4];
    uint8_t pcs                 [ 4];
    uint8_t creation_date_time  [12];
    uint8_t signature           [ 4];
    uint8_t platform            [ 4];
    uint8_t flags               [ 4];
    uint8_t device_manufacturer [ 4];
    uint8_t device_model        [ 4];
    uint8_t device_attributes   [ 8];
    uint8_t rendering_intent    [ 4];
    uint8_t illuminant_X        [ 4];
    uint8_t illuminant_Y        [ 4];
    uint8_t illuminant_Z        [ 4];
    uint8_t creator             [ 4];
    uint8_t profile_id          [16];
    uint8_t reserved            [28];
    uint8_t tag_count           [ 4]; // Technically not part of header, but required
} header_Layout;

typedef struct {
    uint8_t signature [4];
    uint8_t offset    [4];
    uint8_t size      [4];
} tag_Layout;

static const tag_Layout* get_tag_table(const skcms_ICCProfile* profile) {
    return (const tag_Layout*)(profile->buffer + SAFE_SIZEOF(header_Layout));
}

bool skcms_Parse(const void* buf, size_t len, skcms_ICCProfile* profile) {
    static_assert(SAFE_SIZEOF(header_Layout) == 132, "ICC header size");

    if (!profile) {
        return false;
    }
    memset(profile, 0, SAFE_SIZEOF(*profile));

    if (len < SAFE_SIZEOF(header_Layout)) {
        return false;
    }

    // Byte-swap all header fields
    const header_Layout* header = buf;
    profile->buffer              = buf;
    profile->size                = read_big_u32(header->size);
    profile->cmm_type            = read_big_u32(header->cmm_type);
    profile->version             = read_big_u32(header->version);
    profile->profile_class       = read_big_u32(header->profile_class);
    profile->data_color_space    = read_big_u32(header->data_color_space);
    profile->pcs                 = read_big_u32(header->pcs);
    profile->creation_date_time  = read_big_date_time(header->creation_date_time);
    profile->signature           = read_big_u32(header->signature);
    profile->platform            = read_big_u32(header->platform);
    profile->flags               = read_big_u32(header->flags);
    profile->device_manufacturer = read_big_u32(header->device_manufacturer);
    profile->device_model        = read_big_u32(header->device_model);
    profile->device_attributes   = read_big_u64(header->device_attributes);
    profile->rendering_intent    = read_big_u32(header->rendering_intent);
    profile->illuminant_X        = read_big_fixed(header->illuminant_X);
    profile->illuminant_Y        = read_big_fixed(header->illuminant_Y);
    profile->illuminant_Z        = read_big_fixed(header->illuminant_Z);
    profile->creator             = read_big_u32(header->creator);
    static_assert(SAFE_SIZEOF(profile->profile_id) == SAFE_SIZEOF(header->profile_id),
                  "profile_id size");
    memcpy(profile->profile_id, header->profile_id, SAFE_SIZEOF(header->profile_id));
    profile->tag_count           = read_big_u32(header->tag_count);

    // Validate signature, size (smaller than buffer, large enough to hold tag table),
    // and major version
    uint64_t tag_table_size = profile->tag_count * SAFE_SIZEOF(tag_Layout);
    if (profile->signature != make_signature('a', 'c', 's', 'p') ||
        profile->size > len ||
        profile->size < SAFE_SIZEOF(header_Layout) + tag_table_size ||
        (profile->version >> 24) > 4) {
        return false;
    }

    // Validate that illuminant is D50 white
    if (fabsf(profile->illuminant_X - 0.9642f) > 0.0100f ||
        fabsf(profile->illuminant_Y - 1.0000f) > 0.0100f ||
        fabsf(profile->illuminant_Z - 0.8249f) > 0.0100f) {
        return false;
    }

    // Validate that all tag entries have sane offset + size
    const tag_Layout* tags = get_tag_table(profile);
    for (uint32_t i = 0; i < profile->tag_count; ++i) {
        uint32_t tag_offset = read_big_u32(tags[i].offset);
        uint32_t tag_size   = read_big_u32(tags[i].size);
        uint64_t tag_end    = (uint64_t)tag_offset + (uint64_t)tag_size;
        if (tag_size < 4 || tag_end > profile->size) {
            return false;
        }
    }

    return true;
}

// XYZType is technically variable sized, holding N XYZ triples. However, the only valid uses of
// the type are for tags/data that store exactly one triple.
typedef struct {
    uint8_t type     [4];
    uint8_t reserved [4];
    uint8_t X        [4];
    uint8_t Y        [4];
    uint8_t Z        [4];
} XYZ_Layout;

static bool read_tag_xyz(const skcms_ICCTag* tag, float* x, float* y, float* z) {
    const XYZ_Layout* xyzTag = NULL;
    if (tag &&
        tag->type == make_signature('X','Y','Z',' ') &&
        tag->size >= SAFE_SIZEOF(XYZ_Layout)) {
        xyzTag = (const XYZ_Layout*)tag->buf;
    }

    if (!xyzTag || !x || !y || !z) {
        return false;
    }

    *x = read_big_fixed(xyzTag->X);
    *y = read_big_fixed(xyzTag->Y);
    *z = read_big_fixed(xyzTag->Z);
    return true;
}

bool skcms_ToXYZD50(const skcms_ICCProfile* profile,
                               skcms_Matrix3x3* toXYZ) {
    if (!profile || !toXYZ) { return false; }
    skcms_ICCTag rXYZ, gXYZ, bXYZ;
    if (!skcms_GetTagBySignature(profile, make_signature('r', 'X', 'Y', 'Z'), &rXYZ) ||
        !skcms_GetTagBySignature(profile, make_signature('g', 'X', 'Y', 'Z'), &gXYZ) ||
        !skcms_GetTagBySignature(profile, make_signature('b', 'X', 'Y', 'Z'), &bXYZ)) {
        return false;
    }

    return read_tag_xyz(&rXYZ, &toXYZ->vals[0][0], &toXYZ->vals[1][0], &toXYZ->vals[2][0]) &&
           read_tag_xyz(&gXYZ, &toXYZ->vals[0][1], &toXYZ->vals[1][1], &toXYZ->vals[2][1]) &&
           read_tag_xyz(&bXYZ, &toXYZ->vals[0][2], &toXYZ->vals[1][2], &toXYZ->vals[2][2]);
}

typedef struct {
    uint8_t type          [4];
    uint8_t reserved_a    [4];
    uint8_t function_type [2];
    uint8_t reserved_b    [2];
    uint8_t parameters    [ ];  // 1, 3, 4, 5, or 7 s15.16 parameters, depending on function_type
} para_Layout;

// Unified representation of any 'curv' or 'para' tag data
typedef struct {
    skcms_TransferFunction parametric;
    const uint8_t*         table;
    uint32_t               table_size;
} Curve;

static bool read_curve_para(const uint8_t* buf, uint32_t size, Curve* curve) {
    if (size < SAFE_SIZEOF(para_Layout)) {
        return false;
    }

    const para_Layout* paraTag = (const para_Layout*)buf;

    enum { kG = 0, kGAB = 1, kGABC = 2, kGABCD = 3, kGABCDEF = 4 };
    uint16_t function_type = read_big_u16(paraTag->function_type);
    if (function_type > kGABCDEF) {
        return false;
    }

    static const uint32_t curve_bytes[] = { 4, 12, 16, 20, 28 };
    if (size < SAFE_SIZEOF(para_Layout) + curve_bytes[function_type]) {
        return false;
    }

    curve->table = NULL;
    curve->table_size = 0;
    curve->parametric.a = 1.0f;
    curve->parametric.b = 0.0f;
    curve->parametric.c = 0.0f;
    curve->parametric.d = 0.0f;
    curve->parametric.e = 0.0f;
    curve->parametric.f = 0.0f;
    curve->parametric.g = read_big_fixed(paraTag->parameters);

    switch (function_type) {
        case kGAB:
            curve->parametric.a = read_big_fixed(paraTag->parameters + 4);
            curve->parametric.b = read_big_fixed(paraTag->parameters + 8);
            if (curve->parametric.a == 0) {
                return false;
            }
            curve->parametric.d = -curve->parametric.b / curve->parametric.a;
            break;
        case kGABC:
            curve->parametric.a = read_big_fixed(paraTag->parameters + 4);
            curve->parametric.b = read_big_fixed(paraTag->parameters + 8);
            curve->parametric.e = read_big_fixed(paraTag->parameters + 12);
            if (curve->parametric.a == 0) {
                return false;
            }
            curve->parametric.d = -curve->parametric.b / curve->parametric.a;
            curve->parametric.f = curve->parametric.e;
            break;
        case kGABCD:
            curve->parametric.a = read_big_fixed(paraTag->parameters + 4);
            curve->parametric.b = read_big_fixed(paraTag->parameters + 8);
            curve->parametric.c = read_big_fixed(paraTag->parameters + 12);
            curve->parametric.d = read_big_fixed(paraTag->parameters + 16);
            break;
        case kGABCDEF:
            curve->parametric.a = read_big_fixed(paraTag->parameters + 4);
            curve->parametric.b = read_big_fixed(paraTag->parameters + 8);
            curve->parametric.c = read_big_fixed(paraTag->parameters + 12);
            curve->parametric.d = read_big_fixed(paraTag->parameters + 16);
            curve->parametric.e = read_big_fixed(paraTag->parameters + 20);
            curve->parametric.f = read_big_fixed(paraTag->parameters + 24);
            break;
    }
    return true;
}

typedef struct {
    uint8_t type          [4];
    uint8_t reserved      [4];
    uint8_t value_count   [4];
    uint8_t parameters    [ ];  // value_count parameters (8.8 if 1, uint16 (n*65535) if > 1)
} curv_Layout;

static bool read_curve_curv(const uint8_t* buf, uint32_t size, Curve* curve) {
    if (size < SAFE_SIZEOF(curv_Layout)) {
        return false;
    }

    const curv_Layout* curvTag = (const curv_Layout*)buf;

    uint32_t value_count = read_big_u32(curvTag->value_count);
    if (size < SAFE_SIZEOF(curv_Layout) + value_count * SAFE_SIZEOF(uint16_t)) {
        return false;
    }

    if (value_count < 2) {
        curve->table = NULL;
        curve->table_size = 0;
        curve->parametric.a = 1.0f;
        curve->parametric.b = 0.0f;
        curve->parametric.c = 0.0f;
        curve->parametric.d = 0.0f;
        curve->parametric.e = 0.0f;
        curve->parametric.f = 0.0f;
        if (value_count == 0) {
            // Empty tables are a shorthand for linear
            curve->parametric.g = 1.0f;
        } else {
            // Single entry tables are a shorthand for simple gamma
            curve->parametric.g = read_big_u16(curvTag->parameters) * (1.0f / 256.0f);
        }
    } else {
        memset(&curve->parametric, 0, SAFE_SIZEOF(curve->parametric));
        curve->table_size = value_count;
        curve->table = curvTag->parameters;
    }

    return true;
}

// Parses both curveType and parametricCurveType data
static bool read_curve(const uint8_t* buf, uint32_t size, Curve* curve) {
    if (!buf || size < 4 || !curve) {
        return false;
    }

    uint32_t type = read_big_u32(buf);
    if (type == make_signature('p', 'a', 'r', 'a')) {
        return read_curve_para(buf, size, curve);
    } else if (type == make_signature('c', 'u', 'r', 'v')) {
        return read_curve_curv(buf, size, curve);
    }

    return false;
}

bool skcms_GetTransferFunction(const skcms_ICCProfile* profile,
                                          skcms_TransferFunction* transferFunction) {
    if (!profile || !transferFunction) { return false; }
    skcms_ICCTag rTRC, gTRC, bTRC;
    // TODO: Skia code supported some of these being missing, with fallback to others!?
    if (!skcms_GetTagBySignature(profile, make_signature('r', 'T', 'R', 'C'), &rTRC) ||
        !skcms_GetTagBySignature(profile, make_signature('g', 'T', 'R', 'C'), &gTRC) ||
        !skcms_GetTagBySignature(profile, make_signature('b', 'T', 'R', 'C'), &bTRC)) {
        return false;
    }

    // For each TRC tag, check for either V4 parametric curve data, or special cases of
    // V2 curve data that encode a numerical gamma curve.
    Curve rCurve, gCurve, bCurve;
    if (!read_curve(rTRC.buf, rTRC.size, &rCurve) || rCurve.table ||
        !read_curve(gTRC.buf, gTRC.size, &gCurve) || gCurve.table ||
        !read_curve(bTRC.buf, bTRC.size, &bCurve) || bCurve.table) {
        return false;
    }

    if (0 != memcmp(&rCurve.parametric, &gCurve.parametric, SAFE_SIZEOF(rCurve.parametric)) ||
        0 != memcmp(&rCurve.parametric, &bCurve.parametric, SAFE_SIZEOF(rCurve.parametric))) {
        return false;
    }

    *transferFunction = rCurve.parametric;
    return true;
}

bool skcms_ApproximateTransferFunction(const skcms_ICCProfile* profile,
                                                  skcms_TransferFunction* fn,
                                                  float* max_error) {
    if (!profile || !fn) { return false; }
    skcms_ICCTag rTRC, gTRC, bTRC;
    if (!skcms_GetTagBySignature(profile, make_signature('r', 'T', 'R', 'C'), &rTRC) ||
        !skcms_GetTagBySignature(profile, make_signature('g', 'T', 'R', 'C'), &gTRC) ||
        !skcms_GetTagBySignature(profile, make_signature('b', 'T', 'R', 'C'), &bTRC)) {
        return false;
    }

    Curve curves[3];
    if (!read_curve(rTRC.buf, rTRC.size, &curves[0]) || !curves[0].table ||
        !read_curve(gTRC.buf, gTRC.size, &curves[1]) || !curves[1].table ||
        !read_curve(bTRC.buf, bTRC.size, &curves[2]) || !curves[2].table) {
        return false;
    }

    uint64_t n = (uint64_t)curves[0].table_size
               + (uint64_t)curves[1].table_size
               + (uint64_t)curves[2].table_size;
    if (n > INT_MAX) {
        return false;
    }

    uint64_t buf_size = 2 * n * SAFE_SIZEOF(float);

    if (buf_size != (size_t)buf_size) {
        return false;
    }
    float* data = malloc((size_t)buf_size);

    float* x = data;
    float* t = data + n;

    // Merge all channels' tables into a single array.
    for (int c = 0; c < 3; ++c) {
        for (uint32_t i = 0; i < curves[c].table_size; ++i) {
            *x++ = i / (curves[c].table_size - 1.0f);
            *t++ = read_big_u16(curves[c].table + 2 * i) * (1 / 65535.0f);
        }
    }

    x = data;
    t = data + n;

    bool result = skcms_TransferFunction_approximate(fn, x, t, (int)n, max_error);
    free(data);
    return result;
}

void skcms_GetTagByIndex(const skcms_ICCProfile* profile,
                                    uint32_t index,
                                    skcms_ICCTag* tag) {
    if (!profile || !profile->buffer || !tag) { return; }
    if (index > profile->tag_count) { return; }
    const tag_Layout* tags = get_tag_table(profile);
    tag->signature = read_big_u32(tags[index].signature);
    tag->size      = read_big_u32(tags[index].size);
    tag->buf       = read_big_u32(tags[index].offset) + profile->buffer;
    tag->type      = read_big_u32(tag->buf);
}

bool skcms_GetTagBySignature(const skcms_ICCProfile* profile,
                                        uint32_t signature,
                                        skcms_ICCTag* tag) {
    if (!profile || !profile->buffer || !tag) { return false; }
    const tag_Layout* tags = get_tag_table(profile);
    for (uint32_t i = 0; i < profile->tag_count; ++i) {
        if (read_big_u32(tags[i].signature) == signature) {
            tag->signature = signature;
            tag->size      = read_big_u32(tags[i].size);
            tag->buf       = read_big_u32(tags[i].offset) + profile->buffer;
            tag->type      = read_big_u32(tag->buf);
            return true;
        }
    }
    return false;
}
