from __future__ import division
import math
import cv2
import numpy as np
import svr
from base import VisionEntity
import libvision

class Buoy(object):
    buoy_id = 0
    """ an imaged buoy b """

    def __init__(self, xcoor, ycoor, radius):
        self.type = "buoy"
        self.centerx = xcoor
        self.centery = ycoor
        self.radius = radius
        self.color = ""
        self.area = 0
        self.id = 0        # id identifies which buoy your looking at
        self.lastseen = 2  # how recently you have seen this buoy
        # how many times you have seen this buoy (if you see it enough it
        # becomes confirmed)
        self.seencount = 1


class BuoyTestingEntity(VisionEntity):

    def init(self):
        self.adaptive_thresh_blocksize = 15
        self.adaptive_thresh = 13
        self.min_area = 500
        self.max_area = 5000
        self.recent_id = 1
        self.trans_thresh = 25

        self.LEDcandidates = []
        self.LEDconfirmed = []

        self.candidates = []
        self.confirmed = []

        self.lastseen_thresh = 0
        self.seencount_thresh = 2

    def process_frame(self, frame):
        # This is equivalent to the old routine, but it isn't actually necessary
        #height, width, depth = libvision.cv_to_cv2(frame).shape
        #self.debug_frame = np.zeros((height, width, 3), np.uint8)

        # Debug numpy is CV2
        self.debug_frame = libvision.cv_to_cv2(frame)

        # CV2 Transforms
        self.numpy_frame = self.debug_frame.copy()
        self.numpy_frame = cv2.medianBlur(self.numpy_frame, 5)
        self.numpy_frame = cv2.cvtColor(self.numpy_frame, cv2.COLOR_BGR2HSV)

        (self.frame1, self.frame2, self.frame3) = cv2.split(self.numpy_frame)
        # Change the frame number to determine what channel to focus on
        self.numpy_frame = self.frame2

        # Thresholding
        self.numpy_frame = cv2.adaptiveThreshold(self.numpy_frame,
                              255,
                              cv2.ADAPTIVE_THRESH_MEAN_C,
                              cv2.THRESH_BINARY_INV,
                              self.adaptive_thresh_blocksize,
                              self.adaptive_thresh
                              )

        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        kernel2 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (4, 4))
    #     #kernel = np.ones((2,2), np.uint8)
        self.numpy_frame = cv2.erode(self.numpy_frame, kernel)
        self.numpy_frame = cv2.dilate(self.numpy_frame, kernel2)

        self.adaptive_frame = self.numpy_frame.copy()

    #     # Find contours
        contours, hierarchy = cv2.findContours(self.numpy_frame,
                                               cv2.RETR_TREE, 
                                               cv2.CHAIN_APPROX_SIMPLE)
        self.raw_led = []
        self.raw_buoys = []

        if len(contours) > 1:
            cnt = contours[0]
            cv2.drawContours(self.numpy_frame, contours, -1, (255, 255, 255), 3)

            for h, cnt in enumerate(contours):
                approx = cv2.approxPolyDP(cnt, 0.01 * cv2.arcLength(cnt, True), True)

                center, radius = cv2.minEnclosingCircle(cnt)
                x, y = center

                if len(approx) > 10:
                    if (radius > 25):
                        new_buoy = Buoy(int(x), int(y), int(radius))
                        new_buoy.id = self.recent_id
                        self.recent_id += 1
                        self.raw_buoys.append(new_buoy)
                        cv2.drawContours(self.numpy_frame, [cnt], 0, (0, 0, 255), -1)

        for buoy in self.raw_buoys:
            self.match_buoys(buoy)

        self.sort_buoys()
        self.draw_buoys()

        self.return_output()

        self.debug_to_cv = libvision.cv2_to_cv(self.debug_frame)
        self.numpy_to_cv = libvision.cv2_to_cv(self.numpy_frame)
        self.adaptive_to_cv = libvision.cv2_to_cv(self.adaptive_frame)

        svr.debug("processed", self.numpy_to_cv)
        svr.debug("adaptive", self.adaptive_to_cv)
        svr.debug("debug", self.debug_to_cv)

    #     # TODO, CLEAN THIS UP SOME
    def match_buoys(self, target):
        found = 0
        for buoy in self.candidates:
            if math.fabs(buoy.centerx - target.centerx) < self.trans_thresh and \
               math.fabs(buoy.centery - target.centery) < self.trans_thresh:
                print buoy.seencount
                buoy.centerx = target.centerx
                buoy.centery = target.centery
                print "still ", buoy.seencount
                buoy.seencount += 2
                print "new seencount ", buoy.seencount
                buoy.lastseen += 6
                found = 1
        for buoy in self.confirmed:
            if math.fabs(buoy.centerx - target.centerx) < self.trans_thresh and \
               math.fabs(buoy.centery - target.centery) < self.trans_thresh:
                target.id = buoy.id
                buoy = target
                buoy.lastseen += 6
                found = 1
        if found == 0:
            self.candidates.append(target)
            target.lastseen + 3

        # TODO, CLEAN THIS UP SOME
    def sort_buoys(self):
        for buoy in self.candidates[:]:
            print "last seen is ", buoy.lastseen
            print "seencount is ", buoy.seencount
            buoy.lastseen -= 1
            if buoy.seencount >= self.seencount_thresh:
                self.confirmed.append(buoy)
                print "confirmed appended"
            if buoy.lastseen < self.lastseen_thresh:
                self.candidates.remove(buoy)
        for buoy in self.confirmed[:]:
            buoy.lastseen -= 1
            if buoy.lastseen < self.lastseen_thresh:
                self.confirmed.remove(buoy)
                print "confirmed removed"

    def draw_buoys(self):
        clr = (255, 0, 255)
        for buoy in self.raw_buoys:
            cv2.circle(self.debug_frame, (buoy.centerx, buoy.centery), buoy.radius, clr, -1)
            # font = cv2.FONT_HERSHEY_SIMPLEX
            # cv2.putText(self.debug_final_frame, "theta=" + str(buoy.theta), (int(buoy.midx) - 50, int(buoy.midy) + 20), font, .4, clr, 1, cv2.CV_AA)
