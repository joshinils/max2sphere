#include "bitmaplib.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define LEFT 0
#define RIGHT 1
#define TOP 2
#define FRONT 3
#define BACK 4
#define DOWN 5

#define NEARLYONE 0.99999

typedef struct {
    double x, y, z;
} XYZ;

typedef struct {
    float u, v;
} UV;

typedef struct {
    double a, b, c, d;
} PLANE;

struct PARAMS {
    int outwidth, outheight;
    size_t framewidth, frameheight;
    size_t antialias, antialias2;
    size_t n_start, n_stop;
    PLANE faces[6];
    char outfilename[256];
    boolean debug;
    size_t thread_count;
    boolean skip_existing;
    size_t input_buffer_length;
};
typedef struct PARAMS PARAMS;

struct FRAMESPECS {
    int width, height;
    int sidewidth;
    int centerwidth;
    int blendwidth;
    int equi_width;
};
typedef struct FRAMESPECS FRAMESPECS;


// Lookup table
typedef struct {
    UV uv;
    short int face;
} LLTABLE;

typedef struct SPHERICAL_CONTEXT {
    PARAMS params;
    LLTABLE* lltable;
    int ntable;
    int whichtemplate;
    boolean is_initialized;
} SPHERICAL_CONTEXT;

typedef struct buffer_elem {
    pthread_mutex_t mutex;
    boolean waiting_for_stitch;
    size_t nframe;

    BITMAP4* frame_input1;
    BITMAP4* frame_input2;
} buffer_elem;

struct THREAD_DATA {
    size_t worker_id;
    pthread_mutex_t* input_exhausted_mutex;
    boolean* input_exhausted;
    const char* progName;
    const char* last_argument;
    buffer_elem* input_read_buffer;
    buffer_elem* input;

    SPHERICAL_CONTEXT* ctx;

    BITMAP4* frame_spherical;
};
typedef struct THREAD_DATA THREAD_DATA;


// Prototypes

int read_image_pair(const int nframe, THREAD_DATA* data);
void* image_processing_worker_function(void* input);
void set_frame_filename_from_template(char*, char*, int, const char*);
void process_single_image(THREAD_DATA*);
int CheckFrames(const char*, const char*, size_t*, size_t*);
int init_whichtemplate(int width1, int height1);
void create_output_filename(char*, const char*, int);
int WriteSpherical(const char*, int, const BITMAP4*, int, int);
int ReadFrame(BITMAP4*, char*, size_t*, size_t*);
int FindFaceUV(double longitude, double latitude, UV* uv);
BITMAP4 GetColour(int face, UV uv, BITMAP4* frame1, BITMAP4* frame2);
int CheckTemplate(char*, int);

SPHERICAL_CONTEXT* GetSphericalContext();
SPHERICAL_CONTEXT* GetSphericalContext_(const char* caller);
SPHERICAL_CONTEXT* CreateSphericalContext();
void DestroySphericalContext();
int LoadLookupTable(const char* progName);
void calc_spherical(BITMAP4* frame_input1, BITMAP4* frame_input2, BITMAP4* spherical_out);

BITMAP4 ColourBlend(BITMAP4, BITMAP4, double);
void RotateUV90(UV*);
void init_default_params(SPHERICAL_CONTEXT* ctx);
double GetRunTime(void);
void GiveUsage(char*);
