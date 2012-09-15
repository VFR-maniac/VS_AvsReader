/*
  vsavsreader.c: AviSynth Script Reader for VapourSynth

  Copyright (C) 2012  Oka Motofumi

  Author: Oka Motofumi (chikuzen.mo at gmail dot com)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Libav; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/


#include <string.h>
#include <stdint.h>
#include <windows.h>

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"

#include "VapourSynth.h"


#define AVSC_DECLARE_FUNC(name) name##_func name
#define AVS_INTERFACE_25 2

typedef struct {
    VSVideoInfo vs_vi;
    int avs_version;
    int bitdepth;
    AVS_Clip *clip;
    AVS_ScriptEnvironment *env;
    const AVS_VideoInfo *avs_vi;
    HMODULE library;
    struct {
        AVSC_DECLARE_FUNC(avs_clip_get_error);
        AVSC_DECLARE_FUNC(avs_create_script_environment);
        AVSC_DECLARE_FUNC(avs_delete_script_environment);
        AVSC_DECLARE_FUNC(avs_get_error);
        AVSC_DECLARE_FUNC(avs_get_frame);
        AVSC_DECLARE_FUNC(avs_get_version);
        AVSC_DECLARE_FUNC(avs_get_video_info);
        AVSC_DECLARE_FUNC(avs_function_exists);
        AVSC_DECLARE_FUNC(avs_invoke);
        AVSC_DECLARE_FUNC(avs_bit_blt);
        AVSC_DECLARE_FUNC(avs_release_clip);
        AVSC_DECLARE_FUNC(avs_release_value);
        AVSC_DECLARE_FUNC(avs_release_video_frame);
        AVSC_DECLARE_FUNC(avs_take_clip);
    } func;
} avsr_hnd_t;
#undef AVSC_DECLARE_FUNC


#define LOAD_AVS_FUNC(name, continue_on_fail)\
{\
    ah->func.name = (void*)GetProcAddress(ah->library, #name);\
    if( !continue_on_fail && !ah->func.name )\
        goto fail;\
}

static int load_avisynth_dll(avsr_hnd_t *ah)
{
    ah->library = LoadLibrary("avisynth");
    if(!ah->library) {
        return -1;
    }

    LOAD_AVS_FUNC(avs_clip_get_error, 0 );
    LOAD_AVS_FUNC(avs_create_script_environment, 0);
    LOAD_AVS_FUNC(avs_delete_script_environment, 1);
    LOAD_AVS_FUNC(avs_get_error, 1);
    LOAD_AVS_FUNC(avs_get_frame, 0);
    LOAD_AVS_FUNC(avs_get_version, 0);
    LOAD_AVS_FUNC(avs_get_video_info, 0);
    LOAD_AVS_FUNC(avs_function_exists, 0);
    LOAD_AVS_FUNC(avs_invoke, 0);
    LOAD_AVS_FUNC(avs_bit_blt, 0);
    LOAD_AVS_FUNC(avs_release_clip, 0);
    LOAD_AVS_FUNC(avs_release_value, 0);
    LOAD_AVS_FUNC(avs_release_video_frame, 0);
    LOAD_AVS_FUNC(avs_take_clip, 0);

    return 0;

fail:
    FreeLibrary(ah->library);
    return -1;
}
#undef LOAD_AVS_FUNC


static int get_avisynth_version(avsr_hnd_t *ah)
{
    if (!ah->func.avs_function_exists(ah->env, "VersionNumber")) {
        return 0;
    }

    AVS_Value ver = ah->func.avs_invoke(ah->env, "VersionNumber",
                                        avs_new_value_array(NULL, 0), NULL);
    if (avs_is_error(ver) || !avs_is_float(ver)) {
        return 0;
    }

    int version = (int)(avs_as_float(ver) * 100 + 0.5);
    ah->func.avs_release_value(ver);

    return version;
}


static AVS_Value import_avs(avsr_hnd_t *ah, const char *script)
{
    AVS_Value arg = avs_new_value_string(script);
    return ah->func.avs_invoke(ah->env, "Import", arg, NULL);
}


static AVS_Value
invoke_avs_filter(avsr_hnd_t *ah, AVS_Value before, const char *filter)
{
    ah->func.avs_release_clip(ah->clip);
    AVS_Value after = ah->func.avs_invoke(ah->env, filter, before, NULL);
    ah->func.avs_release_value(before);
    ah->clip = ah->func.avs_take_clip(after, ah->env);
    ah->avs_vi = ah->func.avs_get_video_info(ah->clip);
    return after;
}


static AVS_Value initialize_avisynth(avsr_hnd_t *ah, const char *script)
{
    if (load_avisynth_dll(ah)) {
        return avs_void;
    }

    ah->env = ah->func.avs_create_script_environment(AVS_INTERFACE_25);
    if (ah->func.avs_get_error && ah->func.avs_get_error(ah->env)) {
        return avs_void;
    }

    ah->avs_version = get_avisynth_version(ah);

    AVS_Value res = avs_void;
    res = import_avs(ah, script);
    if (avs_is_error(res) || !avs_defined(res)) {
        return res;
    }

#ifdef BLAME_THE_FLUFF
    AVS_Value mt_test = ah->func.avs_invoke(ah->env, "GetMTMode",
                                            avs_new_value_bool(0), NULL);
    int mt_mode = avs_is_int(mt_test) ? avs_as_int(mt_test) : 0;
    ah->func.avs_release_value(mt_test);
    if (mt_mode > 0 && mt_mode < 5) {
        AVS_Value temp = ah->func.avs_invoke(ah->env, "Distributor", res, NULL);
        ah->func.avs_release_value(res);
        res = temp;
    }
#endif

    ah->clip = ah->func.avs_take_clip(res, ah->env);
    ah->avs_vi = ah->func.avs_get_video_info(ah->clip);

    if (!avs_has_video(ah->avs_vi)) {
        goto invalid;
    }

    if (avs_is_yuy2(ah->avs_vi) && ah->avs_version >= 260) {
        res = invoke_avs_filter(ah, res, "ConvertToYV16");
    }

    if (avs_is_rgb24(ah->avs_vi)) {
        res = invoke_avs_filter(ah, res, "ConvertToRGB32");
    }

#ifndef BLAME_MYRSLOIK_I_DO_NOT_WANT_TO_LOST_ALPHA
    if (avs_is_rgb32(ah->avs_vi)) {
        goto invalid;
    }
#endif
#ifndef BLAME_MYRSLOYK_I_DO_NOT_WANT_TO_USE_AVS26
    if (avs_is_yuy2(ah->avs_vi)) {
        goto invalid;
    }
#endif

    if (ah->bitdepth == 8) {
        return res;
    }

    if (!avs_is_planar(ah->avs_vi) ||
        avs_is_yv411(ah->avs_vi) ||
        (avs_is_y8(ah->avs_vi) && ah->bitdepth != 16) ||
        ((avs_is_yv24(ah->avs_vi) || avs_is_y8(ah->avs_vi)) && (ah->avs_vi->width & 1)) ||
        ((avs_is_yv16(ah->avs_vi) || avs_is_yv12(ah->avs_vi)) && (ah->avs_vi->width & 3))) {
        goto invalid;
    }

    return res;

invalid:
    ah->func.avs_release_value(res);
    return avs_void;
}


#define PIX_OR_DEPTH(pix, depth) (((uint64_t)pix << 8) | (depth))

static VSFormat *set_vs_format(avsr_hnd_t *ah)
{
    VSFormat *format = (VSFormat *)calloc(sizeof(VSFormat), 1);
    if (!format) {
        return NULL;
    }

    uint64_t val = PIX_OR_DEPTH(ah->avs_vi->pixel_type, ah->bitdepth);
    struct {
        uint64_t avs_pix_type;
        int id;
        char *name;
        int color_family;
        int subsample_w;
        int subsample_h;
    } table[] = {
#ifdef BLAME_MYRSLOIK_I_DO_NOT_WANT_TO_LOST_ALPHA
        { PIX_OR_DEPTH(AVS_CS_BGR32, 8), pfCompatBgr32, "COMPAT_BGR32", cmCompat, 0, 0 },
#endif
#ifdef BLAME_MYRSLOYK_I_DO_NOT_WANT_TO_USE_AVS26
        { PIX_OR_DEPTH(AVS_CS_YUY2,  8), pfCompayYuy2, "COMPAY_YUY2", cmCompat, 1, 0 },
#endif
        { PIX_OR_DEPTH(AVS_CS_YV24,  8), pfYUV444P8,   "YUV444P8",  cmYuv,  0, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV24,  9), pfYUV444P9,   "YUV444P9",  cmYuv,  0, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV24, 10), pfYUV444P10,  "YUV444P10", cmYuv,  0, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV24, 16), pfYUV444P16,  "YUV444P16", cmYuv,  0, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV16,  8), pfYUV422P8,   "YUV422P8",  cmYuv,  1, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV16,  9), pfYUV422P9,   "YUV422P9",  cmYuv,  1, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV16, 10), pfYUV422P10,  "YUV422P10", cmYuv,  1, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV16, 16), pfYUV422P16,  "YUV422P16", cmYuv,  1, 0 },
        { PIX_OR_DEPTH(AVS_CS_YV411, 8), pfYUV411P8,   "YUV411P8",  cmYuv,  2, 0 },
        { PIX_OR_DEPTH(AVS_CS_I420,  8), pfYUV420P8,   "YUV420P8",  cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_I420,  9), pfYUV420P9,   "YUV420P9",  cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_I420, 10), pfYUV420P10,  "YUV420P10", cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_I420, 16), pfYUV420P16,  "YUV420P16", cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_YV12,  8), pfYUV420P8,   "YUV420P8",  cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_YV12,  9), pfYUV420P9,   "YUV420P9",  cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_YV12, 10), pfYUV420P10,  "YUV420P10", cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_YV12, 16), pfYUV420P16,  "YUV420P16", cmYuv,  1, 1 },
        { PIX_OR_DEPTH(AVS_CS_Y8,    8), pfGray8,      "GRAY8",     cmGray, 0, 0 },
        { PIX_OR_DEPTH(AVS_CS_Y8,   16), pfGray16,     "GRAY16",    cmGray, 0, 0 },
        { val, 0 }
    };

    int i;
    for (i = 0; table[i].avs_pix_type != val; i++);
    if (table[i].id == 0) {
        free(format);
        return NULL;
    }

    format->id = table[i].id;
    strcpy(format->name, table[i].name);
    format->colorFamily = table[i].color_family;
    format->bitsPerSample = ah->bitdepth;
    format->bytesPerSample = (ah->bitdepth + 7) / 8;
    format->subSamplingW = table[i].subsample_w;
    format->subSamplingH = table[i].subsample_h;
    format->numPlanes = avs_is_interleaved(ah->avs_vi) ? 1 : 3;

    return format;

}
#undef PIX_OR_DEPTH

static int set_vs_videoinfo(avsr_hnd_t *ah)
{
    ah->vs_vi.format = set_vs_format(ah);
    if (!ah->vs_vi.format) {
        return -1;
    }

    ah->vs_vi.fpsNum = ah->avs_vi->fps_numerator;
    ah->vs_vi.fpsDen = ah->avs_vi->fps_denominator;
    ah->vs_vi.width = ah->bitdepth > 8 ? ah->avs_vi->width >> 1 :
                                         ah->avs_vi->width;
    ah->vs_vi.height = ah->avs_vi->height;
    ah->vs_vi.numFrames = ah->avs_vi->num_frames;

    return 0;
}

static void close_avisynth_dll(avsr_hnd_t *ah)
{
    if (ah->clip) {
        ah->func.avs_release_clip(ah->clip);
    }

    if (ah->func.avs_delete_script_environment) {
        ah->func.avs_delete_script_environment(ah->env);
    }

    FreeLibrary(ah->library);
}


static void close_handler(avsr_hnd_t *ah)
{
    if (!ah) {
        return;
    }

    if (ah->vs_vi.format) {
        free((void *)ah->vs_vi.format);
        ah->vs_vi.format = NULL;
    }
    if (ah->library) {
        close_avisynth_dll(ah);
    }
    free(ah);
    ah = NULL;
}


static avsr_hnd_t *avsr_init(const char *script, int bitdepth)
{
    avsr_hnd_t *ah = (avsr_hnd_t *)calloc(sizeof(avsr_hnd_t), 1);
    if (!ah) {
        return NULL;
    }

    ah->bitdepth = bitdepth;
    AVS_Value res = initialize_avisynth(ah, script);
    if (!avs_is_clip(res)) {
        close_handler(ah);
        return NULL;
    }

    ah->func.avs_release_value(res);

    if (set_vs_videoinfo(ah)) {
        close_handler(ah);
        return NULL;
    }

    return ah;

}


static void __stdcall avsr_close(void *instance_data, VSCore *core,
                                 const VSAPI *vsapi)
{
    avsr_hnd_t *ah = (avsr_hnd_t *)instance_data;
    close_handler(ah);
}


static void __stdcall vs_init(VSMap *in, VSMap *out, void **instance_data,
                              VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    avsr_hnd_t *ah = (avsr_hnd_t *)*instance_data;
    vsapi->setVideoInfo(&ah->vs_vi, node);
}


static const VSFrameRef * __stdcall
avsr_get_frame(int n, int activation_reason, void **instance_data,
               void **frame_data, VSFrameContext *frame_ctx, VSCore *core,
               const VSAPI *vsapi)
{
    if (activation_reason != arInitial) {
        return NULL;
    }

    avsr_hnd_t *ah = (avsr_hnd_t *)*instance_data;

    int frame_number = n;
    if (n >= ah->avs_vi->num_frames) {
        frame_number = ah->avs_vi->num_frames - 1;
    }

    AVS_VideoFrame *src = ah->func.avs_get_frame(ah->clip, frame_number);
    if (ah->func.avs_clip_get_error(ah->clip)) {
        return NULL;
    }
    VSFrameRef *dst = vsapi->newVideoFrame(ah->vs_vi.format, ah->vs_vi.width,
                                           ah->vs_vi.height, NULL, core);

    VSMap *props = vsapi->getFramePropsRW(dst);
    vsapi->propSetInt(props, "_DurationNum", ah->avs_vi->fps_denominator * n, 0);
    vsapi->propSetInt(props, "_DurationDen", ah->avs_vi->fps_numerator, 0);

    const int plane[] = {AVS_PLANAR_Y, AVS_PLANAR_U, AVS_PLANAR_V};
    for (int i = 0, time = ah->vs_vi.format->numPlanes; i < time; i++) {
        ah->func.avs_bit_blt(ah->env,
                             vsapi->getWritePtr(dst, i),
                             vsapi->getStride(dst, i),
                             avs_get_read_ptr_p(src, plane[i]),
                             avs_get_pitch_p(src, plane[i]),
                             avs_get_row_size_p(src, plane[i]),
                             avs_get_height_p(src, plane[i]));
    }

    ah->func.avs_release_video_frame(src);

    return dst;
}


static void __stdcall create_source(const VSMap *in, VSMap *out, void *user_data,
                                    VSCore *core, const VSAPI *vsapi)
{
    int err;

    const char *script = vsapi->propGetData(in, "script", 0, 0);

    int bitdepth = vsapi->propGetInt(in, "bitdepth", 0, &err);
    if (err) {
        bitdepth = 8;
    }
    if (bitdepth != 8 && bitdepth != 9 && bitdepth != 10 && bitdepth != 16) {
        vsapi->setError(out, "Source: Invalid bitdepth was specified");
        return;
    }

    avsr_hnd_t *ah = avsr_init(script, bitdepth);
    if (!ah) {
        vsapi->setError(out, "Source: avisynth initialize failed");
        return;
    }

    const VSNodeRef *node = vsapi->createFilter(in, out, "Source", vs_init,
                                                avsr_get_frame, avsr_close,
                                                fmSerial, 0, ah, core);
    vsapi->propSetNode(out, "clip", node, 0);

}


EXTERN_C __declspec(dllexport) void __stdcall VapourSynthPluginInit(
    VSConfigPlugin f_config, VSRegisterFunction f_register, VSPlugin *plugin)
{
    f_config("chikuzen.does.not.have.his.own.domain.avsr", "avsr",
             "AviSynth Script Reader for VapourSynth", VAPOURSYNTH_API_VERSION,
             1, plugin);
    f_register("Source", "script:data;bitdepth:int:opt;", create_source, NULL,
               plugin);
}