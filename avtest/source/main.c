#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <debug.h>
#include <stdio.h>
#include <usb/usbmain.h>
#include <ppc/timebase.h>
#include <xetypes.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>
#include <network/network.h>

#include <sys/time.h>
#include <time/time.h>

#include <byteswap.h>

#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <xenos/xenos.h>

 #include <signal.h>

typedef unsigned int DWORD;
#include "hlsl/yuv_ps.h"
#include "hlsl/yuv_vs.h"

static struct XenosVertexBuffer *vb = NULL;
static struct XenosDevice * g_pVideoDevice = NULL;
static struct XenosShader * g_pVertexShader = NULL;
static struct XenosShader * g_pPixelShader = NULL;
static struct XenosDevice _xe;

const char filename[] = "uda:/video.mkv";

typedef struct DrawVerticeFormats {
    float x, y, z, w;
    float u, v;
} DrawVerticeFormats;

typedef struct AVSurface{
    struct XenosSurface * surface;
    unsigned char * data;
    uint32_t pitch;
} AVSurface;

typedef struct YUVSurface{
    AVSurface Y;
    AVSurface U;
    AVSurface V;
} YUVSurface;


void LockYUVSurface(YUVSurface * yuv){
    if(yuv){
        yuv->Y.data = (unsigned char *)Xe_Surface_LockRect(g_pVideoDevice, yuv->Y.surface, 0, 0, 0, 0, XE_LOCK_WRITE);
        yuv->U.data = (unsigned char *)Xe_Surface_LockRect(g_pVideoDevice, yuv->U.surface, 0, 0, 0, 0, XE_LOCK_WRITE);
        yuv->V.data = (unsigned char *)Xe_Surface_LockRect(g_pVideoDevice, yuv->V.surface, 0, 0, 0, 0, XE_LOCK_WRITE);
        
        yuv->Y.pitch = yuv->Y.surface->wpitch;
        yuv->U.pitch = yuv->U.surface->wpitch;
        yuv->V.pitch = yuv->V.surface->wpitch;
    }
}

void UnlockYUVSurface(YUVSurface * yuv){
    if(yuv){
        Xe_Surface_Unlock(g_pVideoDevice, yuv->Y.surface);
        Xe_Surface_Unlock(g_pVideoDevice, yuv->U.surface);
        Xe_Surface_Unlock(g_pVideoDevice, yuv->V.surface);
    }
}

void CreateYUVSurface(YUVSurface * yuv, int width, int height){
    if(yuv){
        
        yuv->Y.surface = Xe_CreateTexture(g_pVideoDevice, width, height, 1, XE_FMT_8, 0);
        yuv->U.surface = Xe_CreateTexture(g_pVideoDevice, width/2, height/2, 1, XE_FMT_8, 0);
        yuv->V.surface = Xe_CreateTexture(g_pVideoDevice, width/2, height/2, 1, XE_FMT_8, 0);
        
        printf("Create surface %d - %d...\r\n",width,height);
    }
}

void SetYuvSurface(YUVSurface * yuv){
    if(yuv){
        Xe_SetTexture(g_pVideoDevice, 0, yuv->Y.surface);
        Xe_SetTexture(g_pVideoDevice, 1, yuv->U.surface);
        Xe_SetTexture(g_pVideoDevice, 2, yuv->V.surface);
    }
}


void video_init(){
    xenos_init(VIDEO_MODE_HDMI_720P);
    g_pVideoDevice = &_xe;
    Xe_Init(g_pVideoDevice);

    Xe_SetRenderTarget(g_pVideoDevice, Xe_GetFramebufferSurface(g_pVideoDevice));

    static const struct XenosVBFFormat vbf = {
        2,
        {
            {XE_USAGE_POSITION, 0, XE_TYPE_FLOAT4},
            {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
        }
    };
    
    g_pPixelShader = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) g_xps_PSmain);
    Xe_InstantiateShader(g_pVideoDevice, g_pPixelShader, 0);

    g_pVertexShader = Xe_LoadShaderFromMemory(g_pVideoDevice, (void*) g_xvs_VSmain);
    Xe_InstantiateShader(g_pVideoDevice, g_pVertexShader, 0);
    Xe_ShaderApplyVFetchPatches(g_pVideoDevice, g_pVertexShader, 0, &vbf);

    edram_init(g_pVideoDevice);
    
    float x = -1.0f;
    float y = 1.0f;
    float w = 4.0f;
    float h = 4.0f;
    
    float ScreenUv[4] = {0.f, 1.0f, 1.0f, 0.f};
    
    vb = Xe_CreateVertexBuffer(g_pVideoDevice, 3 * sizeof (DrawVerticeFormats));
    DrawVerticeFormats *Rect = Xe_VB_Lock(g_pVideoDevice, vb, 0, 3 * sizeof (DrawVerticeFormats), XE_LOCK_WRITE);
    {
        ScreenUv[1] = ScreenUv[1]*2;
        ScreenUv[2] = ScreenUv[2]*2;

        // top left
        Rect[0].x = x;
        Rect[0].y = y;
        Rect[0].u = ScreenUv[0];
        Rect[0].v = ScreenUv[3];

        // bottom left
        Rect[1].x = x;
        Rect[1].y = y - h;
        Rect[1].u = ScreenUv[0];
        Rect[1].v = ScreenUv[2];

        // top right
        Rect[2].x = x + w;
        Rect[2].y = y;
        Rect[2].u = ScreenUv[1];
        Rect[2].v = ScreenUv[3];

        int i = 0;
        for (i = 0; i < 3; i++) {
            Rect[i].z = 0.0;
            Rect[i].w = 1.0;
        }
    }
    Xe_VB_Unlock(g_pVideoDevice, vb);

    Xe_InvalidateState(g_pVideoDevice);
    Xe_SetClearColor(g_pVideoDevice, 0);
}

YUVSurface vSurface;

void video_update(){
    // Reset states
    Xe_InvalidateState(g_pVideoDevice);
    Xe_SetClearColor(g_pVideoDevice, 0x88888888);
    
    // Select stream and shaders
    SetYuvSurface(&vSurface);
    Xe_SetCullMode(g_pVideoDevice, XE_CULL_NONE);
    Xe_SetStreamSource(g_pVideoDevice, 0, vb, 0, sizeof (DrawVerticeFormats));
    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_PIXEL, g_pPixelShader, 0);
    Xe_SetShader(g_pVideoDevice, SHADER_TYPE_VERTEX, g_pVertexShader, 0);

    // Draw
    Xe_DrawPrimitive(g_pVideoDevice, XE_PRIMTYPE_TRIANGLELIST, 0, 1);

    // Resolve
    Xe_Resolve(g_pVideoDevice);
    //while (!Xe_IsVBlank(g_pVideoDevice));
    Xe_Sync(g_pVideoDevice);
}

static void ShowFPS() {
    static unsigned long lastTick = 0;
    static int frames = 0;
    unsigned long nowTick;
    frames++;
    nowTick = mftb() / (PPC_TIMEBASE_FREQ / 1000);
    if (lastTick + 1000 <= nowTick) {

        printf("%d fps\r\n", frames);

        frames = 0;
        lastTick = nowTick;
    }
}
struct SwsContext * pSWSContext;

DECLARE_ALIGNED(16,uint8_t,audio_buf1)[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
DECLARE_ALIGNED(16,uint8_t,audio_buf2)[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
uint8_t *audio_buf;
unsigned int audio_buf_size; /* in bytes */


void decode_audio(AVCodecContext *pVideoCodecCtx,AVFrame *pFrame,AVPacket *packet){
    int audio_size = sizeof(audio_buf1);
    int len = avcodec_decode_audio3(pVideoCodecCtx,(int16_t *)audio_buf1,&audio_size,packet);
    
    if(len<0){
        return;
    }
    
    uint32_t * p = (uint32_t *) audio_buf1;

    int i = 0;
    for (i = 0; i < audio_size; i++) {
        p[i] = bswap_32(p[i]);
    }

    while(xenon_sound_get_unplayed()>(AVCODEC_MAX_AUDIO_FRAME_SIZE)) 
        udelay(50);
    
    xenon_sound_submit(p, AVCODEC_MAX_AUDIO_FRAME_SIZE);
}

void decode_video(AVCodecContext *pVideoCodecCtx,AVFrame *pFrame,AVPacket *packet){
    ShowFPS();
    return;
    
    int frameFinished;
    // Decode video frame
    avcodec_decode_video2(pVideoCodecCtx, pFrame, &frameFinished, packet);

    if(frameFinished){

        LockYUVSurface(&vSurface);

        AVPicture pict;
        pict.data[0] = vSurface.Y.data;
        pict.data[1] = vSurface.U.data;
        pict.data[2] = vSurface.V.data;

        pict.linesize[0] = vSurface.Y.pitch;
        pict.linesize[1] = vSurface.U.pitch;
        pict.linesize[2] = vSurface.V.pitch;

        //Scale it ..
        sws_scale(
            pSWSContext,  pFrame->data, 
            pFrame->linesize, 0, 
            pVideoCodecCtx->height, 
            pict.data, pict.linesize
        );

        UnlockYUVSurface(&vSurface);

        //
        video_update();

        ShowFPS();

        network_poll();
    }
}

int main() {
    AVFormatContext *pFormatCtx = NULL;
    int i, videoStream, audioStream;
    AVCodecContext *pVideoCodecCtx,*pAudioCodecCtx;
    AVCodec *pAudioCodec;
    AVCodec *pVideoCodec;
    AVFrame *pFrame;
    AVPacket packet;
    int frameFinished;
    int64_t start,end;
    
    xenon_make_it_faster(XENON_SPEED_FULL);
        
    usb_init();
    usb_do_poll();
    
    network_init();
    network_print_config();
    httpd_start();
    
    video_init();
    
    xenon_sound_init();
    
    av_log_set_level(AV_LOG_DEBUG);

    // Register all formats and codecs
    av_register_all();

    // Open video file
    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) != 0) {
        printf(" Couldn't open file\r\n");
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf(" Couldn't find stream information\r\n");
        return -1;
    }

    // Dump information about file onto standard error
    av_dump_format(pFormatCtx, 0, filename, 0);

//------------------------------------------------------------------------------    
// Get the video Stream
//------------------------------------------------------------------------------
    {
        // Find the first video stream
        videoStream = -1;
        for (i = 0; i < pFormatCtx->nb_streams; i++)
            if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStream = i;
                break;
            }
        if (videoStream == -1) {
            printf(" Didn't find a video stream\r\n");
            return -1; // Didn't find a video stream
        }

        // Get a pointer to the codec context for the video stream
        pVideoCodecCtx = pFormatCtx->streams[videoStream]->codec;

        // Find the decoder for the video stream
        pVideoCodec = avcodec_find_decoder(pVideoCodecCtx->codec_id);
        if (pVideoCodec == NULL) {
            printf("Codec not found\n");
            return -1; // Codec not found
        }

        // Open codec
        if (avcodec_open2(pVideoCodecCtx, pVideoCodec, NULL) < 0)
        {
            printf("Could not open codec\n");
            return -1; // Could not open codec
        }
    }
    
//------------------------------------------------------------------------------    
// Get the Audio Stream
//------------------------------------------------------------------------------
    {
        audioStream = -1;
        for (i = 0; i < pFormatCtx->nb_streams; i++)
            if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStream = i;
                break;
            }
        if (audioStream == -1) {
            printf(" Didn't find an audio stream\r\n");
            return -1; // Didn't find an audio stream
        }

        // Get a pointer to the codec context for the video stream
        pAudioCodecCtx = pFormatCtx->streams[audioStream]->codec;

        pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
        if (pAudioCodec == NULL) {
            printf("Audio codec not found\n");
            return -1; // Codec not found
        }

        // Open codec
        if (avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL) < 0)
        {
            printf("Could not open audio codec\n");
            return -1; // Could not open codec
        }
    }

    // Allocate video frame
    pFrame = avcodec_alloc_frame();

    i = 0;
    
    start = mftb();
    
    CreateYUVSurface(&vSurface,pVideoCodecCtx->width, pVideoCodecCtx->height);
    
    //Create Scale contexte
    pSWSContext = sws_getContext(
            pVideoCodecCtx->width, 
            pVideoCodecCtx->height, 
            pVideoCodecCtx->pix_fmt, 
            pVideoCodecCtx->width, 
            pVideoCodecCtx->height, 
            PIX_FMT_YUV420P, SWS_POINT, 0, 0, 0
    );
    
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // Is this a packet from the video stream?
        if (packet.stream_index == videoStream) {
            decode_video(pVideoCodecCtx,pFrame,&packet);
        }
        else if(packet.stream_index == audioStream){
            // we sync with audio ...
            // decode_audio(pVideoCodecCtx,pFrame,&packet);
        }
        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);
    }
    
    end = mftb();
    
    printf("time elapsed : %d\r\n",tb_diff_msec(end,start));

    // Free the YUV frame
    av_free(pFrame);

    // Close the codec
    avcodec_close(pVideoCodecCtx);

    // Close the video file
    av_close_input_file(pFormatCtx);

    return 0;
}
// libxenon miss
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <debug.h>
#include <stdio.h>
#include <usb/usbmain.h>
#include <ppc/timebase.h>
#include <xetypes.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>

#include <sys/time.h>
#include <time/time.h>

#include <byteswap.h>

#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <xenos/xenos.h>

#include <limits.h>
// libxenon miss
#include <sys/time.h>
#include <time/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time/time.h>
#include <xenon_smc/xenon_smc.h>

int stat(const char * __restrict path, struct stat * __restrict buf) {
    int fd = -1;
    fd = open(path, O_RDONLY);

    if (fd) {
        return fstat(fd, buf);
    }
    return ENOENT; // file doesn't exist
}

void usleep(int s) {
    udelay(s);
}
#include <math.h>

long long llrint(double x) {
    union {
        double d;
        uint64_t u;
    } u = {x};

    uint64_t absx = u.u & 0x7fffffffffffffffULL;

    // handle x is zero, large, or NaN simply convert to long long and return
    if (absx >= 0x4330000000000000ULL) {
        long long result = (long long) x; //set invalid if necessary

        //Deal with overflow cases
        if (x < (double) LONG_LONG_MIN)
            return LONG_LONG_MIN;

        // Note: float representation of LONG_LONG_MAX likely inexact,
        //		  which is why we do >= here
        if (x >= -((double) LONG_LONG_MIN))
            return LONG_LONG_MAX;

        return result;
    }

    // copysign( 0x1.0p52, x )
    u.u = (u.u & 0x8000000000000000ULL) | 0x4330000000000000ULL;

    //round according to current rounding mode
    x += u.d;
    x -= u.d;

    return (long long) x;
}

int getrusage(int who, void * r_usage) {
    printf("getrusage\r\n");
    return -1;
};

int av_get_cpu_flags(void)
{
    sigset_t ens1,ens2;
    sigemptyset(&ens1);
    sigaddset(&ens1,SIGINT);
    sigaddset(&ens1,SIGQUIT);
    sigaddset(&ens1,SIGUSR1);

    /* Mise en place du masquage des signaux de cet ens. */
    sigprocmask(SIG_SETMASK,&ens1,(sigset_t *)0);
    
    printf("av_get_cpu_flags\r\n");
    
    return AV_CPU_FLAG_ALTIVEC;
    return 0;
}