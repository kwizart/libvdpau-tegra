/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
 * Copyright (c) 2013 Avionic Design GmbH
 * Copyright (c) 2013 Thierry Reding
 * Copyright (c) 2017 Dmitry Osipenko
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vdpau_tegra.h"

#define PIXBUF_GUARD_PATTERN    0xF5132803

static bool pixbuf_guard_disabled = true;

struct host1x_pixelbuffer *host1x_pixelbuffer_create(struct drm_tegra *drm,
                                                     unsigned width,
                                                     unsigned height,
                                                     unsigned pitch,
                                                     unsigned pitch_uv,
                                                     enum pixel_format format,
                                                     enum layout_format layout)
{
    struct host1x_pixelbuffer *pixbuf;
    uint32_t flags = 0;
    uint32_t bo_size;
    int ret;

    pixbuf = calloc(1, sizeof(*pixbuf));
    if (!pixbuf)
        return NULL;

    if (layout == PIX_BUF_LAYOUT_TILED_16x16) {
        pitch = ALIGN(pitch, 256);
        pitch_uv = ALIGN(pitch_uv, 256);
    }

    if (width * PIX_BUF_FORMAT_BYTES(format) > pitch) {
        host1x_error("Invalid pitch\n");
        return NULL;
    }

    if (format == PIX_BUF_FMT_YV12) {
        if (width * PIX_BUF_FORMAT_BYTES(format) / 2 > pitch_uv) {
            host1x_error("Invalid UV pitch\n");
            return NULL;
        }
    }

    pixbuf->pitch = pitch;
    pixbuf->pitch_uv = pitch_uv;
    pixbuf->width = width;
    pixbuf->height = height;
    pixbuf->format = format;
    pixbuf->layout = layout;

    if (layout == PIX_BUF_LAYOUT_TILED_16x16)
        height = ALIGN(height, 16);

    bo_size = pitch * height;

    if (!pixbuf_guard_disabled) {
        pixbuf->guard_offset[0] = bo_size;
        bo_size += PIXBUF_GUARD_AREA_SIZE;
    }

    ret = drm_tegra_bo_new(&pixbuf->bo, drm, flags, bo_size);
    if (ret < 0) {
        host1x_error("Failed to allocate BO size %u\n", bo_size);
        goto error_cleanup;
    }

    if (format == PIX_BUF_FMT_YV12) {
        bo_size = pitch_uv * height / 2;

        if (!pixbuf_guard_disabled) {
            pixbuf->guard_offset[1] = bo_size;
            pixbuf->guard_offset[2] = bo_size;
            bo_size += PIXBUF_GUARD_AREA_SIZE;
        }

        ret = drm_tegra_bo_new(&pixbuf->bos[1], drm, flags, bo_size);
        if (ret < 0) {
            host1x_error("Failed to allocate Cb BO size %u\n", bo_size);
            goto error_cleanup;
        }

        ret = drm_tegra_bo_new(&pixbuf->bos[2], drm, flags, bo_size);
        if (ret < 0){
            host1x_error("Failed to allocate Cr BO size %u\n", bo_size);
            goto error_cleanup;
        }
    }

    host1x_pixelbuffer_setup_guard(pixbuf);

    return pixbuf;

error_cleanup:
    drm_tegra_bo_unref(pixbuf->bos[0]);
    drm_tegra_bo_unref(pixbuf->bos[1]);
    drm_tegra_bo_unref(pixbuf->bos[2]);
    free(pixbuf);

    return NULL;
}

void host1x_pixelbuffer_free(struct host1x_pixelbuffer *pixbuf)
{
    drm_tegra_bo_unref(pixbuf->bos[0]);
    drm_tegra_bo_unref(pixbuf->bos[1]);
    drm_tegra_bo_unref(pixbuf->bos[2]);
    free(pixbuf);
}

int host1x_pixelbuffer_load_data(struct drm_tegra *drm,
                                 struct tegra_stream *stream,
                                 struct host1x_pixelbuffer *pixbuf,
                                 void *data,
                                 unsigned data_pitch,
                                 unsigned long data_size,
                                 enum pixel_format data_format,
                                 enum layout_format data_layout)
{
    struct host1x_pixelbuffer *tmp;
    bool blit = false;
    void *map;
    int ret;

    if (pixbuf->format != data_format)
        return -1;

    if (pixbuf->layout != data_layout)
        blit = true;

    if (pixbuf->pitch != data_pitch)
        blit = true;

    if (blit) {
        tmp = host1x_pixelbuffer_create(drm,
                                        pixbuf->width, pixbuf->height,
                                        data_pitch, 0, data_format,
                                        data_layout);
        if (!tmp)
            return -1;
    } else {
        tmp = pixbuf;
    }

    ret = drm_tegra_bo_map(tmp->bo, &map);
    if (ret < 0)
        return ret;

    memcpy(map, data, data_size);

    if (blit) {
        ret = host1x_gr2d_blit(stream, tmp, pixbuf,
                               0, 0, 0, 0,
                               pixbuf->width, pixbuf->height);
        host1x_pixelbuffer_free(tmp);
    }

    drm_tegra_bo_unmap(pixbuf->bo);

    return ret;
}

static int host1x_pixelbuffer_setup_bo_guard(struct drm_tegra_bo *bo,
                                             uint32_t guard_offset)
{
    volatile uint32_t *guard;
    unsigned i;
    int ret;

    ret = drm_tegra_bo_map(bo, (void**)&guard);
    if (ret < 0)
        return ret;

    guard = (void*)guard + guard_offset;

    for (i = 0; i < PIXBUF_GUARD_AREA_SIZE / 4; i++)
        guard[i] = PIXBUF_GUARD_PATTERN + i;

    ret = drm_tegra_bo_unmap(bo);
    if (ret < 0)
        return ret;

    return 0;
}

int host1x_pixelbuffer_setup_guard(struct host1x_pixelbuffer *pixbuf)
{
    unsigned i;
    int ret;

    if (pixbuf_guard_disabled)
        return 0;

    for (i = 0; i < PIX_BUF_FORMAT_PLANES_NB(pixbuf->format); i++) {
        ret = host1x_pixelbuffer_setup_bo_guard(pixbuf->bos[i],
                                                pixbuf->guard_offset[i]);
        if (ret < 0) {
            host1x_error("Pixbuf guard setup failed %d\n", ret);
            return ret;
        }
    }

    return 0;
}

static int host1x_pixelbuffer_check_bo_guard(struct host1x_pixelbuffer *pixbuf,
                                             struct drm_tegra_bo *bo,
                                             uint32_t guard_offset)
{
    volatile uint32_t *guard;
    bool smashed = false;
    uint32_t value;
    unsigned i;
    int ret;

    ret = drm_tegra_bo_map(bo, (void**)&guard);
    if (ret < 0)
        return ret;

    guard = (void*)guard + guard_offset;

    for (i = 0; i < PIXBUF_GUARD_AREA_SIZE / 4; i++) {
        value = guard[i];

        if (value != PIXBUF_GUARD_PATTERN + i) {
            host1x_error("Guard[%d of %d] smashed, 0x%08X != 0x%08X\n",
                         i, PIXBUF_GUARD_AREA_SIZE / 4 - 1,
                         value, PIXBUF_GUARD_PATTERN + i);
            smashed = true;
        }
    }

    if (smashed) {
        host1x_error("Pixbuf %p: width %u, height %u, pitch %u, format %u\n",
                     pixbuf, pixbuf->width, pixbuf->height,
                     pixbuf->pitch, pixbuf->format);
        abort();
    }

    ret = drm_tegra_bo_unmap(bo);
    if (ret < 0)
        return ret;

    return 0;
}

int host1x_pixelbuffer_check_guard(struct host1x_pixelbuffer *pixbuf)
{
    unsigned i;
    int ret;

    if (pixbuf_guard_disabled)
        return 0;

    for (i = 0; i < PIX_BUF_FORMAT_PLANES_NB(pixbuf->format); i++) {
        ret = host1x_pixelbuffer_check_bo_guard(pixbuf, pixbuf->bos[i],
                                                pixbuf->guard_offset[i]);
        if (ret < 0) {
            host1x_error("Pixbuf guard check failed %d\n", ret);
            return ret;
        }
    }

    return 0;
}

void host1x_pixelbuffer_disable_bo_guard(void)
{
    pixbuf_guard_disabled = true;
}
