// gcc -o kmsmodeline kmsmodeline.c -ldrm -lgbm -lEGL -lGLESv2 -I/usr/include/libdrm -I/usr/include/GLES2
// super simple stand-alone example of using DRM/GBM+EGL without X11 on the pi4

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>


// The following code related to DRM/GBM was adapted from the following sources:
// https://alantechreview.blogspot.com/2021/04/setting-custom-video-mode-using-drmkms.html
// and
// https://github.com/eyelash/tutorials/blob/master/drm-gbm.c
// and
// https://www.raspberrypi.org/forums/viewtopic.php?t=243707#p1499181
// and
// https://gitlab.freedesktop.org/mesa/drm/-/blob/master/tests/modetest/modetest.c
// and
// https://docs.huihoo.com/doxygen/linux/kernel/3.7/drm__modes_8c_source.html

#include <time.h>
#define MS 1000.0
long long microsec() { // since epoc
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    return ((long long) spec.tv_sec *1000000LL) + (spec.tv_nsec / 1.0e3); // Convert nanoseconds to microseconds (1.0e6 would give miliseconds)
}

#define min(a,b)            (((a) < (b)) ? (a) : (b))
float getenvf(char * name, float def ) {char * r = getenv(name); return (r==0) ? def : atof(r);}

int device;
static drmModeModeInfo mode_info; // our local copy so we can free resources
drmModeModeInfo *mode; // most functions want a pointer
struct gbm_device *gbmDevice;
struct gbm_surface *gbmSurface;
drmModeCrtc *crtc;
uint32_t connectorId;
static const char *eglGetErrorStr(); // moved to bottom

static drmModeConnector* getConnector(drmModeRes* resources)
    {
    for (int i = 0; i < resources->count_connectors; i++)
        {
        drmModeConnector* connector = drmModeGetConnector(device, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED)
            {
            return connector;
            }
        drmModeFreeConnector(connector);
        }

    return NULL;
    }

static drmModeEncoder* findEncoder(drmModeConnector* connector)
    {
    if (connector->encoder_id)
        {
        return drmModeGetEncoder(device, connector->encoder_id);
        }
    return NULL;
    }

static int matchConfigToVisual(EGLDisplay display, EGLint visualId, EGLConfig* configs, int count)
    {
    EGLint id;
    for (int i = 0; i < count; ++i)
        {
        if (!eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &id))
            continue;
        if (id == visualId)
            return i;
        }
    return -1;
    }

static struct gbm_bo* previousBo = NULL;
static uint32_t previousFb;

static void gbmSwapBuffers(EGLDisplay* display, EGLSurface* surface)
    {
    eglSwapBuffers(*display, *surface);
    struct gbm_bo* bo = gbm_surface_lock_front_buffer(gbmSurface);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    uint32_t fb;
    drmModeAddFB(device, mode->hdisplay, mode->vdisplay, 24, 32, pitch, handle, &fb);
    drmModeSetCrtc(device, crtc->crtc_id, fb, 0, 0, &connectorId, 1, mode);

    if (previousBo)
        {
        drmModeRmFB(device, previousFb);
        gbm_surface_release_buffer(gbmSurface, previousBo);
        }
    previousBo = bo;
    previousFb = fb;
    }

// fill the screen with a specific grayscale
static void draw(float progress) 
    {
    glClearColor(progress, progress, progress, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    }

static void gbmClean()
    {
    // set the previous crtc
    drmModeSetCrtc(device, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &connectorId, 1, &crtc->mode);
    drmModeFreeCrtc(crtc);

    if (previousBo)
        {
        drmModeRmFB(device, previousFb);
        gbm_surface_release_buffer(gbmSurface, previousBo);
        }

    gbm_surface_destroy(gbmSurface);
    gbm_device_destroy(gbmDevice);
    }

// The following code was adopted from
// https://github.com/matusnovak/rpi-opengl-without-x/blob/master/triangle.c
// and is licensed under the Unlicense.
static const EGLint configAttribs[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE };

static const EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE };

// start of code for printing out mode in x86modeline syntax
float drm_mode_vrefresh(drmModeModeInfo* mode)   // modded from drm_modes.c
    {
    float refresh = mode->clock * 1000.00 / (mode->htotal * mode->vtotal);

    if (mode->flags & DRM_MODE_FLAG_INTERLACE)
        refresh *= 2;
    if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
        refresh /= 2;
    if (mode->vscan > 1)
        refresh /= mode->vscan;

    return refresh;
    }

// see https://www.kernel.org/doc/html/v4.8/gpu/drm-kms.html for flags,etc

int flagvals[] = { DRM_MODE_FLAG_PHSYNC, DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PVSYNC, DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_INTERLACE, DRM_MODE_FLAG_DBLSCAN,  DRM_MODE_FLAG_CSYNC,  DRM_MODE_FLAG_PCSYNC,  DRM_MODE_FLAG_NCSYNC,
DRM_MODE_FLAG_HSKEW, DRM_MODE_FLAG_BCAST, DRM_MODE_FLAG_PIXMUX, DRM_MODE_FLAG_DBLCLK, DRM_MODE_FLAG_CLKDIV2   };

static const char *mode_flag_names[] = {
	"+hsync",
	"-hsync",
	"+vsync",
	"-vsync",
	"interlace",
	"dblscan",  // mode uses doublescan.
	"csync",    //  mode uses composite sync.
	"pcsync",
	"ncsync",
	"hskew",     // not used?
	"bcast",     // not used?
	"pixmux",   // not used?
	"dblclk",   // double-clocked mode.
	"clkdiv2"   // half-clocked mode.
};    // missing all 3d mode flags

static void dump_mode(drmModeModeInfo *mode)
{
	printf("%s-%.2fhz(%d): %f %d %d %d %d %d %d %d %d",

	       mode->name,
	       drm_mode_vrefresh(mode),
	       mode->vrefresh,
	       mode->clock/1000.0,
	       mode->hdisplay,
	       mode->hsync_start,
	       mode->hsync_end,
	       mode->htotal,
	       mode->vdisplay,
	       mode->vsync_start,
	       mode->vsync_end,
	       mode->vtotal
	       );

  for (int i = 0; i < 14; i++)
    if (mode->flags & flagvals[i])
      printf(" %s", mode_flag_names[i]);

	printf("\n");
	
}

// syntax: kmsmodeline [defined mode #] ["modeline"]
// example: kmsmodeline 0 "13.500000 720 739 801 858 480 488 494 525 -hsync -vsync interlace dblclk

int main(int argc, char* argv[])
{
EGLDisplay display;
drmModeRes* resources;
int modenum = 0;

// defaults settable via enviroment, eg export warmup=4
float warmup = getenvf("warmup", 1); // in seconds
int samples = min(getenvf("samples", 60), 1000); // count, 60 = 1 second for 60hz.

if (argc == 1)
    printf("defaulting to graphics mode 0\n");
else
    modenum = atoi(argv[1]);

// we have to try card0 and card1 to see which is valid. fopen will work on both, so...
device = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

if ((resources = drmModeGetResources(device)) == NULL) // if we have the right device we can get it's resources
    {
    printf("/dev/dri/card0 does not have DRM resources, using card1\n");
    device = open("/dev/dri/card1", O_RDWR | O_CLOEXEC); // if not, try the other one: (1)
    resources = drmModeGetResources(device);
    }
else
    printf("using /dev/dri/card0\n");

if (resources == NULL)
    {
    printf("Unable to get DRM resources on card1\n"); return -1;
    }

drmModeConnector* connector = getConnector(resources);
if (connector == NULL)
    {
    fprintf(stderr, "Unable to get connector\n");
    drmModeFreeResources(resources);
    return -1;
    }

connectorId = connector->connector_id;

// choose a mode (resolution + refresh rate)

mode_info = connector->modes[modenum]; // array of resolutions and refresh rates supported by this display; save to a local mode_info so we can clear the resources later
mode = &mode_info;
dump_mode(mode);

if (argc == 3)
    {
    mode->type = DRM_MODE_TYPE_USERDEF;

    char flags[5][16] = {0,0,0,0,0};;
    float fclock;
    // example: 13.514 720 739 801 858 480 488 494 525 -hsync -vsync interlace dblclk
    sscanf(argv[2], "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s %15s %15s %15s",
        &fclock,
        &mode->hdisplay,
        &mode->hsync_start,
        &mode->hsync_end,
        &mode->htotal,
        &mode->vdisplay,
        &mode->vsync_start,
        &mode->vsync_end,
        &mode->vtotal, flags[0], flags[1], flags[2], flags[3], flags[4]);
        
    mode->clock = fclock * 1000;
    mode->flags = 0;
    // calcualte vrefresh later

    for (int f = 0; f < 4; f++)  // this could use the array that's used to dump the flags
    {
    if (strcasecmp(flags[f], "+hsync") == 0)
                mode->flags |= DRM_MODE_FLAG_PHSYNC;
        if (strcasecmp(flags[f], "-hsync") == 0)
                mode->flags |= DRM_MODE_FLAG_NHSYNC;

        if (strcasecmp(flags[f], "+vsync") == 0)
                mode->flags |= DRM_MODE_FLAG_PVSYNC;
        if (strcasecmp(flags[f], "-vsync") == 0)
                mode->flags |= DRM_MODE_FLAG_NVSYNC;

        if (strcasecmp(flags[f], "interlace") == 0)
                mode->flags |= DRM_MODE_FLAG_INTERLACE;

        if (strcasecmp(flags[f], "dblclk") == 0)
                mode->flags |= DRM_MODE_FLAG_DBLCLK;

    // printf("flag %i\n", mode->flags); debugging, is it finding the flags properly?
    }

    mode->vrefresh = .49 + drm_mode_vrefresh(mode); // looks like this is just for human consumption, since it's an integer it's not used for any real display calculations

    snprintf(mode->name, sizeof(mode->name),"%dx%d%c", // "%dx%d%c@%1.2f", // if you want refresh info
        mode->hdisplay, mode->vdisplay,
        (mode->flags & DRM_MODE_FLAG_INTERLACE ? 'i':0)); // , drm_mode_vrefresh(mode));
    dump_mode(mode);

    }
else if (argc > 3)
        printf("wrong number of args, did you forget the quotes around modeline?\n");

  //    printf("resolution: %ix%i\n", mode.hdisplay, mode.vdisplay);

    // done selecting/calcualting mode, start intialzing kms/drm/egl
    drmModeEncoder *encoder = findEncoder(connector);
    if (encoder == NULL)
    {
        fprintf(stderr, "Unable to get encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        return -1;
    }

    crtc = drmModeGetCrtc(device, encoder->crtc_id);
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);  // we can only free this if we have made a copy of mode (as we have in mode_info)
    drmModeFreeResources(resources);
    gbmDevice = gbm_create_device(device);
    gbmSurface = gbm_surface_create(gbmDevice, mode->hdisplay, mode->vdisplay, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    display = eglGetDisplay(gbmDevice);

    // We will use the screen resolution as the desired width and height for the viewport.
    int desiredWidth = mode->hdisplay;
    int desiredHeight = mode->vdisplay;

    // Other variables we will need further down the code.
    int major, minor;
    GLuint program, vert, frag, vbo;
    GLint posLoc, colorLoc, result;

    if (eglInitialize(display, &major, &minor) == EGL_FALSE)
    {
        fprintf(stderr, "Failed to get EGL version! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // Make sure that we can use OpenGL in this EGL app.
    eglBindAPI(EGL_OPENGL_API);

    printf("Initialized EGL version: %d.%d\n", major, minor);

    EGLint count;
    EGLint numConfigs;
    eglGetConfigs(display, NULL, 0, &count);
    EGLConfig *configs = malloc(count * sizeof(configs));

    if (!eglChooseConfig(display, configAttribs, configs, count, &numConfigs))
    {
        fprintf(stderr, "Failed to get EGL configs! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    // I am not exactly sure why the EGL config must match the GBM format.
    // But it works!
    int configIndex = matchConfigToVisual(display, GBM_FORMAT_XRGB8888, configs, numConfigs);
    if (configIndex < 0)
    {
        fprintf(stderr, "Failed to find matching EGL config! Error: %s\n",
                eglGetErrorStr());
        eglTerminate(display);
        gbm_surface_destroy(gbmSurface);
        gbm_device_destroy(gbmDevice);
        return EXIT_FAILURE;
    }

    EGLContext context = eglCreateContext(display, configs[configIndex], EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "Failed to create EGL context! Error: %s\n",   eglGetErrorStr());
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    EGLSurface surface = eglCreateWindowSurface(display, configs[configIndex], gbmSurface, NULL);
    if (surface == EGL_NO_SURFACE)
    {
        fprintf(stderr, "Failed to create EGL surface! Error: %s\n",   eglGetErrorStr());
        eglDestroyContext(display, context);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    free(configs);
    eglMakeCurrent(display, surface, surface, context);

    // Set GL Viewport size, always needed!
    glViewport(0, 0, desiredWidth, desiredHeight);

    // Get GL Viewport size and test if it is correct.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // viewport[2] and viewport[3] are viewport width and height respectively
    printf("GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

    if (viewport[2] != desiredWidth || viewport[3] != desiredHeight)
    {
        fprintf(stderr, "Error! The glViewport returned incorrect values! Something is wrong!\n");
        eglDestroyContext(display, context);
        eglDestroySurface(display, surface);
        eglTerminate(display);
        gbmClean();
        return EXIT_FAILURE;
    }

    long long start = microsec();
    for (int i = 0; i < warmup*drm_mode_vrefresh(mode); i++) // warm up the gpu buffer pipeline first
      {
      draw(i/(4*drm_mode_vrefresh(mode)));
      gbmSwapBuffers(&display, &surface);
      }

    long long flipstamp[1000];
    int stamp = 0;

    while (stamp < samples)
    {
    draw(0);
    gbmSwapBuffers(&display, &surface);
    flipstamp[stamp++] = microsec();
    
    draw(1);
    gbmSwapBuffers(&display, &surface);
    flipstamp[stamp++] = microsec();
    }

    stamp--; // index of last valid stamp

    for (int i = 1; i < stamp; i++) // might as well skip the first
      {
      float ms = (flipstamp[i+1] - flipstamp[i])/MS;
      printf("%1.2f ms \t%1.2f hz\n", ms, 1000.0/ms);
      }
      printf("------------------------\n");

   printf("%1.4f ms \t%1.4f hz (mean) \n%1.4f ms \t%1.4f hz (ideal)\n",
     (flipstamp[stamp]-flipstamp[0])/(MS*stamp),
     1000.0/((flipstamp[stamp]-flipstamp[0])/(MS*stamp)), 1000.0/drm_mode_vrefresh(mode),drm_mode_vrefresh(mode)  );

    // Cleanup
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    gbmClean();

    close(device);
    return EXIT_SUCCESS;
}


// Get the EGL error back as a string. Useful for debugging.
static const char *eglGetErrorStr()
{
    switch (eglGetError())
    {
    case EGL_SUCCESS:
        return "The last function succeeded without error.";
    case EGL_NOT_INITIALIZED:
        return "EGL is not initialized, or could not be initialized, for the "
               "specified EGL display connection.";
    case EGL_BAD_ACCESS:
        return "EGL cannot access a requested resource (for example a context "
               "is bound in another thread).";
    case EGL_BAD_ALLOC:
        return "EGL failed to allocate resources for the requested operation.";
    case EGL_BAD_ATTRIBUTE:
        return "An unrecognized attribute or attribute value was passed in the "
               "attribute list.";
    case EGL_BAD_CONTEXT:
        return "An EGLContext argument does not name a valid EGL rendering "
               "context.";
    case EGL_BAD_CONFIG:
        return "An EGLConfig argument does not name a valid EGL frame buffer "
               "configuration.";
    case EGL_BAD_CURRENT_SURFACE:
        return "The current surface of the calling thread is a window, pixel "
               "buffer or pixmap that is no longer valid.";
    case EGL_BAD_DISPLAY:
        return "An EGLDisplay argument does not name a valid EGL display "
               "connection.";
    case EGL_BAD_SURFACE:
        return "An EGLSurface argument does not name a valid surface (window, "
               "pixel buffer or pixmap) configured for GL rendering.";
    case EGL_BAD_MATCH:
        return "Arguments are inconsistent (for example, a valid context "
               "requires buffers not supplied by a valid surface).";
    case EGL_BAD_PARAMETER:
        return "One or more argument values are invalid.";
    case EGL_BAD_NATIVE_PIXMAP:
        return "A NativePixmapType argument does not refer to a valid native "
               "pixmap.";
    case EGL_BAD_NATIVE_WINDOW:
        return "A NativeWindowType argument does not refer to a valid native "
               "window.";
    case EGL_CONTEXT_LOST:
        return "A power management event has occurred. The application must "
               "destroy all contexts and reinitialise OpenGL ES state and "
               "objects to continue rendering.";
    default:
        break;
    }
    return "Unknown error!";
}

