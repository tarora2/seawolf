#include "vision_lib.h"

#include <stdio.h>

#include <cv.h>
#include <highgui.h>

#define MAX_BLOB_AREA 500000

//----------------------------------------------------
//Function: blob
//
// This is takes an image from colorfilter and finds what's left over
// arugment: center_type: 0 returns the middle of the bounding box
//            1 returns the centroid of the blobs

int blob(IplImage* Img, BLOB**  targets, int tracking_number, int minimum_blob_area) {

  int blobnumber; //holds the number of blobs we found

  //find the most massive blob and assigns it to targets
  *targets = findPrimary(Img, tracking_number, minimum_blob_area, &blobnumber);

  int i;
  //now compute the middle of each blob
  for(i=0;i<blobnumber;i++){
    (*targets)[i].mid.x = ((*targets)[i].left+(*targets)[i].right)/2;
    (*targets)[i].mid.y = ((*targets)[i].top + (*targets)[i].bottom)/2;
  }

  //return the number of blobs we found
  return blobnumber;
}


//-----------------------------------------------------------------
//Function: findPrimary
//
// Searches the image for the largest blob, currently returns that blob as a BLOB
//

BLOB* findPrimary(IplImage* Img, int tracking_number, int minimum_blob_area, int *blobnumber){

 //usefull variables 
  int height = Img->height;
  int width = Img->width;  
  int i,x,y;
  int blobs_found=0; 

  //initialize an array of blobs 
  BLOB* blobs;
  *blobnumber = 0; //how many blobs we've found so far

  //Allocate memory for blob variable
  blobs = (BLOB*)calloc(30000,sizeof(BLOB)); //30,000 is estimated max number of blobs

  //create a list of our target blobs, which we will combine into a single blob
  BLOB* targets; 
  targets = (BLOB*)calloc(tracking_number,sizeof(BLOB));

  //create a list of targets if nto all blobs were requested
  if(tracking_number>0){
    //allocate memory for target pixels
    for(i=0;i<tracking_number;i++){
      targets[i].pixels = (CvPoint*)cvAlloc(MAX_BLOB_AREA*sizeof(CvPoint));
    }
  }
 
  unsigned int** pixlog;
  //Allocate array of pointers to keep track of pixels that have been checked
  pixlog = (unsigned int**)calloc(width,sizeof(unsigned int*));

  //Allocate the width dimention of the array of pointers to keep track of pixels that have been checked
  for(i=0; i<width; i++)
    pixlog[i] = (unsigned int*)calloc(height,sizeof(unsigned int));
 
  //now sweep the image looking for blobs (check a grid, not every pixel)
  for(y=0; y<height-3; y+=4 ) {
    uchar* ptr = (uchar*) (Img->imageData + y * Img->widthStep);
    for(x=0; x<width-3; x+=4 ) {
       //if the pixel hasn't been blacked out as the wrong color AND has not yet been checked
       if((ptr[3*x+0]||ptr[3*x+1]||ptr[3*x+2])&& pixlog[x][y]==0){
      //we've found a new blob, so let's initialize it's values
          blobs[*blobnumber].area = 0; 
          blobs[*blobnumber].top = 0;
          blobs[*blobnumber].bottom = height;
          blobs[*blobnumber].right = 0;
          blobs[*blobnumber].left = width;
      blobs[*blobnumber].cent_x = 0;
      blobs[*blobnumber].cent_y = 0;
      blobs[*blobnumber].mid.x = 0;
      blobs[*blobnumber].mid.y = 0;
      blobs[*blobnumber].pixels = (CvPoint*)cvAlloc(sizeof(CvPoint)*MAX_BLOB_AREA);
          //now let's examine the blob and update it's properties
      int depth = 0;
          checkPixel(Img, x,y, pixlog, &blobs[*blobnumber], depth);

      //don't bother sorting if we are asked to return ALL the blobs (argument: tracking_number of zero)
      if(tracking_number > 0){
            //now we check to see if this makes our list of top <tracking_number> biggest blobs
            if(blobs[*blobnumber].area > targets[tracking_number-1].area && blobs[*blobnumber].area >= minimum_blob_area){
          blobs_found++;

              for(i = tracking_number-1; i >=0; i--){
            if(blobs[*blobnumber].area <= targets[i].area){
          blob_copy(&targets[i+1],&blobs[*blobnumber]);
                  i=-1;//we've found where it goes, don't come back into the loop
            }
            else if(i+1 < tracking_number){
                  blob_copy(&targets[i+1],&targets[i]);

                  //and for the case where this is the biggest blob
                  if(i==0){
            blob_copy(&targets[i],&blobs[*blobnumber]);
              }
                }
          }
        }
      }
      //increment blobnumber
          *blobnumber += 1;
       }
    }
  }

  //free the pixle log
  for(i=0; i<width; i++)
    free(pixlog[i]); 
  free(pixlog);

  //if we want all the blobs, just return blobs and be done
  if(tracking_number == 0){
    return blobs;
  }

  //free the target pixels we don't need
  for(i=blobs_found;i<tracking_number;i++){
    cvFree(&targets[i].pixels);
  }

  //mark the size of targets
  *blobnumber = tracking_number < blobs_found? tracking_number:blobs_found;

  return targets;
}

//--------------------------------------------------------------------------------------------
// Function: checkPixel
// Examines an object pixel-by-pixel, checking each pixel to see if it 
//   is the top bottom left or rightmost of the object. Then recursively calls
//   itself on all surrounding pixels that are part of the object. assignes total 
//   number of pixels in the object to the object's area 

int checkPixel(IplImage* Img, int x, int y, unsigned int** pixlog, BLOB* blob, int depth){

  //we will crash if we keep going, so let's just stop now
  if(++depth > 100000)
    return 1;

  if(pixlog[x][y] != 0)
    return 1; //we've checked this pixel, so shouldn't do it again
  //now mark this pixel as having been checked 
  pixlog[x][y] = 1;

  //check too see if the pixel is part of the blob
  uchar* ptr = (uchar*) (Img->imageData + y * Img->widthStep);
  if((ptr[3*x+0] || ptr[3*x+1] || ptr[3*x+2]) == 0){
    return 2; //the pixel has been blacked out and shouldn't be used 
  }

  int height = Img->height;
  int width = Img->width;

  //now that we know this is a new pixel belonging to the current blob, proccess it
  blob->area++; //increase area
  blob->cent_x = (blob->cent_x*(blob->area-1)+x)/(blob->area);
  blob->cent_y = (blob->cent_y*(blob->area-1)+y)/(blob->area);

  //quick saftey check. If the blob is ever this big the program no longer works, but at least this line keeps it from crashing
  if(blob->area >= MAX_BLOB_AREA)
  return 3; 

  //now catagorize this pixel
  if(y > blob->top) blob->top = y; 
  if(y < blob->bottom) blob->bottom = y; 
  if(x < blob->left) blob->left = x;       
  if(x > blob->right) blob->right = x;

  //add this pixel to the list for this blob
  blob->pixels[(blob->area-1)].x = x;
  blob->pixels[(blob->area-1)].y = y;
  //printf("pixels[%ld] being assigned {%d,%d}\n",blob->area-1,blob->pixels[(blob->area-1)].x,blob->pixels[(blob->area-1)].y);
  //fflush(NULL);

  int w,z; 
  //now check all surrounding pixels
  for(w = x-1; w <= x+1; w++){
    for(z = y-1; z <= y+1; z++){
      //save from overfow
      if((w > 1)&&(z > 1)&&(w < width -1)&&(z < height-1)) 
        //now that we know we are on the image, check this pixel
        checkPixel(Img,w,z,pixlog, blob, depth);
    }
  }
  return 0;
}  

void blob_copy(BLOB* dest, BLOB* src){
    //copy src to destination
    dest->top   = src->top;
    dest->left  = src->left;
    dest->right = src->right;
    dest->bottom= src->bottom; 
    dest->area  = src->area;
    dest->cent_x= src->cent_x;
    dest->cent_y= src->cent_y;
    dest->mid   = src->mid;
    memcpy(dest->pixels,src->pixels,MAX_BLOB_AREA*sizeof(CvPoint));
}