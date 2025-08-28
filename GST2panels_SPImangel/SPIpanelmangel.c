// sk6805_gst_spi.c
// Pi Zero 2 W: drive 2x(13x9) SK6805 panels as a 26x9 matrix via SPI using GStreamer input.
// Physical order per row: first 13 left->right, then 13 right->left; origin at bottom-left.

#define _GNU_SOURCE
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#define W 26
#define H 9
#define LED_COUNT (W*H)

#define PW 13
#define PH 9
#define PANEL_LEDS (PW*PH)

#define GAMMA 1.2f
#define BRIGHTNESS 32 // scale 0..255

// Optional wiring tweaks for the *right* panel only
#define RIGHT_MIRROR_X   0   // 1 if the right panel is mirrored horizontally
#define RIGHT_ROTATE_180 0   // 1 if the right panel is rotated 180°

#define SPI_DEV     "/dev/spidev0.0"
#define SPI_SPEED   3200000
#define LATCH_US    300
#define FRAME_INTERVAL_US 5000   // ~200 Hz output

static volatile int running = 1;

// Shared latest RGB frame (top-left origin, RGB order)
static uint8_t rgb_latest[H][W][3];
static pthread_mutex_t rgb_mutex = PTHREAD_MUTEX_INITIALIZER;

static int spi_fd = -1;
static uint32_t LUT[256];
static uint8_t bLUT[256];

// Build 8->32 bit LUT using 1->1110, 0->1000 at 3.2 MHz
static void build_lut(){
    for(int b=0;b<256;b++){
        uint32_t out=0;
        for(int i=7;i>=0;i--){
            uint32_t code = ((b>>i)&1)? 0b1110u : 0b1000u;
            out = (out<<4)|code;
        }
        LUT[b]=out;
    }
}

static inline size_t phys_index(size_t x, size_t y){
    // Which panel
    size_t panel = (x < PW) ? 0 : 1;
    size_t x_p = (panel == 0) ? x : (x - PW);
    size_t y_p = y;

#if RIGHT_ROTATE_180
    if (panel == 1) { x_p = PW - 1 - x_p; y_p = PH - 1 - y_p; }
#elif RIGHT_MIRROR_X
    if (panel == 1) { x_p = PW - 1 - x_p; }
#endif

    // Sneksnek inside each 13×9 panel, bottom row = y=0 is L→R
    size_t row_base = y_p * PW;
    size_t idx_in_panel = (y_p & 1) ? (row_base + (PW - 1 - x_p))
                                    : (row_base + x_p);

    return panel * PANEL_LEDS + idx_in_panel;
}

// Encode GRB bytes -> SPI nibble stream
static size_t encode_bytes(const uint8_t *in, size_t in_len, uint8_t *out){
    size_t p=0;
    for(size_t i=0;i<in_len;i++){
        uint32_t v=LUT[in[i]];
        out[p++]=(uint8_t)(v>>24);
        out[p++]=(uint8_t)(v>>16);
        out[p++]=(uint8_t)(v>>8);
        out[p++]=(uint8_t)(v);
    }
    return p;
}

static int spi_setup(const char *dev){
    int fd = open(dev, O_WRONLY);
    if(fd<0){ perror("open spidev"); return -1; }
    uint8_t mode = SPI_MODE_0, bpw=8;
    if(ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0){ perror("SPI_IOC_WR_MODE"); goto fail; }
    if(ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0){ perror("SPI_IOC_WR_BITS_PER_WORD"); goto fail; }
    if(ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &(uint32_t){SPI_SPEED}) < 0){ perror("SPI_IOC_WR_MAX_SPEED_HZ"); goto fail; }
    return fd;
fail:
    close(fd);
    return -1;
}

static int spi_write_encoded(int fd, const uint8_t *tx, size_t len){
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = 0,
        .len = (uint32_t)len,
        .speed_hz = SPI_SPEED,
        .delay_usecs = 0,
        .bits_per_word = 8,
        .cs_change = 0
    };
    return ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

// appsink callback: copy 26x9 RGB into rgb_latest
static GstFlowReturn on_new_sample(GstAppSink *appsink, gpointer user_data){
    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if(!sample) return GST_FLOW_OK;

    GstCaps *caps = gst_sample_get_caps(sample);
    if(!caps){ gst_sample_unref(sample); return GST_FLOW_OK; }

    GstVideoInfo vinfo;
    if(!gst_video_info_from_caps(&vinfo, caps)){ gst_sample_unref(sample); return GST_FLOW_OK; }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if(!buffer){ gst_sample_unref(sample); return GST_FLOW_OK; }

    GstVideoFrame frame;
    if(!gst_video_frame_map(&frame, &vinfo, buffer, GST_MAP_READ)){
        gst_sample_unref(sample); return GST_FLOW_OK;
    }

    // Expect RGB, 26x9. Handle stride safely.
    pthread_mutex_lock(&rgb_mutex);
    for(int y=0;y<H;y++){
        const uint8_t *src = GST_VIDEO_FRAME_PLANE_DATA(&frame,0) + y*GST_VIDEO_FRAME_PLANE_STRIDE(&frame,0);
        memcpy(rgb_latest[y], src, W*3);
    }
    pthread_mutex_unlock(&rgb_mutex);

    gst_video_frame_unmap(&frame);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

static void *spi_thread_fn(void *arg){
    uint8_t *grb = malloc(LED_COUNT*3);
    uint8_t *tx  = malloc(LED_COUNT*3*4);
    if(!grb || !tx){ perror("malloc"); running=0; }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    while(running){
        // Build GRB in physical order from latest RGB frame.
        pthread_mutex_lock(&rgb_mutex);
        for(int y=0;y<H;y++){
            int src_y = (H-1 - y); // input is top-left origin; convert to bottom-left
            for(int x=0;x<W;x++){
                size_t pi = phys_index((size_t)x, (size_t)y);
                const uint8_t *rgb = &rgb_latest[src_y][x][0];
                // GRB order for SK6805
                grb[pi*3+0] = bLUT[rgb[1]];
                grb[pi*3+1] = bLUT[rgb[0]];
                grb[pi*3+2] = bLUT[rgb[2]];
            }
        }
        pthread_mutex_unlock(&rgb_mutex);

        size_t enc_len = encode_bytes(grb, LED_COUNT*3, tx);
        if(spi_write_encoded(spi_fd, tx, enc_len) < 0) perror("spi write");
        usleep(LATCH_US);

        // pace
        ts.tv_nsec += FRAME_INTERVAL_US * 1000;
        while(ts.tv_nsec >= 1000000000L){ ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    }

    // clear on exit
    memset(grb, 0, LED_COUNT*3);
    size_t enc_len = encode_bytes(grb, LED_COUNT*3, tx);
    spi_write_encoded(spi_fd, tx, enc_len);
    usleep(LATCH_US);

    free(tx); free(grb);
    return NULL;
}

static void on_sigint(int s){ (void)s; running=0; }

int main(int argc, char **argv){

    //Make brightness LUT
    for (int i=0;i<256;i++){
        float v = i/255.0f;
        v = powf(v, GAMMA);                 // optional gamma
        int u = (int)lrintf(v * BRIGHTNESS); 
        if (u<0){ u=0; }
	if (u>255){ u=255; }
        bLUT[i] = (uint8_t)u;
    }

    // Args: optional --pipeline "<gst-launch string ending in appsink name=sink>"
    const char *pipeline_str =
        "videotestsrc is-live=true pattern=smpte ! "
        "videoconvert ! videoscale ! "
        "video/x-raw,format=RGB,width=26,height=9 ! "
        "appsink name=sink emit-signals=true sync=false max-buffers=1 drop=true";

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--pipeline")==0 && i+1<argc){ pipeline_str = argv[++i]; }
    }

    // SPI
    spi_fd = spi_setup(SPI_DEV);
    if(spi_fd < 0) return 1;
    build_lut();

    // Init GStreamer
    gst_init(&argc, &argv);
    GError *err = NULL;
    GstElement *pipeline = gst_parse_launch(pipeline_str, &err);
    if(!pipeline){
        fprintf(stderr, "gst_parse_launch error: %s\n", err?err->message:"unknown");
        if(err) g_error_free(err);
        return 1;
    }
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if(!sink){ fprintf(stderr, "appsink 'sink' not found\n"); return 1; }
    g_object_set(sink, "emit-signals", TRUE, NULL);
    g_signal_connect(sink, "new-sample", G_CALLBACK(on_new_sample), NULL);

    // Fill buffer with black
    pthread_mutex_lock(&rgb_mutex);
    memset(rgb_latest, 0, sizeof(rgb_latest));
    pthread_mutex_unlock(&rgb_mutex);

    // Start SPI thread
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    pthread_t spi_thread;
    pthread_create(&spi_thread, NULL, spi_thread_fn, NULL);

    // Run pipeline
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // Main loop: wait until interrupted
    while(running){
        GstBus *bus = gst_element_get_bus(pipeline);
        GstMessage *msg = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if(msg){
            switch(GST_MESSAGE_TYPE(msg)){
                case GST_MESSAGE_ERROR:{
                    GError *e=NULL; gchar *dbg=NULL;
                    gst_message_parse_error(msg,&e,&dbg);
                    fprintf(stderr,"GStreamer error: %s\n", e?e->message:"");
                    if(dbg){ g_free(dbg); }
                    if(e){ g_error_free(e); }
                    running=0;
                } break;
                case GST_MESSAGE_EOS:
                    running=0; break;
                default: break;
            }
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    }

    // Cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    pthread_join(spi_thread, NULL);
    if(spi_fd>=0) close(spi_fd);
    return 0;
}
