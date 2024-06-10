#include "max2sphere.h"
#include <unistd.h>

/*
    Convert a sequence of pairs of frames from the GoPro MAX camera to an equirectangular
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
LLTABLE* g_lltable = NULL;
int ntable = 0;
int itable = 0;


int read_BMP_stdin(BITMAP4* image, size_t* width, size_t* height) {
    int ret_code = BMP_Read(stdin, image, width, height);
    return ret_code;
}

void test_read_from_stdin() {
    size_t width_front = 1344 * 4096;
    size_t height_front = 1;

    size_t width_back = 1344 * 4096;
    size_t height_back = 1;

    BITMAP4* img_front = Create_Bitmap(width_front, height_front);
    BITMAP4* img_back = Create_Bitmap(width_front, height_front);


    for(size_t img_count = 0; TRUE; ++img_count) {
        int ret_code_front = read_BMP_stdin(img_front, &width_front, &height_front);
        int ret_code_back = read_BMP_stdin(img_back, &width_back, &height_back);
        if(ret_code_front || ret_code_back) {
            fprintf(stderr, "no image read, assuming stdin finished, stopping\n");
            break;
        }
    }

    Destroy_Bitmap(img_front);
    Destroy_Bitmap(img_back);


    exit(42);
}


void load_lltable(const char* progName) {
    // Does a table exist? If it does, load it. if not, create it and save it
    ntable = params.outheight * params.outwidth * params.antialias * params.antialias;
    g_lltable = malloc(ntable * sizeof(LLTABLE));
    char tablename[256];
    sprintf(tablename, "%d_%d_%d_%li.data", whichtemplate, params.outwidth, params.outheight, params.antialias);
    FILE* fptr;
    int n = 0;
    if((fptr = fopen(tablename, "r")) != NULL) {
        if(params.debug) fprintf(stderr, "%s() - Reading lookup table\n", progName);
        if((n = fread(g_lltable, sizeof(LLTABLE), ntable, fptr)) != ntable) {
            fprintf(stderr, "%s() - Failed to read lookup table \"%s\" (%d != %d)\n", progName, tablename, n, ntable);
        }
        fclose(fptr);
    }

    if(n != ntable) {
        if(params.debug) fprintf(stderr, "%s() - Generating lookup table\n", progName);
        double dx = params.antialias * params.outwidth;
        double dy = params.antialias * params.outheight;
        itable = 0;
        for(int j = 0; j < params.outheight; j++) {
            double y0 = j / (double)params.outheight;
            for(int i = 0; i < params.outwidth; i++) {
                double x0 = i / (double)params.outwidth;
                for(size_t aj = 0; aj < params.antialias; aj++) {
                    double y = y0 + aj / dy; // 0 ... 1
                    for(size_t ai = 0; ai < params.antialias; ai++) {
                        double x = x0 + ai / dx; // 0 ... 1
                        double longitude = x * TWOPI - M_PI; // -pi ... pi
                        double latitude = y * M_PI - M_PI / 2; // -pi/2 ... pi/2
                        g_lltable[itable].face = FindFaceUV(longitude, latitude, &(g_lltable[itable].uv));
                        itable++;
                    }
                }
            }
        }
        if(params.debug) fprintf(stderr, "%s() - Saving lookup table\n", progName);
        fptr = fopen(tablename, "w");
        fwrite(g_lltable, ntable, sizeof(LLTABLE), fptr);
        fclose(fptr);
    }
}


void* image_processing_worker_function(void* input) {
    // Cast the pointer to the correct type
    THREAD_DATA* data = (THREAD_DATA*)input;

    for(;;) {
        pthread_mutex_lock(data->counter_mutex);
        const size_t nframe = *(data->ip_shared_counter);
        (*(data->ip_shared_counter))++;
        pthread_mutex_unlock(data->counter_mutex);

        if(nframe > params.n_stop) { break; }

        if(params.debug) { fprintf(stderr, "%s() T%02li - starting job %li\n", data->progName, data->worker_id, nframe); }
        process_single_image(data, nframe);
        if(params.debug) { fprintf(stderr, "%s() T%02li - finished job %li\n", data->progName, data->worker_id, nframe); }
    }
    if(params.debug) { fprintf(stderr, "%s() T%02li - finished all jobs\n", data->progName, data->worker_id); }
    return NULL;
}


void* image_reader_worker_function(void* input) {
    // Cast the pointer to the correct type
    THREAD_DATA* data = (THREAD_DATA*)input;

    // while able to get another image:
        // wait until image is is small enough
        // put image into queue

    return NULL;
}


void init(int argc, char** argv) {
    // Default settings
    init_default_params();

    // Check and parse command line
    if(argc < 2) {
        GiveUsage(argv[0]);
        exit(-1);
    }

    for(int i = 1; i < argc - 1; i++) {
        if(strcmp(argv[i], "-w") == 0) {
            params.outwidth = MAX(1, atoi(argv[i + 1]));
            params.outwidth = 4 * (params.outwidth / 4); // Make factor of 4
            params.outheight = params.outwidth / 2; // Will be even
        } else if(strcmp(argv[i], "-a") == 0) {
            params.antialias = MAX(1, atoi(argv[i + 1]));
            params.antialias2 = params.antialias * params.antialias;
        } else if(strcmp(argv[i], "-o") == 0) {
            strcpy(params.outfilename, argv[i + 1]);
        } else if(strcmp(argv[i], "-n") == 0) {
            params.n_start = MAX(0, atoi(argv[i + 1]));
        } else if(strcmp(argv[i], "-m") == 0) {
            params.n_stop = MAX(0, atoi(argv[i + 1]));
        } else if(strcmp(argv[i], "-d") == 0) {
            params.debug = TRUE;
        } else if(strcmp(argv[i], "-F") == 0) {
            params.skip_existing = FALSE;
        } else if(strcmp(argv[i], "-t") == 0) {
            params.threads = MAX(1, atoi(argv[i + 1])) + 1;  // +1 for the image reading thread
        }
    }

    // Check filename templates
    if(!CheckTemplate(argv[argc - 1], 2)) // Fatal
        exit(-1);
    if(strlen(params.outfilename) > 2) {
        if(!CheckTemplate(params.outfilename, 1)) // Delete user selected output filename template
            params.outfilename[0] = '\0';
    }

    // Check the first frame to determine template and frame sizes
    char fname1[256], fname2[256];
    set_frame_filename_from_template(fname1, fname2, params.n_start, argv[argc - 1]);
    if((whichtemplate = CheckFrames(fname1, fname2, &params.framewidth, &params.frameheight)) < 0) exit(-1);
    if(params.debug) {
        fprintf(stderr, "%s() - frame dimensions: %li × %li\n", argv[0], params.framewidth, params.frameheight);
        fprintf(stderr, "%s() - Expect frame template %d\n", argv[0], whichtemplate + 1);
    }

    if(params.outwidth < 0) {
        params.outwidth = template[whichtemplate].equi_width;
        params.outheight = params.outwidth / 2;
    }

    load_lltable(argv[0]);
}


int main(int argc, char** argv) {
    test_read_from_stdin();

    init(argc, argv);


    if(params.debug) fprintf(stderr, "%s() - Starting threads\n", argv[0]);

    params.threads = MIN(params.threads, params.n_stop);

    pthread_t thread[params.threads];
    THREAD_DATA data[params.threads];

    pthread_mutex_t mutex_counter = PTHREAD_MUTEX_INITIALIZER;
    size_t shared_counter = params.n_start;

    // Initialize the thread data
    for(size_t thread_id = 0; thread_id < params.threads - 1; thread_id++) {
        data[thread_id].worker_id = thread_id;
        data[thread_id].counter_mutex = &mutex_counter;
        data[thread_id].ip_shared_counter = &shared_counter;
        data[thread_id].progName = argv[0];
        data[thread_id].last_argument = argv[argc - 1];
        data[thread_id].frame_input1 = NULL;
        data[thread_id].frame_input2 = NULL;
        data[thread_id].frame_spherical = NULL;
    }

    pthread_t image_reader_thread;
    int creating_thread_status = pthread_create(&(image_reader_thread), NULL, image_reader_worker_function, (void*)&data[0]);
    if(creating_thread_status) {
        if(params.debug) { fprintf(stderr, "Error creating image reading thread 00, exiting.\n"); }
        exit(-1);
    } else if(params.debug) {
        if(params.debug) { fprintf(stderr, "%s() - Started image reading thread 00\n", argv[0]); }
    }


    // thread_id = 1, since id=0 thread_id is reserved for the image_reading_thread
    for(size_t thread_id = 1; thread_id < params.threads; thread_id++) {
        // Malloc images (once, then reuse in the same thread)
        data[thread_id].frame_input1 = Create_Bitmap(params.framewidth, params.frameheight);
        data[thread_id].frame_input2 = Create_Bitmap(params.framewidth, params.frameheight);
        data[thread_id].frame_spherical = Create_Bitmap(params.outwidth, params.outheight);

        int creating_thread_status = pthread_create(&(thread[thread_id]), NULL, image_processing_worker_function, (void*)&data[thread_id]);
        if(creating_thread_status) {
            if(params.debug) { fprintf(stderr, "Error creating thread %02li, exiting.\n", thread_id); }
            exit(-1);
        } else if(params.debug) {
            if(params.debug) { fprintf(stderr, "%s() - Started t %02li\n", argv[0], thread_id); }
        }

        if(params.threads > 1) {
            // spread start of threads out a bit so compute and io time do not overlap at the start
            usleep(1000.0 / (params.threads));
        }
    }

    for(size_t thread_id = 0; thread_id < params.threads; ++thread_id) {
        pthread_join(thread[thread_id], NULL);

        Destroy_Bitmap(data[thread_id].frame_input1);
        Destroy_Bitmap(data[thread_id].frame_input2);
        Destroy_Bitmap(data[thread_id].frame_spherical);

        if(params.debug) { fprintf(stderr, "Thread: %02li done\n", thread_id); }
    }

    free(g_lltable);
    exit(0);
}


void set_frame_filename_from_template(char* fname1, char* fname2, int nframe, const char* last_argument) {
    sprintf(fname1, last_argument, 0, nframe);
    sprintf(fname2, last_argument, 5, nframe);
}


// calculates spherical image from two input images
void calc_spherical(BITMAP4* frame_input1, BITMAP4* frame_input2, BITMAP4* spherical_out) {
    int itable = 0;
    for(int j = 0; j < params.outheight; ++j) {
        //y0 = j / (double)params.outheight;
        //if (params.debug && j % (params.outheight/32) == 0)
        //fprintf(stderr,"%s() - Scan line %d\n",progName,j);

        for(int i = 0; i < params.outwidth; ++i) {
            //x0 = i / (double)params.outwidth;
            COLOUR16 csum = { 0, 0, 0 }; // Supersampling antialising sum

            // Antialiasing loops
            for(size_t aj = 0; aj < params.antialias; aj++) {
                //y = y0 + aj / dy; // 0 ... 1

                // Antialiasing loops
                for(size_t ai = 0; ai < params.antialias; ai++) {
                    //x = x0 + ai / dx; // 0 ... 1

                    // Calculate latitude and longitude
                    //longitude = x * TWOPI - M_PI;    // -pi ... pi
                    //latitude = y * M_PI - M_PI/2;    // -pi/2 ... pi/2
                    int face = g_lltable[itable].face;
                    UV uv = g_lltable[itable].uv;
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
            spherical_out[index].r = csum.r / params.antialias2;
            spherical_out[index].g = csum.g / params.antialias2;
            spherical_out[index].b = csum.b / params.antialias2;
        }
    }
}


// Read both frames
void read_image_pair(const int nframe, THREAD_DATA* data) {
    char fname1[256], fname2[256];
    set_frame_filename_from_template(fname1, fname2, nframe, data->last_argument);

    if(!ReadFrame(data->frame_input1, fname1, params.framewidth, params.frameheight)) {
        if(params.debug) fprintf(stderr, "%s() T%02li - failed to read frame \"%s\"\n", data->progName, data->worker_id, fname2);
        return;
    }

    if(!ReadFrame(data->frame_input2, fname2, params.framewidth, params.frameheight)) {
        if(params.debug) fprintf(stderr, "%s() T%02li - failed to read frame \"%s\"\n", data->progName, data->worker_id, fname2);
        return;
    }
}


void process_single_image(THREAD_DATA* data, int nframe) {
    char fname1[256], fname2[256];
    set_frame_filename_from_template(fname1, fname2, nframe, data->last_argument);

    // skip calculation of frame if output file exists
    if(params.skip_existing) {
        // Create the output file name
        char fname_out[256];
        create_output_filename(fname_out, fname1, nframe);

        if(access(fname_out, F_OK) == 0) {
            if(params.debug) { fprintf(stderr, "%s() T%02li - skipping frame, already exists \"%s\"\n", data->progName, data->worker_id, fname_out); }
            return;
        } else if(params.debug) {
            fprintf(stderr, "%s() T%02li - NOT skipping frame \"%s\"\n", data->progName, data->worker_id, fname_out);
        }
    }

    if(data->frame_input1 == NULL || data->frame_input2 == NULL || data->frame_spherical == NULL) {
        fprintf(stderr, "%s() T%02li - Failed to malloc memory for the images\n", data->progName, data->worker_id);
        exit(-1);
    }

    // Form the spherical map
    if(params.debug) {
        fprintf(stderr, "%s() T%02li - Creating spherical map for frame %d\n", data->progName, data->worker_id, nframe);

        BITMAP4 black = { 0, 0, 0, 255 };
        Erase_Bitmap(data->frame_spherical, params.outwidth, params.outheight, black);
    }

    read_image_pair(nframe, data);

    double starttime = GetRunTime();
    calc_spherical(data->frame_input1, data->frame_input2, data->frame_spherical);
    if(params.debug) { fprintf(stderr, "%s() T%02li - Processing time: %g seconds\n", data->progName, data->worker_id, GetRunTime() - starttime); }

    // Write out the equirectangular
    // Base the name on the name of the first frame
    if(params.debug) fprintf(stderr, "%s() T%02li - Saving equirectangular\n", data->progName, data->worker_id);
    WriteSpherical(fname1, nframe, data->frame_spherical, params.outwidth, params.outheight);
}


/*
    Check the frames
    - do they exist
    - are they jpeg or png
    - are they the same size
    - determine which frame template we are using
*/
int CheckFrames(const char* fname1, const char* fname2, size_t* width, size_t* height) {
    boolean frame1_is_jpg = IsJPEG(fname1);
    boolean frame2_is_jpg = IsJPEG(fname2);
    boolean frame1_is_png = IsPNG(fname1);
    boolean frame2_is_png = IsPNG(fname2);

    if((!frame1_is_jpg && !frame1_is_png) || (!frame2_is_jpg && !frame2_is_png)) {
        fprintf(stderr, "CheckFrames() - frame name does not look like a jpeg or png file\n");
        return (-1);
    }

    if(params.debug) fprintf(stderr, "fname1=%s fname2=%s\n", fname1, fname2);

    FILE* fptr;
    // Frame 1
    if((fptr = fopen(fname1, "rb")) == NULL) {
        fprintf(stderr, "CheckFrames() - Failed to open first frame \"%s\"\n", fname1);
        return (-1);
    }
    int w1 = -1, h1 = -1, depth;
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
    if(frame2_is_jpg) {
        JPEG_Info(fptr, &w2, &h2, &depth);
    } else {
        PNG_Info(fptr, &w2, &h2, &depth);
    }
    fclose(fptr);

    // Are they the same size
    if(w1 != w2 || h1 != h2) {
        fprintf(stderr, "CheckFrames() - Frame sizes don't match, %d != %d or %d != %d\n", w1, h1, w2, h2);
        fprintf(stderr, "CheckFrames() - Frame sizes don't match, %d != %d or %d != %d\n", w1, w2, h1, h2);
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

// params.skip_existing
void create_output_filename(char* fname_out, const char* basename, int nframe) {
    if(strlen(params.outfilename) < 2) {
        sprintf(fname_out, basename, 0, nframe);
        for(int i = strlen(fname_out) - 1; i > 0; i--) {
            if(fname_out[i] == '.') {
                fname_out[i] = '\0';
                break;
            }
        }
        strcat(fname_out, "_sphere.png");
    } else {
        sprintf(fname_out, params.outfilename, nframe);
    }
}


/*
   Write spherical image
    The file name is either using the mask params.outfilename which should have a %d for the frame number
    or based upon the basename provided which will have two %d locations for track and framenumber
*/
int WriteSpherical(const char* basename, int nframe, const BITMAP4* img, int w, int h) {
    // Create the output file name
    char fname_out[256];
    create_output_filename(fname_out, basename, nframe);

    if(params.debug) fprintf(stderr, "WriteSpherical() - Saving file \"%s\"\n", fname_out);

    // Save
    FILE* fptr;
    if((fptr = fopen(fname_out, "wb")) == NULL) {
        fprintf(stderr, "WriteSpherical() - Failed to open output file \"%s\"\n", fname_out);
        return (FALSE);
    }

    if(PNG_Write(fptr, img, w, h, FALSE)) { fprintf(stderr, "WriteSpherical() - Failed to write output file \"%s\"\n", fname_out); }
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
void init_default_params(void) {
    params.outwidth = -1;
    params.outheight = -1;
    params.framewidth = -1;
    params.frameheight = -1;
    params.antialias = 2;
    params.antialias2 = 4; // antialias squared
    params.n_start = 0;
    params.n_stop = 100000;
    params.outfilename[0] = '\0';
    params.debug = FALSE;
    params.threads = MAX(1, sysconf(_SC_NPROCESSORS_ONLN)) + 1; // +1 for the image reading thread
    params.skip_existing = TRUE;

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
    fprintf(stderr,
            "The sequence filename template should contain two %%d entries. The first will be populated with the track "
            "number 0 or 5, the second is the frame sequence number, see -n and -m below.\n");
    fprintf(stderr, "So for example, if there are 1000 frames called track0_frame0001.png, track5_0001.png, ...\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "then the program might be called as follows:\n");
    fprintf(stderr, "   %s -w 4096 -n 1 -m 1000 track%%d_frame%%04d.png\n", s);
    fprintf(stderr, "\n");
    fprintf(stderr, "Or if directories are used with frames track0/frame1.png, track5/1000.png, ...\n");
    fprintf(stderr, "   %s -w 4096 -n 1 -m 1000 track%%d/frame%%4d.png\n", s);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options\n");
    fprintf(stderr, "   -w n      Sets the output image width,      default: %d\n", params.outwidth);
    fprintf(stderr, "   -a n      Sets antialiasing level,          default: %li\n", params.antialias);
    fprintf(stderr,
            "   -o s      Specify the output filename template, default is based on track 0 name uses track 2. If "
            "specified then it should contain one %%d field for the frame number\n");
    fprintf(stderr, "   -n n      Start index for the sequence,     default: %li\n", params.n_start);
    fprintf(stderr, "   -m n      End index for the sequence,       default: %li\n", params.n_stop);
    fprintf(stderr, "   -t n      Amount of threads to use,         default: %li\n", params.threads);
    fprintf(stderr, "   -d        Enable debug mode,                default: off\n");
    fprintf(stderr, "   -F        Overwrite existing output images, default: off\n");
}
