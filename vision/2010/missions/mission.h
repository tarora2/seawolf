//header for mission-related files 

#include <stdbool.h>
#include <highgui.h>
#include <opencv/cv.h>

#define WAIT 0
//EACH REAL MISSION INCLUDES FINDING THE NEXT APPROPRIATE MARKER (SINCE THAT IS SPECIFIC TO EACH MISSION) THEN ALLIGN_PATH ALLIGNS US WITH THAT MARKER
#define GATE 1 
#define GATE_PATH 11
#define BOUEY 3
#define BOUEY_PATH 33
#define BARBED_WIRE 4
#define BARBED_WIRE_ALLIGN 42
#define BARBED_WIRE_PATH 44
#define TORPEDO 5
#define TORPEDO_PATH 55
#define BOMBING_RUN 6
#define BOMBING_RUN_PATH 66
#define BOMBING_RUN_2 61
#define BOMBING_RUN_2_PATH 661
#define BRIEFCASE 7
#define BRIEFCASE_GRAB 77
#define OCTOGON 8
#define ALLIGN_PATH 9 //CALLED AFTER FINDING THE CORRECT PATH AFTER EACH MISSION, ONCE COMPLETED, RETURN RETURN TASK COMPLETE
#define IDENTIFY_SILHOUET 10
#define TUNA_BLOB 12
#define MOTION 13

