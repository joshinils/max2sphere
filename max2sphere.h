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
    size_t threads;
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


struct buffer_elem {
    pthread_mutex_t mutex;
    boolean waiting_for_stitch;
    size_t nframe;

    BITMAP4* frame_input1;
    BITMAP4* frame_input2;
};
typedef struct buffer_elem buffer_elem;

struct THREAD_DATA {
    size_t worker_id;
    pthread_mutex_t* input_exhausted_mutex;
    boolean* input_exhausted;
    const char* progName;
    const char* last_argument;
    buffer_elem* input_read_buffer;
    buffer_elem* input;

    BITMAP4* frame_spherical;
};
typedef struct THREAD_DATA THREAD_DATA;


// Prototypes

int read_image_pair(const int nframe, THREAD_DATA* data);
void* image_processing_worker_function(void* input);
void set_frame_filename_from_template(char*, char*, int, const char*);
void process_single_image(THREAD_DATA*);
int CheckFrames(const char*, const char*, size_t*, size_t*);
void create_output_filename(char*, const char*, int);
int WriteSpherical(const char*, int, const BITMAP4*, int, int);
int ReadFrame(BITMAP4*, char*, size_t*, size_t*);
int FindFaceUV(double, double, UV*);
BITMAP4 GetColour(int, UV, BITMAP4*, BITMAP4*);
int CheckTemplate(char*, int);

BITMAP4 ColourBlend(BITMAP4, BITMAP4, double);
void RotateUV90(UV*);
void init_default_params(void);
double GetRunTime(void);
void GiveUsage(char*);
