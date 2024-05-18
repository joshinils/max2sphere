#include "max2sphere.h"
#include <unistd.h> // TODO: remove, gives sleep and unisleep
/*
    Convert a sequence of pairs of frames from the GoPro Max camera to an equirectangular
    Sept 08: First version based upon cube2sphere
    Sept 10: Added output file mask
   Dec  14: This version which is a batch converter based upon a lookup table
*/

PARAMS params;

// These are known frame templates
// The appropriate one to use will be auto detected, error is none match
#define NTEMPLATE 2
FRAMESPECS template[NTEMPLATE] = { { 4096, 1344, 1376, 1344, 32, 5376 }, { 2272, 736, 768, 736, 16, 2944 } };
int whichtemplate = -1; // Which frame template do we think we have

// Lookup table
typedef struct {
    UV uv;
    short int face;
} LLTABLE;
LLTABLE* lltable = NULL;
int ntable = 0;
int itable = 0;


typedef struct {
    size_t worker_id;
    pthread_mutex_t* counter_mutex;
    int* ip_shared_counter;
} thread_data;


void* worker_function(void* input);

int thread_test() {
    const size_t THREAD_COUNT = 16;
    pthread_t thread[THREAD_COUNT];
    thread_data data[THREAD_COUNT];

    pthread_mutex_t mutex_counter = PTHREAD_MUTEX_INITIALIZER;
    int counter = 100;

    for(size_t i = 0; i < THREAD_COUNT; i++) {
        // Initialize the thread data
        data[i].worker_id = i;
        data[i].counter_mutex = &mutex_counter;
        data[i].ip_shared_counter = &counter;

        int creating_thread_status = pthread_create(&(thread[i]), NULL, worker_function, (void*)&data[i]);
        if(creating_thread_status) {
            fprintf(stderr, "Error creating thread %02li, exiting.\n", i);
            exit(-1);
        }
    }

    for(size_t i = 0; i < THREAD_COUNT; i++) {
        // Wait till threads are complete before main continues
        pthread_join(thread[i], NULL);
        printf("Thread: %02li done\n", i);
    }
    exit(0);
}

void print_message_function(thread_data* data, int todo_id) {
    printf("Thread: %02li working for %d\n", data->worker_id, todo_id);
    usleep(todo_id);
    printf("Thread: %02li    finished %d\n", data->worker_id, todo_id);
}


void* worker_function(void* input) {
    // Cast the pointer to the correct type
    thread_data* data = (thread_data*)input;

    for(;;) {
        pthread_mutex_lock(data->counter_mutex);
        printf("Thread: %02li counter pre: %d\n", data->worker_id, *data->ip_shared_counter);
        int work_time = *(data->ip_shared_counter);
        (*(data->ip_shared_counter))--;
        printf("Thread: %02li counter post: %d\n", data->worker_id, *data->ip_shared_counter);
        pthread_mutex_unlock(data->counter_mutex);

        if(work_time <= 0) { break; }

        print_message_function(data, work_time);
        printf("Thread: %02li finished job %d\n", data->worker_id, work_time);
    }
    printf("Thread: %02li finished all jobs\n", data->worker_id);

    return NULL;
}


int main(int argc, char** argv) {
    thread_test();
    return 0;


    char tablename[256];
    double x, y, dx, dy, x0, y0, longitude, latitude;
    FILE* fptr;

    // Default settings
    Init();

    // Check and parse command line
    if(argc < 2) GiveUsage(argv[0]);
    for(int i = 1; i < argc - 1; i++) {
        if(strcmp(argv[i], "-w") == 0) {
            params.outwidth = atoi(argv[i + 1]);
            params.outwidth = 4 * (params.outwidth / 4); // Make factor of 4
            params.outheight = params.outwidth / 2; // Will be even
        } else if(strcmp(argv[i], "-a") == 0) {
            params.antialias = MAX(1, atoi(argv[i + 1]));
            params.antialias2 = params.antialias * params.antialias;
        } else if(strcmp(argv[i], "-o") == 0) {
            strcpy(params.outfilename, argv[i + 1]);
        } else if(strcmp(argv[i], "-n") == 0) {
            params.nstart = atoi(argv[i + 1]);
        } else if(strcmp(argv[i], "-m") == 0) {
            params.nstop = atoi(argv[i + 1]);
        } else if(strcmp(argv[i], "-d") == 0) {
            params.debug = TRUE;
        }
    }

    // Check filename templates
    if(!CheckTemplate(argv[argc - 1], 2)) // Fatal
        exit(-1);
    if(strlen(params.outfilename) > 2) {
        if(!CheckTemplate(params.outfilename, 1)) // Delete user selected output filename template
            params.outfilename[0] = '\0';
    }


    char fname1[256], fname2[256];
    // Check the first frame to determine template and frame sizes
    set_frame_filename_from_template(fname1, fname2, params.nstart, argv[argc - 1]);
    if((whichtemplate = CheckFrames(fname1, fname2, &params.framewidth, &params.frameheight)) < 0) exit(-1);
    if(params.debug) {
        fprintf(stderr, "%s() - frame dimensions: %d x %d\n", argv[0], params.framewidth, params.frameheight);
        fprintf(stderr, "%s() - Expect frame template %d\n", argv[0], whichtemplate + 1);
    }

    if(params.outwidth < 0) {
        params.outwidth = template[whichtemplate].equiwidth;
        params.outheight = params.outwidth / 2;
    }

    // Does a table exist? If it does, load it. if not, create it and save it
    ntable = params.outheight * params.outwidth * params.antialias * params.antialias;
    lltable = malloc(ntable * sizeof(LLTABLE));
    sprintf(tablename, "%d_%d_%d_%d.data", whichtemplate, params.outwidth, params.outheight, params.antialias);
    int n = 0;
    if((fptr = fopen(tablename, "r")) != NULL) {
        if(params.debug) fprintf(stderr, "%s() - Reading lookup table\n", argv[0]);
        if((n = fread(lltable, sizeof(LLTABLE), ntable, fptr)) != ntable) {
            fprintf(stderr, "%s() - Failed to read lookup table \"%s\" (%d != %d)\n", argv[0], tablename, n, ntable);
        }
        fclose(fptr);
    }
    if(n != ntable) {
        if(params.debug) fprintf(stderr, "%s() - Generating lookup table\n", argv[0]);
        dx = params.antialias * params.outwidth;
        dy = params.antialias * params.outheight;
        itable = 0;
        for(int j = 0; j < params.outheight; j++) {
            y0 = j / (double)params.outheight;
            for(int i = 0; i < params.outwidth; i++) {
                x0 = i / (double)params.outwidth;
                for(int aj = 0; aj < params.antialias; aj++) {
                    y = y0 + aj / dy; // 0 ... 1
                    for(int ai = 0; ai < params.antialias; ai++) {
                        x = x0 + ai / dx; // 0 ... 1
                        longitude = x * TWOPI - M_PI; // -pi ... pi
                        latitude = y * M_PI - M_PI / 2; // -pi/2 ... pi/2
                        lltable[itable].face = FindFaceUV(longitude, latitude, &(lltable[itable].uv));
                        itable++;
                    }
                }
            }
        }
        if(params.debug) fprintf(stderr, "%s() - Saving lookup table\n", argv[0]);
        fptr = fopen(tablename, "w");
        fwrite(lltable, ntable, sizeof(LLTABLE), fptr);
        fclose(fptr);
    }

    // Process each frame of the sequence
    for(int nframe = params.nstart; nframe <= params.nstop; nframe++) {
        process_single_image(nframe, argv[0], argv[argc - 1]);
    }

    exit(0);
}


void set_frame_filename_from_template(char* fname1, char* fname2, int nframe, const char* last_argument) {
    sprintf(fname1, last_argument, 0, nframe);
    sprintf(fname2, last_argument, 5, nframe);
}


void process_single_image(int nframe, const char* progName, const char* last_argument) {
    char fname1[256], fname2[256];
    set_frame_filename_from_template(fname1, fname2, nframe, last_argument);

    // Malloc images
    BITMAP4* frame_input1 = Create_Bitmap(params.framewidth, params.frameheight);
    BITMAP4* frame_input2 = Create_Bitmap(params.framewidth, params.frameheight);
    BITMAP4* frame_spherical = Create_Bitmap(params.outwidth, params.outheight);

    if(frame_input1 == NULL || frame_input2 == NULL || frame_spherical == NULL) {
        fprintf(stderr, "%s() - Failed to malloc memory for the images\n", progName);
        exit(-1);
    }

    // Form the spherical map
    if(params.debug) fprintf(stderr, "%s() - Creating spherical map for frame %d\n", progName, nframe);
    BITMAP4 black = { 0, 0, 0, 255 };
    Erase_Bitmap(frame_spherical, params.outwidth, params.outheight, black);

    // Read both frames
    if(!ReadFrame(frame_input1, fname1, params.framewidth, params.frameheight)) {
        if(params.debug) fprintf(stderr, "%s() - failed to read frame \"%s\"\n", progName, fname2);
        return;
    }
    if(!ReadFrame(frame_input2, fname2, params.framewidth, params.frameheight)) {
        if(params.debug) fprintf(stderr, "%s() - failed to read frame \"%s\"\n", progName, fname2);
        return;
    }

    double starttime = GetRunTime();
    itable = 0;
    for(int j = 0; j < params.outheight; j++) {
        //y0 = j / (double)params.outheight;
        //if (params.debug && j % (params.outheight/32) == 0)
        //fprintf(stderr,"%s() - Scan line %d\n",progName,j);

        for(int i = 0; i < params.outwidth; i++) {
            //x0 = i / (double)params.outwidth;
            COLOUR16 csum = { 0, 0, 0 }; // Supersampling antialising sum

            // Antialiasing loops
            for(int aj = 0; aj < params.antialias; aj++) {
                //y = y0 + aj / dy; // 0 ... 1

                // Antialiasing loops
                for(int ai = 0; ai < params.antialias; ai++) {
                    //x = x0 + ai / dx; // 0 ... 1

                    // Calculate latitude and longitude
                    //longitude = x * TWOPI - M_PI;    // -pi ... pi
                    //latitude = y * M_PI - M_PI/2;    // -pi/2 ... pi/2
                    int face = lltable[itable].face;
                    UV uv = lltable[itable].uv;
                    itable++;

                    // Sum over the supersampling set
                    BITMAP4 c = GetColour(face, uv, frame_input1, frame_input2);
                    csum.r += c.r;
                    csum.g += c.g;
                    csum.b += c.b;
                }
            }

            // Finally update the spherical image
            int index = j * params.outwidth + i;
            frame_spherical[index].r = csum.r / params.antialias2;
            frame_spherical[index].g = csum.g / params.antialias2;
            frame_spherical[index].b = csum.b / params.antialias2;
        }
    }

    if(params.debug) { fprintf(stderr, "%s() - Processing time: %g seconds\n", progName, GetRunTime() - starttime); }

    // Write out the equirectangular
    // Base the name on the name of the first frame
    if(params.debug) fprintf(stderr, "%s() - Saving equirectangular\n", progName);
    WriteSpherical(fname1, nframe, frame_spherical, params.outwidth, params.outheight);
}


/*
    Check the frames
    - do they exist
    - are they jpeg or png
    - are they the same size
    - determine which frame template we are using
*/
int CheckFrames(const char* fname1, const char* fname2, int* width, int* height) {
    boolean frame1_is_jpg = IsJPEG(fname1);
    boolean frame2_is_jpg = IsJPEG(fname2);
    boolean frame1_is_png = IsPNG(fname1);
    boolean frame2_is_png = IsPNG(fname2);

    if((!frame1_is_jpg && !frame1_is_png) || (!frame2_is_jpg && !frame2_is_png)) {
        fprintf(stderr, "CheckFrames() - frame name does not look like a jpeg or png file\n");
        return (-1);
    }

    FILE* fptr;
    // Frame 1
    if((fptr = fopen(fname1, "rb")) == NULL) {
        fprintf(stderr, "CheckFrames() - Failed to open first frame \"%s\"\n", fname1);
        return (-1);
    }
    int w1 = -1, h1 = -1, depth = -1;
    if(frame1_is_jpg) {
        JPEG_Info(fptr, &w1, &h1, &depth);
    } else {
        PNG_Info(fptr, &w1, &h1, &depth);
    }
    fclose(fptr);

    // Frame 2
    if((fptr = fopen(fname2, "rb")) == NULL) {
        fprintf(stderr, "CheckFrames() - Failed to open second frame \"%s\"\n", fname2);
        return (-1);
    }
    int w2 = -1, h2 = -1;
    if(frame1_is_jpg) {
        JPEG_Info(fptr, &w1, &h1, &depth);
    } else {
        PNG_Info(fptr, &w1, &h1, &depth);
    }
    fclose(fptr);

    // Are they the same size
    if(w1 != w2 || h1 != h2) {
        fprintf(stderr, "CheckFrames() - Frame sizes don't match, %d != %d or %d != %d\n", w1, h1, w2, h2);
        return (-1);
    }

    int template_n = -1;
    // Is it a known template?
    for(int i = 0; i < NTEMPLATE; i++) {
        if(w1 == template[i].width && h1 == template[i].height) {
            template_n = i;
            break;
        }
    }
    if(template_n < 0) {
        fprintf(stderr, "CheckFrames() - No recognised frame template\n");
        return (-1);
    }

    *width = w1;
    *height = h1;

    return template_n;
}


/*
   Write spherical image
    The file name is either using the mask params.outfilename which should have a %d for the frame number
    or based upon the basename provided which will have two %d locations for track and framenumber
*/
int WriteSpherical(const char* basename, int nframe, const BITMAP4* img, int w, int h) {
    int i;
    FILE* fptr;
    char fname[256];

    // Create the output file name
    if(strlen(params.outfilename) < 2) {
        sprintf(fname, basename, 0, nframe);
        for(i = strlen(fname) - 1; i > 0; i--) {
            if(fname[i] == '.') {
                fname[i] = '\0';
                break;
            }
        }
        strcat(fname, "_sphere.png");
    } else {
        sprintf(fname, params.outfilename, nframe);
    }

    if(params.debug) fprintf(stderr, "WriteSpherical() - Saving file \"%s\"\n", fname);

    // Save
    if((fptr = fopen(fname, "wb")) == NULL) {
        fprintf(stderr, "WriteSpherical() - Failed to open output file \"%s\"\n", fname);
        return (FALSE);
    }
    PNG_Write(fptr, img, w, h, 100);
    fclose(fptr);

    return (TRUE);
}

/*
   Read a frame
*/
int ReadFrame(BITMAP4* img, char* fname, int w, int h) {
    FILE* fptr;

    if(params.debug) fprintf(stderr, "ReadFrame() - Reading image \"%s\"\n", fname);

    // Attempt to open file
    if((fptr = fopen(fname, "rb")) == NULL) {
        fprintf(stderr, "ReadFrame() - Failed to open \"%s\"\n", fname);
        return (FALSE);
    }

    // Read image data
    if((IsJPEG(fname) && JPEG_Read(fptr, img, &w, &h) != 0) || (IsPNG(fname) && PNG_Read(fptr, img, &w, &h) != 0)) {
        fprintf(stderr, "ReadFrame() - Failed to correctly read JPG/PNG file \"%s\"\n", fname);
        return (FALSE);
    }
    fclose(fptr);

    return (TRUE);
}

/*
   Given longitude and latitude find corresponding face id and (u,v) coordinate on the face
   Return -1 if something went wrong, shouldn't
*/
int FindFaceUV(double longitude, double latitude, UV* uv) {
    int k, found = -1;
    double mu, denom, coslatitude;
    UV fuv;
    XYZ p, q;

    // p is the ray from the camera position into the scene
    coslatitude = cos(latitude);
    p.x = coslatitude * sin(longitude);
    p.y = coslatitude * cos(longitude);
    p.z = sin(latitude);

    // Find which face the vector intersects
    for(k = 0; k < 6; k++) {
        denom = -(params.faces[k].a * p.x + params.faces[k].b * p.y + params.faces[k].c * p.z);

        // Is p parallel to face? Shouldn't ever happen.
        //if (ABS(denom) < 0.000001)
        //   continue;

        // Find position q along ray and ignore intersections on the back pointing ray?
        if((mu = params.faces[k].d / denom) < 0) continue;
        q.x = mu * p.x;
        q.y = mu * p.y;
        q.z = mu * p.z;

        // Find out which face it is on
        switch(k) {
        case LEFT:
        case RIGHT:
            if(q.y <= 1 && q.y >= -1 && q.z <= 1 && q.z >= -1) found = k;
            q.y = (atan(q.y) * 4.0) / M_PI;
            q.z = (atan(q.z) * 4.0) / M_PI;
            break;
        case FRONT:
        case BACK:
            if(q.x <= 1 && q.x >= -1 && q.z <= 1 && q.z >= -1) found = k;
            q.x = (atan(q.x) * 4.0) / M_PI;
            q.z = (atan(q.z) * 4.0) / M_PI;
            break;
        case TOP:
        case DOWN:
            if(q.x <= 1 && q.x >= -1 && q.y <= 1 && q.y >= -1) found = k;
            q.x = (atan(q.x) * 4.0) / M_PI;
            q.y = (atan(q.y) * 4.0) / M_PI;
            break;
        }
        if(found >= 0) break;
    }
    if(found < 0 || found > 5) {
        fprintf(stderr, "FindFaceUV() - Didn't find an intersecting face, shouldn't happen!\n");
        return (-1);
    }

    // Determine the u,v coordinate
    switch(found) {
    case LEFT:
        fuv.u = q.y + 1;
        fuv.v = q.z + 1;
        break;
    case RIGHT:
        fuv.u = 1 - q.y;
        fuv.v = q.z + 1;
        break;
    case FRONT:
        fuv.u = q.x + 1;
        fuv.v = q.z + 1;
        break;
    case BACK:
        fuv.u = 1 - q.x;
        fuv.v = q.z + 1;
        break;
    case DOWN:
        fuv.u = 1 - q.x;
        fuv.v = 1 - q.y;
        break;
    case TOP:
        fuv.u = 1 - q.x;
        fuv.v = q.y + 1;
        break;
    }
    fuv.u *= 0.5;
    fuv.v *= 0.5;

    // Need to understand this at some stage
    if(fuv.u >= 1) fuv.u = NEARLYONE;
    if(fuv.v >= 1) fuv.v = NEARLYONE;

    if(fuv.u < 0 || fuv.v < 0 || fuv.u >= 1 || fuv.v >= 1) {
        fprintf(stderr, "FindFaceUV() - Illegal (u,v) coordinate (%g,%g) on face %d\n", fuv.u, fuv.v, found);
        return (-1);
    }

    *uv = fuv;

    return (found);
}

/*
    Given a face and a (u,v) in that face, determine colour from the two frames
    This is largely a mapping exercise from (u,v) of each face to the two frames
    For faces left, right, down and top a blend is required between the two halves
    Relies on the values from the frame template
*/
BITMAP4 GetColour(int face, UV uv, BITMAP4* frame1, BITMAP4* frame2) {
    int ix, iy, index;
    int x0, w;
    double alpha, duv;
    UV uvleft, uvright;
    BITMAP4 c = { 0, 0, 0, 255 }, c1, c2;

    // Rotate u,v counterclockwise by 90 degrees for lower frame
    if(face == DOWN || face == BACK || face == TOP) RotateUV90(&uv);

    // v doesn't change
    uvleft.v = uv.v;
    uvright.v = uv.v;

    switch(face) {
    // Frame 1
    case FRONT:
    case BACK:
        x0 = template[whichtemplate].sidewidth;
        w = template[whichtemplate].centerwidth;
        ix = x0 + uv.u * w;
        iy = uv.v * template[whichtemplate].height;
        index = iy * template[whichtemplate].width + ix;
        c = (face == FRONT) ? frame1[index] : frame2[index];
        break;
    case LEFT:
    case DOWN:
        w = template[whichtemplate].sidewidth;
        duv = template[whichtemplate].blendwidth / (double)w;
        uvleft.u = 2.0 * (0.5 - duv) * uv.u;
        uvright.u = 2.0 * (0.5 - duv) * (uv.u - 0.5) + 0.5 + duv;
        if(uvleft.u <= 0.5 - 2.0 * duv) {
            ix = uvleft.u * w;
            iy = uvleft.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c = (face == LEFT) ? frame1[index] : frame2[index];
        } else if(uvright.u >= 0.5 + 2.0 * duv) {
            ix = uvright.u * w;
            iy = uvright.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c = (face == LEFT) ? frame1[index] : frame2[index];
        } else {
            ix = uvleft.u * w;
            iy = uvleft.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c1 = (face == LEFT) ? frame1[index] : frame2[index];
            ix = uvright.u * w;
            iy = uvright.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c2 = (face == LEFT) ? frame1[index] : frame2[index];
            alpha = (uvleft.u - 0.5 + 2.0 * duv) / (2.0 * duv);
            c = ColourBlend(c1, c2, alpha);
        }
        break;
    case RIGHT:
    case TOP:
        x0 = template[whichtemplate].sidewidth + template[whichtemplate].centerwidth;
        w = template[whichtemplate].sidewidth;
        duv = template[whichtemplate].blendwidth / (double)w;
        uvleft.u = 2.0 * (0.5 - duv) * uv.u;
        uvright.u = 2.0 * (0.5 - duv) * (uv.u - 0.5) + 0.5 + duv;
        if(uvleft.u <= 0.5 - 2.0 * duv) {
            ix = x0 + uvleft.u * w;
            iy = uv.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c = (face == RIGHT) ? frame1[index] : frame2[index];
        } else if(uvright.u >= 0.5 + 2.0 * duv) {
            ix = x0 + uvright.u * w;
            iy = uvright.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c = (face == RIGHT) ? frame1[index] : frame2[index];
        } else {
            ix = x0 + uvleft.u * w;
            iy = uvleft.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c1 = (face == RIGHT) ? frame1[index] : frame2[index];
            ix = x0 + uvright.u * w;
            iy = uvright.v * template[whichtemplate].height;
            index = iy * template[whichtemplate].width + ix;
            c2 = (face == RIGHT) ? frame1[index] : frame2[index];
            alpha = (uvleft.u - 0.5 + 2.0 * duv) / (2.0 * duv);
            c = ColourBlend(c1, c2, alpha);
        }
        break;
    }

    return (c);
}

/*
    Blend two colours
*/
BITMAP4 ColourBlend(BITMAP4 c1, BITMAP4 c2, double alpha) {
    double m1;
    BITMAP4 c;

    alpha = tanh(alpha * 5.0 - 5.0 / 2.0) / 2 + 0.5;

    m1 = 1 - alpha;
    c.r = m1 * c1.r + alpha * c2.r;
    c.g = m1 * c1.g + alpha * c2.g;
    c.b = m1 * c1.b + alpha * c2.b;

    return (c);
}

/*
    Rotate a uv by 90 degrees counterclockwise
*/
void RotateUV90(UV* uv) {
    UV tmp;

    tmp = *uv;
    uv->u = tmp.v;
    uv->v = NEARLYONE - tmp.u;
}

/*
    Initialise parameters structure
*/
void Init(void) {
    params.outwidth = -1;
    params.outheight = -1;
    params.framewidth = -1;
    params.frameheight = -1;
    params.antialias = 2;
    params.antialias2 = 4; // antialias squared
    params.nstart = 0;
    params.nstop = 100000;
    params.outfilename[0] = '\0';
    params.debug = FALSE;

    // Parameters for the 6 cube planes, ax + by + cz + d = 0
    params.faces[LEFT].a = -1;
    params.faces[LEFT].b = 0;
    params.faces[LEFT].c = 0;
    params.faces[LEFT].d = -1;

    params.faces[RIGHT].a = 1;
    params.faces[RIGHT].b = 0;
    params.faces[RIGHT].c = 0;
    params.faces[RIGHT].d = -1;

    params.faces[TOP].a = 0;
    params.faces[TOP].b = 0;
    params.faces[TOP].c = 1;
    params.faces[TOP].d = -1;

    params.faces[DOWN].a = 0;
    params.faces[DOWN].b = 0;
    params.faces[DOWN].c = -1;
    params.faces[DOWN].d = -1;

    params.faces[FRONT].a = 0;
    params.faces[FRONT].b = 1;
    params.faces[FRONT].c = 0;
    params.faces[FRONT].d = -1;

    params.faces[BACK].a = 0;
    params.faces[BACK].b = -1;
    params.faces[BACK].c = 0;
    params.faces[BACK].d = -1;
}

/*
   Time scale at microsecond resolution but returned as seconds
    OS dependent, an alternative will need to be found for non UNIX systems
*/
double GetRunTime(void) {
    double sec = 0;
    struct timeval tp;

    gettimeofday(&tp, NULL);
    sec = tp.tv_sec + tp.tv_usec / 1000000.0;

    return (sec);
}

/*
    Check that the filename template has the correct number of %d entries
*/
int CheckTemplate(char* s, int nexpect) {
    int n = 0;
    for(size_t i = 0; i < strlen(s); i++) {
        if(s[i] == '%') n++;
    }

    if(n != nexpect) {
        fprintf(stderr, "This filename template \"%s\" does not look like it contains sufficient %%d entries\n", s);
        fprintf(stderr, "Expect %d but found %d\n", nexpect, n);
        return (FALSE);
    }

    return (TRUE);
}

/*
   Standard usage string
*/
void GiveUsage(char* s) {
    fprintf(stderr, "Usage: %s [options] sequencetemplate\n", s);
    fprintf(stderr, "\n");
    fprintf(stderr, "The sequence filename template should contain two %%d entries. The first will be populated\n");
    fprintf(stderr, "with the track number 0 or 5, the second is the frame sequence number, see -n and -m below.\n");
    fprintf(stderr, "So for example, if there are 1000 frames called track0_frame0001.png, track5_0001.png, ...\n");
    fprintf(stderr, "then the program might be called as follows:\n");
    fprintf(stderr, "   %s -w 4096 -n 1 -m 1000 track%%d_frame%%04d.png\n", s);
    fprintf(stderr, "Or if directories are used with frames track0/frame1.png, track5/1000.png, ...\n");
    fprintf(stderr, "   %s -w 4096 -n 1 -m 1000 track%%d/frame%%4d.png\n", s);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options\n");
    fprintf(stderr, "   -w n      Sets the output image width, default: %d\n", params.outwidth);
    fprintf(stderr, "   -a n      Sets antialiasing level, default = %d\n", params.antialias);
    fprintf(stderr,
            "   -o s      Specify the output filename template, default is based on track 0 name uses track 2\n");
    fprintf(stderr, "             If specified then it should contain one %%d field for the frame number\n");
    fprintf(stderr, "   -n n      Start index for the sequence, default: %d\n", params.nstart);
    fprintf(stderr, "   -m n      End index for the sequence, default: %d\n", params.nstop);
    fprintf(stderr, "   -d        Enable debug mode, default: off\n");
    exit(-1);
}
