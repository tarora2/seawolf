
#include "seawolf.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#include "seawolf3.h"

#include "util.h"
#include "vision_lib.h"
#include <cv.h>
#include <highgui.h>

#include "mission.h"

/******* #DEFINES for BOUY **********/

// Definitions for the bouy colors NOT THE ORDER, SEE BELOW
#define YELLOW_BOUY 1
#define RED_BOUY    2
#define GREEN_BOUY  3
#define SUNSPOT_BOUY 4 //we can't avoid it unless we acount for it

//order of bouys from left to right
static int bouy_order[] = {
    [RED_BOUY]    = 2,
    [GREEN_BOUY]  = 3,
    [YELLOW_BOUY] = 1
};

//depths of each bouy from the surface in feet
static float bouy_depth[] = {
    [RED_BOUY]    = 4.0,
    [GREEN_BOUY]  = 4.0,
    [YELLOW_BOUY] = 4.0
};

// The order to hit the bouys in
#define BOUY_1 RED_BOUY
#define BOUY_2 GREEN_BOUY

// States for the bouy state machine
#define BOUY_STATE_FIRST_APPROACH 0
#define BOUY_STATE_FIRST_ORIENTATION 1
#define BOUY_STATE_BUMP_FIRST_BOUY 2
#define BOUY_STATE_FIRST_BACKING_UP 3
#define BOUY_STATE_SECOND_ORIENTATION 4
#define BOUY_STATE_BUMP_SECOND_BOUY 5
#define BOUY_STATE_FINAL_ORIENTATION 6
#define BOUY_STATE_PASS_BOUYS 7
#define BOUY_STATE_ORIENT_FOR_SEARCH 8
#define BOUY_STATE_ALLIGN_FOR_SEARCH 9
#define BOUY_STATE_SEARCHING_FOR_PATH 10
#define BOUY_STATE_COMPLETE 11

// How fast we go throughout most of the mission
#define FORWARD_SPEED 15

// How fast we back up.  Should be positive
#define BACKING_SPEED 15

// How many frames we ignore at the beginning of the mission
#define STARTUP_FRAMES 9

// How Long We Must See a Blob Durring Approach
#define APPROACH_THRESHOLD 8

// How large blobs must be
#define MIN_BLOB_SIZE 200

// What fraction of the found color has to be taken up by a blob for us to
// consider it a blob
#define BLOB_COLOR_FRACTION (3.0/4.0)

// Turn Rate When Searching For Bouys
#define TURN_RATE 5

// How Long To Back Up After 1st Bouy
#define BACK_UP_TIME_1 7

// How long we are chasing a bouy before we fix our heading
#define FORWARD_TRACKING_AMOUNT 10

// How many frames depth must be centered before moving forward
#define DEPTH_CENTER_COUNT 4

// How close to the center of the frame the bouy must be before we consider it in the center
#define VERTICAL_THRESHOLD 25

// How long we must track a bouy before activating depth control
#define TRACKING_THRESHOLD 6

// How Far to Turn When Looking for a Bouy
#define TURNING_THRESHOLD_1 70

// How Far to Turn When Looking for a Bouy
#define TURNING_THRESHOLD_2 70

// How long in SECONDS after we first saw a blob in bouy_bump before we give up and say we hit it.
#define BOUY_BUMP_TIMER 7

// How many consecutive frames we must see a bouy before thinking we've hit it
#define MAX_TRACKING_COUNT 45

// How many frames since first seeing a large blob before we think we've hit a bouy
#define BIG_BLOB_WAIT 10

// Y value for where a blob is found is multiplied by this to get the relative
// depth heading
#define DEPTH_SCALE_FACTOR (1.0/200.0)

// How far to turn after bumping the second bouy in degrees
#define TURN_AMOUNT_AFTER_SECOND_BOUY_BUMP 70

// How many frames we need to see an orangle blob to think we've seen the path
#define SEEN_PATH_THRESHOLD 3

// Bigger the number, the less we turn
#define YAW_SCALE_FACTOR 4

//how long each leg of the search should be
#define SEARCH_PATTERN_TIME 8

//How well we have to see a blob to think it's the path
#define PATH_FRACTION_THRESHOLD .75

//How long to drive through the bouy mission when finished in seconds
#define PASSING_TIME 3

/************* STATE VARIABLES FOR BOUY *************/

// State Variables for Bouy Mission
double initial_angle = 0;
static int bouy_state = 0;           //Keeps track of primary bouy state machine
static int bouys_found = 0;          //turns to 1,2, or 3 when a bouy is found, # signifies color
static RGBPixel bouy_colors[] = {    //holds the three colors of the bouys

    // Competition measured
    // Midday.  Competition side
    // Background was about 72b7ff
    // Red: san_diego_day1/62/00430.jpg
    // Yellow: san_diego_day1/62/00465.jpg
    //[YELLOW_BOUY]  = {0x9E, 0xE6, 0xFF },
    //[RED_BOUY]     = {0xF4, 0xA8, 0xFF },
    //[GREEN_BOUY]   = {0x5B, 0xE8, 0xFF },
    //[SUNSPOT_BOUY] = {0xff, 0xff, 0xff },

    // Pure white for yellow
    //[YELLOW_BOUY]  = {0xFF, 0xFF, 0xFF },
    // funny guess for yellow
    [YELLOW_BOUY] = {0x80, 0xff, 0xff},

    // Ideal Colors
    //[YELLOW_BOUY]  = {0xff, 0xff, 0x00 },
    [RED_BOUY]     = {0xff, 0x00, 0x00 },
    [GREEN_BOUY]   = {0x00, 0xff, 0x00 },
    //[SUNSPOT_BOUY] = {0xff, 0xff, 0xff },

};


// State Variables for BOUY - First Approach sub routine
static int startup_counter = 0;
static int approach_counter = 0;  //counts how many frames we've seen any blob

// State Variables for BOUY - First Orientation sub routine
static double starting_angle;

// State variables for BOUY - Bump Bouy Sub Routine
static int lost_blob = 0;         //how long it's been since we lost the blob
static int tracking_counter = 0;  //total number of frames we've seen a blob
static int saw_big_blob = 0;      //starts incrementing once we see a big enough blob
static int hit_blob = 0;          //increments for every frame we think we've hit the blob
static int bump_initialized = 0;  //flag to keep track of initializing bump routine
static Timer* bouy_timer = NULL;      //time how long since we first saw the bouy
static int turn_counter = 0;      //count # of times we've turned around looking for bouy
// State Variables for Bouy - First Backing Up
static Timer* backing_timer = NULL;      //times our backing up step for us
static int depth_counter; // How long we've centered vertically
static int forward_counter; // How long we've been going forward

// State Variables for Dead Reakoning At End
double heading;
double target;

// State Variables for Search Pattern For Path
static int search_direction = 0; //which direction we need to turn next (1 or 2)
static int initial_leg_length = 0; //how long our first leg of the search pattern should be
static int search_pattern_turning = 0; //flags we are completing a turn
static int seen_orange_blob = 0; //lets us know if we have found the path
static Timer* search_timer = NULL; //times the legs of our search
static int first_search_leg = 1; //true when we are on our first search leg
static RGBPixel PathColor = {0xff, 0x88, 0x00};
static Timer* passing_timer = NULL; //times how long we drive through bouys

RGBPixel find_blob_avg_color(IplImage* img, BLOB* blob);
RGBPixel find_blob_avg_color(IplImage* img, BLOB* blob) {
    unsigned int avg_r = 0;
    unsigned int avg_g = 0;
    unsigned int avg_b = 0;
    CvPoint* pixel = blob->pixels;
    for (int i=0; i<blob->area-1 && i < 50000; i++) {
        avg_r += (unsigned int) img->imageData[img->widthStep*pixel->y + 3*pixel->x + 2];
        avg_g += (unsigned int) img->imageData[img->widthStep*pixel->y + 3*pixel->x + 1];
        avg_b += (unsigned int) img->imageData[img->widthStep*pixel->y + 3*pixel->x + 0];
        pixel++;
    }
    avg_r = avg_r / blob->area;
    avg_g = avg_g / blob->area;
    avg_b = avg_b / blob->area;
    RGBPixel average = {avg_r, avg_g, avg_b};
    return average;
}

void mission_bouy_init(IplImage * frame, struct mission_output* result)
{
    startup_counter = 0;
    bouy_state = BOUY_STATE_FIRST_APPROACH;
    bouy_bump_init();
    bouy_first_approach_init();
    result->depth_control = DEPTH_ABSOLUTE;
    result->depth = 4.0;
    bump_initialized = 0;
    search_pattern_turning = 0;
    seen_orange_blob = 0;
    first_search_leg = 1;
    depth_counter = 0;
    forward_counter = 0;

    if (backing_timer != NULL) {
        Timer_destroy(backing_timer);
    }
    backing_timer = NULL;

    if (search_timer != NULL) {
        Timer_destroy(search_timer);
    }
    search_timer = NULL;
    
    if (passing_timer != NULL) {
        Timer_destroy(passing_timer);
    }
    passing_timer = NULL;

    initial_angle = Var_get("SEA.Yaw");

}

struct mission_output mission_bouy_step (struct mission_output result)
{

    switch (bouy_state) {

        case BOUY_STATE_FIRST_APPROACH:
            //drive forward
            result.rho = FORWARD_SPEED;

            //scan the image for any of the three bouys
            bouys_found = bouy_first_approach(&result);

            if (++startup_counter >= STARTUP_FRAMES) {

                // if we see a bouy, move on
                if(bouys_found){
                    printf("we finished the approach \n");
                    bouy_state++;
                } else {
                    // Don't break if we're going to change states, or we would
                    // waste a frame
                    break;
                }

            } else {
                bouy_first_approach_init();
                break;
            }

        case BOUY_STATE_FIRST_ORIENTATION:

            //set our depth to the first bouy we need to see
            result.depth = bouy_depth[BOUY_1];

            //turn towards our first bouy
            result.yaw_control = ROT_MODE_RATE;

            //record what angle we started this turn
            starting_angle = Var_get("SEA.Yaw");

            if(bouy_order[bouys_found] > bouy_order[BOUY_1]){
                //TURN LEFT
                printf("Going LEFT to desired bouy\n");
                result.rho = 0;
                result.yaw = -1*TURN_RATE;
            }
            else if(bouy_order[bouys_found] < bouy_order[BOUY_1]){
                //TURN RIGHT
                result.rho = 0;
                printf("Going RIGHT to desired bouy\n");
                result.yaw = TURN_RATE;
            }
            else{
                printf("Stopping to face desired bouy STRAIGHT.\n");
                result.rho = 0;
                //STAY STRAIGHT
            }
            bouy_state++;
            // Move onto next case without a break so that we don't waste a
            // frame

        case BOUY_STATE_BUMP_FIRST_BOUY:

            //initialize bouy bump
            if(bump_initialized == 0){
                bouy_bump_init();
            }

            //run bump routine until complete
            if( bouy_bump(&result, BOUY_1) == 1){
                bouy_state++;
                bump_initialized = 0;
                printf("we finished bouy bump 1 \n");
                turn_counter = 0;
            }else{
                //grab angle data to check how far we have turned
                double current_angle = Var_get("SEA.Yaw");
                if(!(fabs(current_angle-starting_angle) < TURNING_THRESHOLD_1 ||
                    fabs(current_angle-starting_angle) > 360-TURNING_THRESHOLD_1)){
                        //We have turned too fari
                        turn_counter++;
                        result.yaw *= -1;
                        starting_angle = current_angle;
                    if(turn_counter%2 == 0){
                         result.rho = 5;
                    }else{
                        result.rho = 0;
                    }
                }
            }
            break;

        case BOUY_STATE_FIRST_BACKING_UP:

            //set our depth to the second bouy
            result.depth_control = DEPTH_ABSOLUTE;
            result.depth = bouy_depth[BOUY_2];

            //back up
            result.rho = -BACKING_SPEED;
            printf("we are backing up \n");

            //back up for X seconds
            if( backing_timer == NULL){
                backing_timer = Timer_new();
            }else{
                //kill some time
                Util_usleep(0.1);
            }

            if(Timer_getTotal(backing_timer) > BACK_UP_TIME_1){
                //we have backed up long enough
                bouy_state++;
                result.rho = 0;
                printf("We are done backing up \n");
            }

            break;

        case BOUY_STATE_SECOND_ORIENTATION:
            //turn towards our second bouy
            result.yaw_control = ROT_MODE_RATE;

            if(bouy_order[BOUY_2] < bouy_order[BOUY_1]){
                //TURN_LEFT
                result.yaw = -1*TURN_RATE;
                printf("Turning Left, towards the second bouy \n");
            }
            else if(bouy_order[BOUY_2] > bouy_order[BOUY_1]){
                //TURN_RIGHT
                result.yaw = TURN_RATE;
                printf("Turning Right, towards the second bouy \n");
            }
            else{
                printf("Why are we hitting the same bouy twice? really? are we that vindictive? what did that bouy ever do to us?");
            }

            //move on
            bouy_state++;

            break;

        case BOUY_STATE_BUMP_SECOND_BOUY:

            //initialize bouy bump
            if(bump_initialized == 0){
                printf("initializing bump for bouy 2\n");
                bouy_bump_init();
            }

            //run bump routine until complete
            if( bouy_bump(&result, BOUY_2) == 1){
                bouy_state++;
                bump_initialized = 0;
                turn_counter = 0;
                printf("we finished bouy bump 2 \n");
            }else{
                //grab angle data to check how far we have turned
                double current_angle = Var_get("SEA.Yaw");
                if(!(fabs(current_angle-starting_angle) < TURNING_THRESHOLD_2 ||
                    fabs(current_angle-starting_angle) > 360-TURNING_THRESHOLD_2)){
                        //We have turned too fari
                        turn_counter++;
                        result.yaw *= -1;
                        starting_angle = current_angle;
                    if(turn_counter%2 == 0){
                         result.rho = 10;
                    }else{
                         result.rho = 0;
                    }
                }
                break;
            }
        case BOUY_STATE_FINAL_ORIENTATION:
            //line up with our initial heading, preparing to swim a short
            // ways beyond the line of bouys
            result.yaw_control = ROT_MODE_ANGULAR;
            result.yaw = initial_angle;

            //stop forward motion
            result.rho = 0;
        
            if(fabs(Var_get("SEA.Yaw") - initial_angle) < 10){
                //we are lined up
                bouy_state++;
                printf("pointing beyond the bouys\n");
            }else{
                break;
            }
        
        case BOUY_STATE_PASS_BOUYS: 
            
            result.rho = FORWARD_SPEED;
            
            if(passing_timer == NULL){
                passing_timer = Timer_new();
            }

            if(Timer_getTotal(passing_timer) >= PASSING_TIME){
                //ASSUME WE HAVE PASSED THE BOUYS
                bouy_state++;
                printf("we are beyond the bouys\n");
                Timer_destroy(passing_timer);
                passing_timer = NULL;
            } else {
                break;
            }
        case BOUY_STATE_ORIENT_FOR_SEARCH:
            printf("orienting to begin search\n");
            
            //double check that we are set to angular
            result.yaw_control = ROT_MODE_ANGULAR;

            if(bouy_order[BOUY_2] == 1){
                //turn right
                result.yaw = ((int)initial_angle + 60 +180)%360 - 180;
                search_direction = -1;
                printf("turning right\n");
            }else if(bouy_order[BOUY_2] == 2){
                //arbitrarily turn right
                result.yaw = ((int)initial_angle + 60 +180)%360 - 180;
                search_direction = -1;
                printf("turning right\n");
            }else if(bouy_order[BOUY_2]== 3){
                //turn left
                result.yaw = ((int)initial_angle - 60 +180)%360 - 180;
                search_direction = 1;
                printf("turning left\n");
            }else{
                printf("BOUY_2 DEFINED INCORRECTLY\n");
            }
            bouy_state++;

        case BOUY_STATE_ALLIGN_FOR_SEARCH:

            //if our heading is close enough to
            heading = Var_get("SEA.Yaw");
            target = Var_get("Rot.Angular.Target");
            printf("orienting for search: heading = %f, target = %f, init angle = %f \n",heading,target,initial_angle);
            if(fabs(heading-target) < 10){
                printf("FINISHED orienting for search pattern\n");
                bouy_state++;
            }
            break;

        case BOUY_STATE_SEARCHING_FOR_PATH:
            //perform a search patter to begin looking for the path


            if(search_pattern_turning == 0){
                printf("going forward in search\n");
                if( search_timer == NULL){
                    //start a timer
                    search_timer = Timer_new();
                } else if(Timer_getTotal(search_timer) > SEARCH_PATTERN_TIME ||
                          (Timer_getTotal(search_timer) > SEARCH_PATTERN_TIME/2 && bouy_order[BOUY_2] == 2 && first_search_leg ==1) ){
                    //we've been driving long enough
                    Timer_destroy(search_timer);
                    search_timer = NULL;
                    first_search_leg = 0;
                    search_pattern_turning = 1;
                } else {
                    //go forward
                    result.rho = 15;
                }
            }else if (search_pattern_turning == 1){
                //initiate turn
                printf("initiating turn.  initial_angle = %f, first yaw = %f",initial_angle,result.yaw);
                result.yaw = (int)(initial_angle + 75 * search_direction+180)%360-180;
                printf("new yaw = %f\n",result.yaw);
                search_direction *= -1;
                search_pattern_turning = 2;
            }else{
                printf("turning in search\n");
                //continue turning
                result.rho = 0;

                //if our heading is close enough, finish turn
                heading = Var_get("SEA.Yaw");
                target = Var_get("Rot.Angular.Target");

                if(fabs(heading-target) < 10){
                    search_pattern_turning = 0;
                }
            }

            //Check the Down Cam for an Orange Blob (the path)
            IplImage* frame = multicam_get_frame (DOWN_CAM);
            result.frame = frame;
            //frame = normalize_image(frame);

            IplImage* ipl_out = cvCreateImage(cvGetSize (frame), 8, 3);
            int num_pixels = FindTargetColor(frame, ipl_out, &PathColor , 1, 310, 1.5);

            BLOB* path_blob;
            int blobs_found = blob(ipl_out, &path_blob, 4, MIN_BLOB_SIZE);

            if((blobs_found == 1 || blobs_found == 2) &&
                (float)path_blob->area / num_pixels > PATH_FRACTION_THRESHOLD ){
                if(++seen_orange_blob > SEEN_PATH_THRESHOLD){
                    bouy_state++;
                }
            } else {
                seen_orange_blob = 0;
            }

            //free blob resources
            blob_free (path_blob, blobs_found);
            cvReleaseImage (&ipl_out);
            break;

        case BOUY_STATE_COMPLETE:
            result.mission_done = true;
            break;

        default:
            printf("bouy_state set to meaningless value");
            break;
    }

    return result;
}

/**
 * find_closest_blob
 * Takes in n colors and n blobs so that the nth blob corresponds to the nth
 * color.  This function finds which blob it closest to it's target color and
 * returns it.
 *
 * We did not get good results from using this.  It may have bugs in it, or it
 * may just be a bad idea.
 */
int find_closest_blob(int n, RGBPixel colors[], BLOB* blobs[], IplImage* image) {

    // Gather information about what color blobs we're seeing
    double min_distance = sqrt(255*255*255);
    int closest_blob = 0;
    for (int i=0; i<n; i++) {

        RGBPixel average = find_blob_avg_color(image, blobs[i]);
        double distance = Pixel_stddev(&colors[i], &average);
        if (distance < min_distance) {
            min_distance = distance;
            closest_blob = i;
        }

    }
    return closest_blob;
}

int find_bouy(IplImage* frame, BLOB** found_blob, int* blobs_found_arg, int target_color) {

    int found_bouy = 0;

    IplImage* ipl_out[3];
    ipl_out[0] = cvCreateImage(cvGetSize (frame), 8, 3);
    ipl_out[1] = cvCreateImage(cvGetSize (frame), 8, 3);
    ipl_out[2] = cvCreateImage(cvGetSize (frame), 8, 3);
    //ipl_out[3] = cvCreateImage(cvGetSize (frame), 8, 3);

    int num_pixels[3];                                                 //color thresholds
    num_pixels[0] = FindTargetColor(frame, ipl_out[0], &bouy_colors[YELLOW_BOUY], 1, 280, 3.5);
    num_pixels[1] = FindTargetColor(frame, ipl_out[1], &bouy_colors[RED_BOUY], 1, 360, 2.0);
    num_pixels[2] = FindTargetColor(frame, ipl_out[2], &bouy_colors[GREEN_BOUY], 1, 280, 2.5);
    //num_pixels[3] = FindTargetColor(frame, ipl_out[3], &bouy_colors[SUNSPOT_BOUY], 1, 90, 2);

    // Debugs
    cvNamedWindow("Yellow", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("Red", CV_WINDOW_AUTOSIZE);
    cvNamedWindow("Green", CV_WINDOW_AUTOSIZE);
    //cvNamedWindow("Sunspot White", CV_WINDOW_AUTOSIZE);
    cvMoveWindow("Yellow",0,150);
    cvMoveWindow("Red",300,150);
    cvMoveWindow("Green",600,150);
    //cvMoveWindow("Sunspot White",900,150);
    cvShowImage("Yellow", ipl_out[0]);
    cvShowImage("Red", ipl_out[1]);
    cvShowImage("Green", ipl_out[2]);
    //cvShowImage("Sunspot White", ipl_out[3]);

    //Look for blobs
    BLOB* blobs[3];
    int blobs_found[3];
    blobs_found[0] = blob(ipl_out[0], &blobs[0], 4, MIN_BLOB_SIZE);
    blobs_found[1] = blob(ipl_out[1], &blobs[1], 4, MIN_BLOB_SIZE);
    blobs_found[2] = blob(ipl_out[2], &blobs[2], 4, MIN_BLOB_SIZE);
    //blobs_found[3] = blob(ipl_out[3], &blobs[3], 4, MIN_BLOB_SIZE);

    printf("Blobs found: y=%d r=%d g=%d\n", blobs_found[0], blobs_found[1], blobs_found[2]);

    int seen_blob[3];
    BLOB* blobs_seen[3];
    RGBPixel colors_seen[3];
    int indexes_seen[3];
    int num_colors_seen = 0;
    static int max_blob_size = 1000000000;
    for (int i=0; i<3; i++) {
        //printf("blobs[%d]->area = %ld\n", i, blobs[i]->area);
        if ((blobs_found[i] == 1 || blobs_found[i] == 2) &&
            blobs[i]->area < max_blob_size &&
            ((float)blobs[i]->area) / ((float)num_pixels[i]) > BLOB_COLOR_FRACTION)
        {
            //printf("i=%d\n", i);
            blobs_seen[num_colors_seen] = blobs[i];
            colors_seen[num_colors_seen] = bouy_colors[i+1];
            indexes_seen[num_colors_seen] = i+1;
            num_colors_seen++;
            seen_blob[i] = 1;
        } else {
            //printf("Blob amount / Filter amount = %f\n", ((float)blobs[i]->area) / ((float)num_pixels[i]));
            seen_blob[i] = 0;
        }
    }

    

    // Make a decision baised on how many colors we've seen:
    //  0) We saw nothing
    //  1) The one color we saw is correct
    //  2) Choose the color that's closer to it's target
    //  3) We're getting bad data, assume we saw nothing
    //printf("Number of bouys seen: %d\n", num_colors_seen);
    if (num_colors_seen == 1) {
        found_bouy = indexes_seen[0];
    } else if (num_colors_seen == 2) {
        /*
        found_bouy = indexes_seen[
            find_closest_blob(2, colors_seen, blobs_seen, frame)
        ];
        */
        if (blobs_seen[0]->area > blobs_seen[1]->area) {
            found_bouy = indexes_seen[0];
        } else {
            found_bouy = indexes_seen[1];
        }

        //if the target color's blob is comparable to the found blob, return target
        if(blobs_found[target_color-1]==1 && target_color > 0 && 
            abs(blobs[target_color-1]->area - blobs[found_bouy-1]->area) < 0.1*blobs[found_bouy-1]->area)
        {
            //the two blobs are comparable, pick the one we are looking for
            found_bouy = target_color;
        }

    } else if (num_colors_seen == 0 || num_colors_seen == 3) {
        //we don't see anything
        found_bouy = 0;
    }

    //NEW COLOR-DETERMINING CODE!!!
    //if we see a blob with any of our color filters,
    // send that blob and the frame off to be analyzed 
    // for a final authoritative color analysis
    //if(found_bouy > 0){
        //found_bouy = determine_color(blobs[found_bouy-1],frame);
    //}

    //free resources
    for (int i=0; i<3; i++) {
        if (i == found_bouy-1) {
            *found_blob = blobs[i];
            *blobs_found_arg = blobs_found[i];
        } else {
            blob_free(blobs[i], blobs_found[i]);
        }
        cvReleaseImage(&ipl_out[i]);
    }

    return found_bouy;
}

int determine_color(BLOB* blob, IplImage* frame){
    int found_bouy = 0;
    int i;

    //determine averagae color of the frame
    double imgAverage[3];
    for(i=0;i<3;i++){
        imgAverage[i]=0;
    }
    
    for(i=frame->width*frame->height; i>=0;i--){ 
        // Update the average color
        RGBPixel tempPix;
        tempPix.r = frame->imageData[3*i+2];
        tempPix.g = frame->imageData[3*i+1];
        tempPix.b = frame->imageData[3*i+0];
        if(tempPix.r < 0) printf("negative value\n");
        if(tempPix.g< 0) printf("negative value\n");
        if(tempPix.b< 0) printf("negative value\n");
        imgAverage[0] = (imgAverage[0]*(i)+tempPix.r)/(i+1);
        imgAverage[1] = (imgAverage[1]*(i)+tempPix.g)/(i+1);
        imgAverage[2] = (imgAverage[2]*(i)+tempPix.b)/(i+1); 
    }

    //determine average blob color
    RGBPixel blobAverage;
    blobAverage = find_blob_avg_color(frame,blob);

    printf("blob.r = %d, blob.g = %d, blob.b = %d \n",blobAverage.r,blobAverage.g,blobAverage.b);
    printf("img.r = %f, img.g = %f, img.b = %f \n",imgAverage[0],imgAverage[1],imgAverage[2]);

    //determine which color the blob is based on average color of frame
    if(blobAverage.r > imgAverage[0]){
        //we this blob has more red then average, it's red or yellow
        if(blobAverage.g > imgAverage[1]){
            //this blob has more green then average, it is yellow
            found_bouy = YELLOW_BOUY;
            printf("determined yellow\n");
        }else{
            //this blob is red
            found_bouy = RED_BOUY;
            printf("determined red\n");
        }
    }else{
        if(blobAverage.g > imgAverage[1]){
            //this is teil or green, call it green
            found_bouy = GREEN_BOUY;
            printf("determined green\n");
        }else if(imgAverage[1]-blobAverage.g >imgAverage[0]-blobAverage.r){
            //this is slightly more red than green
            found_bouy = RED_BOUY;
            printf("determined red\n");
        }else{
            //this is slightly more green than red
            found_bouy = GREEN_BOUY;
            printf("determined green\n");
        }
    }

    return found_bouy;
}

/*******      First Approach      ***********/
void bouy_first_approach_init(void) {
    approach_counter = 0;
}

// Scan Image, if we see a bouy, return it's color.  Otherwise
// return zero.
int bouy_first_approach(struct mission_output* result){

    //obtain frame
    IplImage* frame = multicam_get_frame (FORWARD_CAM);
    result->frame = frame;
    //frame = normalize_image(frame);

    BLOB* found_blob = NULL;
    int blobs_found = 0;
    int found_bouy = find_bouy(frame, &found_blob, &blobs_found, 0);

    approach_counter++;
    if (found_bouy == YELLOW_BOUY) {
        //result->depth_control = DEPTH_RELATIVE;
        //result->depth = found_blob->mid.y * DEPTH_SCALE_FACTOR;
        printf("FOUND YELLOW\n");
    } else if (found_bouy == RED_BOUY) {
        //result->depth_control = DEPTH_RELATIVE;
        //result->depth = found_blob->mid.y * DEPTH_SCALE_FACTOR;
        printf("FOUND RED\n");
    } else if (found_bouy == GREEN_BOUY) {
        //result->depth_control = DEPTH_RELATIVE;
        printf("FOUND GREEN\n");
        //result->depth = found_blob->mid.y * DEPTH_SCALE_FACTOR;
    //} else if (found_bouy == SUNSPOT_BOUY) {
    //    printf("FOUND SUNSPOT\n");
    //    approach_counter = 0;
    } else {
        approach_counter = 0;
    }

    //if we've seen a blob for longer than a threshold, finish
    printf("approch_counter=%d\n", approach_counter);
    if(approach_counter <= APPROACH_THRESHOLD){
        //we haven't seen the blob long enough, keep trying
        found_bouy = 0;
    }

    if (found_blob != NULL) {
        blob_free(found_blob, blobs_found);
    }

    return found_bouy;

}


/*******      Bouy Bump      ***********/

void bouy_bump_init(void){
    //initialize state variables for the bouy_bump routine
    tracking_counter = 0;
    saw_big_blob = 0;
    lost_blob = 0;
    hit_blob = 0;
    if (bouy_timer != NULL) {
        Timer_destroy(bouy_timer);
    }
    bouy_timer = NULL;
    bump_initialized = 1;
}

int bouy_bump(struct mission_output* result, int target_bouy){

    int bump_complete = 0;

    //obtain image data
    IplImage* frame = multicam_get_frame (FORWARD_CAM);
    //frame = normalize_image(frame);
    result->frame = frame;
    int frame_width = frame->width;
    int frame_height = frame->height;

    BLOB* found_blob = NULL;
    int blobs_found = 0;
    int found_bouy = find_bouy(frame, &found_blob, &blobs_found, target_bouy);

    // If we saw the wrong color, ignore it
    if (found_bouy != target_bouy) {
        blobs_found = 0;
    }

    // Determine course
    printf ("tracking_counter = %d\n", tracking_counter);
    CvPoint heading;
    if (blobs_found == 0 || blobs_found > 3) {

        // We don't think what we see is a blob
        if (tracking_counter > 6)
        {
            // We have seen the blob for long enough, we may have hit it
            if (++hit_blob > 4)
            {
                //we have probably hit the blob, so let's look for a marker
                printf("We've missed the blob for a while; assuming we hit it.\n");
                bump_complete = 1;
            }
        }
        // We havn't gotten to it yet, but are pretty sure we saw it once
        else if (tracking_counter > 5)
        {
            //don't udpate yaw heading, head for last place we saw the blob

            if (++lost_blob > 100)
            {
                //Something might be wrong.  Keep looking for the blob though
                printf ("WE LOST THE BLOB!!");
            }
        }
        else
        {
            //we arn't even sure we've seen it, so just stay our current course
            tracking_counter = 0;
        }

    }
    else if (++tracking_counter > 2)
    {
        //we do see a blob
        result->yaw_control = ROT_MODE_RELATIVE;

        //modify state variables
        hit_blob = 0;
        lost_blob = 0;

        // Update heading
        // We do this only for a few frames to get a good heading
        if (forward_counter < FORWARD_TRACKING_AMOUNT) {
            printf("setting yaw to chase a blob\n");
            heading = found_blob[0].mid;
            result->yaw = heading.x;
            //adjust to put the origin in the center of the frame
            result->yaw = result->yaw - frame_width / 2;
            //subjectively scale output !!!!!!!
            result->yaw = result->yaw / YAW_SCALE_FACTOR; // Scale Output

            //Convert Pixels to Degrees
            result->yaw = PixToDeg(result->yaw);

            // Start adjusting depth
            if(tracking_counter > TRACKING_THRESHOLD){

                // Depth control
                result->depth_control = DEPTH_RELATIVE;
                result->depth = heading.y;
                cvCircle(result->frame, cvPoint(result->yaw, result->depth), 5, cvScalar(0,0,0,255),1,8,0);
                result->depth = result->depth - frame_height / 2;
                result->depth *= DEPTH_SCALE_FACTOR; // Scaling factor

                if (fabs(heading.y - frame_height/2) < VERTICAL_THRESHOLD && ++depth_counter > DEPTH_CENTER_COUNT) {
                    result->rho = FORWARD_SPEED;
                    forward_counter++;
                } else if (forward_counter) {
                    forward_counter++;
                } else {
                    depth_counter = 0;
                }
            }

            // Init timer
            if (bouy_timer == NULL) {
                bouy_timer = Timer_new();
            }

        }

        //if the blob gets big enough for us to be close, start a countdown
        if(found_blob[0].area > frame_width*frame_height / 4 && saw_big_blob == 0){
            saw_big_blob++;
        }

        if (tracking_counter > MAX_TRACKING_COUNT || saw_big_blob > BIG_BLOB_WAIT)
        {
            result->yaw = 0;
            //result->depth = 0;
            result->rho = 0;
            bump_complete = 1;
            printf ("WE ARE AT THE BLOB ^_^ !!!!\n");
        }

    }

    //check big blob countdown
    if(saw_big_blob > 0){
        if(++saw_big_blob > 5){
            result->yaw = 0;
            //result->depth = 0;
            result->rho = 0;
            bump_complete = 1;
        }
        printf("saw_big_blob = %d \n", saw_big_blob);
    }
//
//   if (bouy_timer != NULL &&
//        Timer_getTotal(bouy_timer) >= BOUY_BUMP_TIMER)
//    {
//            result->yaw = 0;
//            result->depth = 0;
//            result->rho = 0;
//            bump_complete = 1;
//            printf("It's been a while since we've first seen the blob...\n");
//            printf("  We probably either hit it, or failed, so I'm moving on.\n");
//    }

    //RELEASE THINGS
    if (found_blob != NULL) {
        blob_free (found_blob, blobs_found);
    }
    //cvReleaseImage (&frame);

    return bump_complete;
}
