// A bracket to mount bushings to the DFRobot FIT0708 stepper motor.

cv = 5;
$fn = 60;

weightCenters = 21;
weightStems = 8.03;

// The bracket itself, for design or printing
if(1) bracket();

// Reference parts
// The threaded tongue/bolt from the motor, that the bracket fits over
if(0) translate([0,0,0.9]) color("yellow") tongue();
// The bushings
if(0) translate([0,0,3]) {
  for(i=[-1,1]) translate([i*weightCenters,0,j*7.1]) {
    color("teal") bushing();
  }
}


module bracket() {
  difference() {
    
    linear_extrude(3,convexity=cv) {
      difference() {
        hull() {
          for(i=[-1,1]) translate([i*(weightCenters),0]) circle(d=weightStems);
        }
        translate([0,2]) hull(){
          for(i=[-1,1]) translate([i*8,3]) circle(d=6,$fn=60);
        }
        translate([-1.7,-4.5/2]) square([5.5,10]);
      }
    }

    translate([0,0,0.9]) tongue();
  }
  for(i=[-1,1]) translate([i*weightCenters,0,-7]) cylinder(d=weightStems,h=7);
}

module tongue(removeHoles=false) {
   {
    //7.15, centerline 2.85, depth 4.5, height 1.1
    linear_extrude(1.1,convexity=cv) {
      difference() {
        translate([-2.85,-4.5/2]) square([7.25,4.5]);
        if(!removeHoles) {
          circle(d=2,$fn=24);
          translate([2.5,0]) circle(d=1.6,$fn=24);
        }
      }
    }
  }
}

module bushing() {
  {
    linear_extrude(7,convexity=cv) {
      difference() {
        circle(d=22);
        circle(d=8);
      }
    }
  }
}