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

typedef struct {
    int outwidth, outheight;
    size_t framewidth, frameheight;
    size_t antialias, antialias2;
    size_t n_start, n_stop;
    PLANE faces[6];
    char outfilename[256];
    boolean debug;
    size_t threads;
} PARAMS;

typedef struct {
    int width, height;
    int sidewidth;
    int centerwidth;
    int blendwidth;
    int equi_width;
} FRAMESPECS;

typedef struct {
    size_t worker_id;
    pthread_mutex_t* counter_mutex;
    size_t* ip_shared_counter;
    const char* progName;
    const char* last_argument;

    BITMAP4* frame_input1;
    BITMAP4* frame_input2;
    BITMAP4* frame_spherical;
} THREAD_DATA;


// Prototypes
void* worker_function(void* input);
void set_frame_filename_from_template(char*, char*, int, const char*);
void process_single_image(THREAD_DATA*, int);
int CheckFrames(const char*, const char*, size_t*, size_t*);
int WriteSpherical(const char*, int, const BITMAP4*, int, int);
int ReadFrame(BITMAP4*, char*, int, int);
int FindFaceUV(double, double, UV*);
BITMAP4 GetColour(int, UV, BITMAP4*, BITMAP4*);
int CheckTemplate(char*, int);

BITMAP4 ColourBlend(BITMAP4, BITMAP4, double);
void RotateUV90(UV*);
void Init(void);
double GetRunTime(void);
void GiveUsage(char*);
