/*
 * NVIDIA TEGRA 2 VDPAU backend
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef VDPAU_TEGRA_H
#define VDPAU_TEGRA_H

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <pixman.h>
#include <vdpau/vdpau_x11.h>

#include <libdrm/tegra_drm.h>
#include <libdrm/tegra.h>
#include <xf86drm.h>

#define NEED_REPLIES
#include <X11/Xlibint.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/dri2tokens.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xvlib.h>

#include "atomic.h"
#include "dri2.h"
#include "bitstream.h"
#include "tegra_stream.h"
#include "host1x.h"
#include "util_double_list.h"
#include "uapi/dma-buf.h"
#include "uapi/tegra-vde.h"

#if HAVE_VISIBILITY
#define EXPORTED __attribute__((__visibility__("default")))
#else
#define EXPORTED
#endif

#define TEGRA_VDPAU_INTERFACE_VERSION 1

#define MAX_DEVICES_NB                      1
#define MAX_DECODERS_NB                     1
#define MAX_MIXERS_NB                       16
#define MAX_SURFACES_NB                     256
#define MAX_PRESENTATION_QUEUE_TARGETS_NB   32
#define MAX_PRESENTATION_QUEUES_NB          128

#define SURFACE_VIDEO               (1 << 0)
#define SURFACE_YUV_UNCONVERTED     (1 << 1)
#define SURFACE_DATA_NEEDS_SYNC     (1 << 2)

#define PASSTHROUGH_DATA_SIZE   36

#define FOURCC_PASSTHROUGH_YV12     (('1' << 24) + ('2' << 16) + ('V' << 8) + 'Y')
#define FOURCC_PASSTHROUGH_XRGB565  (('1' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_XRGB8888 (('X' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_XBGR8888 (('X' << 24) + ('R' << 16) + ('G' << 8) + 'B')

#define __align_mask(value, mask)  (((value) + (mask)) & ~(mask))
#define ALIGN(value, alignment)    __align_mask(value, (typeof(value))((alignment) - 1))

#define ErrorMsg(fmt, args...) \
    fprintf(stderr, "%s:%d/%s(): " fmt, \
            __FILE__, __LINE__, __func__, ##args)

typedef struct {
    int atomic;
} atomic_t;

extern pthread_mutex_t global_lock;

typedef struct tegra_device {
    struct drm_tegra *drm;
    struct drm_tegra_channel *gr2d;
    struct tegra_stream *stream;
    Display *display;
    XvPortID xv_port;
    atomic_t refcnt;
    int screen;
    int vde_fd;
    int drm_fd;
} tegra_device;

typedef struct tegra_surface {
    tegra_device *dev;

    struct tegra_vde_h264_frame *frame;
    int32_t pic_order_cnt;

    pixman_image_t *pix;
    XvImage *xv_img;
    uint32_t flags;

    void *y_data;
    void *cb_data;
    void *cr_data;

    struct host1x_pixelbuffer *pixbuf;
    struct drm_tegra_bo *y_bo;
    struct drm_tegra_bo *cb_bo;
    struct drm_tegra_bo *cr_bo;
    struct drm_tegra_bo *aux_bo;

    uint32_t disp_width;
    uint32_t disp_height;

    VdpPresentationQueueStatus status;
    VdpTime first_presentation_time;
    VdpTime earliest_presentation_time;

    atomic_t refcnt;
    struct list_head list_item;
    pthread_cond_t idle_cond;
    pthread_mutex_t lock;

    bool idle_hack;
} tegra_surface;

typedef struct tegra_decoder {
    struct drm_tegra_bo *bitstream_bo;
    tegra_device *dev;
    bitstream_reader reader;
    int bitstream_data_fd;
    void *bitstream_data;
    int is_baseline_profile;
    uint32_t width;
    uint32_t height;
} tegra_decoder;

typedef struct tegra_mixer {
    VdpCSCMatrix csc_matrix;
    VdpColor bg_color;
} tegra_mixer;

typedef struct tegra_pqt {
    tegra_device *dev;
    tegra_surface *disp_surf;
    Drawable drawable;
    GC gc;
    atomic_t refcnt;
} tegra_pqt;

typedef struct tegra_pq {
    tegra_pqt *pqt;
    VdpColor background_color;

    struct list_head surf_list;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t disp_thread;
    pthread_t x11_thread;
    bool exit;
} tegra_pq;

tegra_device * get_device(VdpDevice device);

void ref_device(tegra_device *dev);

VdpStatus unref_device(tegra_device *dev);

tegra_decoder * get_decoder(VdpDecoder decoder);

void set_decoder(VdpDecoder decoder, tegra_decoder *dec);

tegra_mixer * get_mixer(VdpVideoMixer mixer);

void set_mixer(VdpVideoMixer mixer, tegra_mixer *mix);

tegra_surface * get_surface(VdpBitmapSurface surface);

void set_surface(VdpBitmapSurface surface, tegra_surface *surf);

tegra_pqt * get_presentation_queue_target(VdpPresentationQueueTarget target);

void set_presentation_queue_target(VdpPresentationQueueTarget target,
                                   tegra_pqt *pqt);

tegra_pq * get_presentation_queue(VdpPresentationQueue presentation_queue);

void set_presentation_queue(VdpPresentationQueue presentation_queue,
                            tegra_pq *pq);

uint32_t create_surface(tegra_device *dev,
                        uint32_t width,
                        uint32_t height,
                        VdpRGBAFormat rgba_format,
                        int output,
                        int video);

VdpStatus destroy_surface(tegra_surface *surf);

void ref_surface(tegra_surface *surf);

VdpStatus unref_surface(tegra_surface *surf);

int sync_dmabuf_write_start(int dmabuf_fd);

int sync_dmabuf_write_end(int dmabuf_fd);

int sync_dmabuf_read_start(int dmabuf_fd);

int sync_dmabuf_read_end(int dmabuf_fd);

enum frame_sync {
    READ_START,
    READ_END,
    WRITE_START,
    WRITE_END,
};

int sync_video_frame_dmabufs(tegra_surface *surf, enum frame_sync type);

VdpGetErrorString                                   vdp_get_error_string;
VdpGetProcAddress                                   vdp_get_proc_address;
VdpGetApiVersion                                    vdp_get_api_version;
VdpGetInformationString                             vdp_get_information_string;
VdpDeviceDestroy                                    vdp_device_destroy;
VdpGenerateCSCMatrix                                vdp_generate_csc_matrix;
VdpVideoSurfaceQueryCapabilities                    vdp_video_surface_query_capabilities;
VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities     vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities;
VdpVideoSurfaceCreate                               vdp_video_surface_create;
VdpVideoSurfaceDestroy                              vdp_video_surface_destroy;
VdpVideoSurfaceGetParameters                        vdp_video_surface_get_parameters;
VdpVideoSurfaceGetBitsYCbCr                         vdp_video_surface_get_bits_y_cb_cr;
VdpVideoSurfacePutBitsYCbCr                         vdp_video_surface_put_bits_y_cb_cr;
VdpOutputSurfaceQueryCapabilities                   vdp_output_surface_query_capabilities;
VdpOutputSurfaceQueryGetPutBitsNativeCapabilities   vdp_output_surface_query_get_put_bits_native_capabilities;
VdpOutputSurfaceQueryPutBitsIndexedCapabilities     vdp_output_surface_query_put_bits_indexed_capabilities;
VdpOutputSurfaceQueryPutBitsYCbCrCapabilities       vdp_output_surface_query_put_bits_y_cb_cr_capabilities;
VdpOutputSurfaceCreate                              vdp_output_surface_create;
VdpOutputSurfaceDestroy                             vdp_output_surface_destroy;
VdpOutputSurfaceGetParameters                       vdp_output_surface_get_parameters;
VdpOutputSurfaceGetBitsNative                       vdp_output_surface_get_bits_native;
VdpOutputSurfacePutBitsNative                       vdp_output_surface_put_bits_native;
VdpOutputSurfacePutBitsIndexed                      vdp_output_surface_put_bits_indexed;
VdpOutputSurfacePutBitsYCbCr                        vdp_output_surface_put_bits_y_cb_cr;
VdpBitmapSurfaceQueryCapabilities                   vdp_bitmap_surface_query_capabilities;
VdpBitmapSurfaceCreate                              vdp_bitmap_surface_create;
VdpBitmapSurfaceDestroy                             vdp_bitmap_surface_destroy;
VdpBitmapSurfaceGetParameters                       vdp_bitmap_surface_get_parameters;
VdpBitmapSurfacePutBitsNative                       vdp_bitmap_surface_put_bits_native;
VdpOutputSurfaceRenderOutputSurface                 vdp_output_surface_render_output_surface;
VdpOutputSurfaceRenderBitmapSurface                 vdp_output_surface_render_bitmap_surface;
VdpDecoderQueryCapabilities                         vdp_decoder_query_capabilities;
VdpDecoderCreate                                    vdp_decoder_create;
VdpDecoderDestroy                                   vdp_decoder_destroy;
VdpDecoderGetParameters                             vdp_decoder_get_parameters;
VdpDecoderRender                                    vdp_decoder_render;
VdpVideoMixerQueryFeatureSupport                    vdp_video_mixer_query_feature_support;
VdpVideoMixerQueryParameterSupport                  vdp_video_mixer_query_parameter_support;
VdpVideoMixerQueryParameterValueRange               vdp_video_mixer_query_parameter_value_range;
VdpVideoMixerQueryAttributeSupport                  vdp_video_mixer_query_attribute_support;
VdpVideoMixerQueryAttributeValueRange               vdp_video_mixer_query_attribute_value_range;
VdpVideoMixerSetFeatureEnables                      vdp_video_mixer_set_feature_enables;
VdpVideoMixerCreate                                 vdp_video_mixer_create;
VdpVideoMixerSetAttributeValues                     vdp_video_mixer_set_attribute_values;
VdpVideoMixerGetFeatureSupport                      vdp_video_mixer_get_feature_support;
VdpVideoMixerGetFeatureEnables                      vdp_video_mixer_get_feature_enables;
VdpVideoMixerGetParameterValues                     vdp_video_mixer_get_parameter_values;
VdpVideoMixerGetAttributeValues                     vdp_video_mixer_get_attribute_values;
VdpVideoMixerDestroy                                vdp_video_mixer_destroy;
VdpVideoMixerRender                                 vdp_video_mixer_render;
VdpPresentationQueueTargetDestroy                   vdp_presentation_queue_target_destroy;
VdpPresentationQueueCreate                          vdp_presentation_queue_create;
VdpPresentationQueueDestroy                         vdp_presentation_queue_destroy;
VdpPresentationQueueSetBackgroundColor              vdp_presentation_queue_set_background_color;
VdpPresentationQueueGetBackgroundColor              vdp_presentation_queue_get_background_color;
VdpPresentationQueueGetTime                         vdp_presentation_queue_get_time;
VdpPresentationQueueDisplay                         vdp_presentation_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle           vdp_presentation_queue_block_until_surface_idle;
VdpPresentationQueueQuerySurfaceStatus              vdp_presentation_queue_query_surface_status;
VdpPreemptionCallbackRegister                       vdp_preemption_callback_register;
VdpPresentationQueueTargetCreateX11                 vdp_presentation_queue_target_create_x11;

#endif // VDPAU_TEGRA_H
