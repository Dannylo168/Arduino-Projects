/*
 *   NovaSM3 - a Spot-Mini Micro clone
 *   Version: 4.0
 *   Version Date: 2021-04-21
 *   
 *   Author:  Chris Locke - cguweb@gmail.com
 *   GitHub Project:  https://github.com/cguweb-com/Arduino-Projects/tree/main/Nova-SM3
 *   Thingiverse:  https://www.thingiverse.com/thing:4767006
 *   Instructables Project:  https://www.instructables.com/Nova-Spot-Micro-a-Spot-Mini-Clone/
 *   YouTube Playlist:  https://www.youtube.com/watch?v=00PkTcGWPvo&list=PLcOZNHwM_I2a3YZKf8FtUjJneKGXCfduk
 *   
 *   RELEASE NOTES:
 *      Arduino mega performance: 36% storage / 31% memory
 *      splt AsyncServo class into an include file
 *      created serial connection to arduino nano / slave
 *      created functions for serial communication to nano / slave
 *      moved uss, rgb, and oled functions to nano / slave companion file: Nova-SM3_nano-vX.x
 *      Bug Fix: state machine intervals were not being set / reset for USS and MPU sensors
 *      added stop on uss alarm and halt on pir alarm
 *
 *   DEV NOTES:
 *      BUG: ramping: on interruption of ramp, servo speed is set to the speed of the point of interrupt
 *      re-calibrate servo home positions to balance Nova's COG!! seems to be back-heavy
 *      write a z-axis control that is sticky, so it maintains the set height on subsequent moves
 *      write a stable fixed speed / step walking routine
 *      fix 'stay' routine (ie: tends to fall backward when coming off of kneel or sit positions into stay)
 *      work on integrating MPU data into movements
 *      x_axis: tweak pattern, adjusting use of move_steps to not near fall over backwards on startup
 *      finish tweaking left and right stepping
 *      finish forward step (w/ left, right, backwards!)
 *      
 *      see more 'DEV NOTE' comments in code for more bugs/tasks
 *      
*/

//set Nova SM3 version
#define VERSION 4.0

//debug vars for displaying operation runtime data for debugging
const byte debug = 1;             //general messages
const byte debug1 = 0;            //ps2 commands
const byte debug2 = 0;            //debug servo steps
const byte debug3 = 0;            //ramping and sequencing
const byte debug4 = 0;            //amperage, battery and serial terminal commands
const byte debug5 = 0;            //mpu and uss sensors
const byte debug6 = 0;            //serial communication output/response
const byte plotter = 0;           //plot servo steps, turn off debug1

byte debug_leg = 0;               //default debug leg (3 servos) (changed by serial command input)
int debug_servo = 0;              //default debug servo (changed by serial command input)
const int debug_loops = 3;        //default loops for debug movements
int debug_loops2 = 3;             //movement decremented loop
int debug_spd = 10;               //default speed for debug movements

//activate/deactivate devices
byte slave_active = 1;            //activate slave arduino nano
byte pwm_active = 1;              //activate pwm controller / servos
byte ps2_active = 0;              //activate PS2 remote control 
byte serial_active = 1;           //activate serial monitor command input
byte mpu_active = 0;              //activate MPU6050 
byte rgb_active = 1;              //activate RGB modules
byte oled_active = 1;             //activate OLED display
byte pir_active = 0;              //activate PIR motion sensor
byte uss_active = 0;              //activate Ultra-Sonic sensors
byte amp_active = 0;              //activate amperate monitoring
byte batt_active = 1;             //activate battery level monitoring
byte buzz_active = 1;             //activate simple tone sounds
byte melody_active = 0;           //activate melodic tone sounds

//include supporting libraries
#include <SPI.h>
#include <Wire.h>
#include <PS2X_lib.h>
#include <Adafruit_PWMServoDriver.h>

//pwm controller
Adafruit_PWMServoDriver pwm1 = Adafruit_PWMServoDriver(0x40);
#define SERVO_FREQ 60             //CAUTION: do not change this once calibrated to servos!
#define OSCIL_FREQ 25000000       //CAUTION: do not change this once calibrated to servos!
#define OE_PIN 3                  //PWM Output Enable pin
byte pwm_oe = 0;                  //boolean control for enable / disable output
const float min_spd = 96.0;       //min is higher than max, since this is the time increment delay between servo calls 
const float max_spd = 1.0;        //maximum, fastest speed
float default_spd = 13.0;
float spd = default_spd;
float spd_factor = 1.0;           //ratio factor used in movements
const float min_spd_factor = 5.0;
const float max_spd_factor = 0.5;
float spd_c;
float spd_f;
float spd_t;
byte start_stop = 0;              //deprecated: boolean to check if robot is doing anything
byte step_start = 0;              //boolean to check if sequenced steps are running
int x_dir = 0;
int y_dir = 0;
int z_dir = 0;
byte use_ramp = 0;                //boolean to enable / disable ramping function
float ramp_dist = 0.20;           //ramp percentage distance from each end of travel (ex: 0.20 ramps up from 0-20% of travel, then ramps down 80-100% of travel)
float ramp_spd = 5.00;            //default ramp speed multiplier

//buzzer
#define BUZZ 11                   //piezo buzzer pin

//rgb leds
//int fade_steps = 400;
//int pattern = 0;                  //set which light pattern is used
int pattern_int = 500;            //set pattern delay between
int pattern_cnt = 0;              //set number of loops of pattern
//int cur_rgb_val1[3] = {25, 0, 125};  //set color of left lights
//int cur_rgb_val2[3] = {55, 0, 200};  //set color of right lights

//pir sensor
#define PIR_SENSOR 30
unsigned int pirInterval = 300;
unsigned long lastPIRUpdate = 0;
byte pir_state = LOW;
byte pir_val = 0;
byte pir_reset = 0;
byte pir_halt = 0;
int pir_wait = 0;

//ultrasonic sensors (2 - left & right)
unsigned int ussInterval = 500;
unsigned long lastUSSUpdate = 0;
int distance_alarm = 40;              //distance to set triggers
int distance_alarm_set = 0;           //count consecutive set triggers
int distance_tolerance = 5;           //threshold before setting triggers
int distance_l;                       //current distance from left sensor
int prev_distance_l;                  //previous distance of left sensor to prevent false positives
int distance_r;                       //current distance from right sensor
int prev_distance_r;                  //previous distance of right sensor to prevent false positives

//mpu6050 sensor
const int MPU = 0x68;
unsigned int mpuInterval = 40;
unsigned long lastMPUUpdate = 0;
float AccX, AccY, AccZ;
float GyroX, GyroY, GyroZ;
float accAngleX, accAngleY, gyroAngleX, gyroAngleY, gyroAngleZ;
float mroll, mpitch, myaw;
float mroll_prev, mpitch_prev, myaw_prev;
float mpu_trigger_thresh = 0.1;
float AccErrorX, AccErrorY, GyroErrorX, GyroErrorY, GyroErrorZ;
float elapsedTime, currentTime, previousTime;
int mpu_c = 0;

//DEV NOTE: these are used for an experimental oscillation prevention check, but really, a PID controller
//          should researched and coded to replace this messy, unreliable solution
//
float mpu_oscill_thresh = 3.0;
int mpu_oscill_grace = 3;
int mpu_oscill_limit = 3;
int mpu_oscill_cnt = mpu_oscill_limit;

//PS2 controller
#define PS2_DAT 24
#define PS2_CMD 25
#define PS2_SEL 26
#define PS2_CLK 27
PS2X ps2x;
unsigned int ps2Interval = 50;
unsigned long lastPS2Update = 0;
int ps2_select = 1;               //sets default button set


//slave arduino and serial commands
#define SLAVE_ID 1
byte serial_oled = 0;            //switch for serial or oled commands
int serial_resp;
int ByteReceived;
unsigned int serialInterval = 60;
unsigned long lastSerialUpdate = 0;

//amperage monitor
#define AMP_PIN A2
#define PWR_PIN 4
unsigned int ampInterval = 15000;
unsigned long lastAmpUpdate = 0;
int amp_cnt = 0;
int amp_thresh = 10;            //loop count for consecutive amperage alarms to prevent false positives
int amp_warning = 0;            //current alarm warning level
int amp_loop = 1;               //if warning level set, this changes accordingly to prevent false positives
float amp_limit = 6.5;          //aperage draw limit before triggering alarms


//battery monitor
#define BATT_MONITOR A1
unsigned long batteryInterval = 6000;
unsigned long lastBatteryUpdate = 0;
int batt_cnt = 0;
int batt_skip = 0;
float batt_voltage = 11.4;                          //fully charged battery minimum nominal voltage
float batt_voltage_prev = 11.4;                     //comparison voltage to prevent false positives
float batt_levels[4] = {11.1, 10.9, 10.7, 10.5};    //voltage drop alarm levels(4)


//movement vars for steps, delays, loops, sequencing, etc
unsigned long lastMoveDelayUpdate = millis();
unsigned int moveDelayInterval = 0;
int move_delays[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int move_delay_sequences[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int move_delay = 0;
int move_loops = 0;
int move_switch = 0;
float move_steps_min = -50;
float move_steps_max = 50;
float move_steps = 0;
float move_steps_prev = 0;
float move_steps_x = 0;
float move_steps_y = 0;
float move_steps_yaw_x = 0;
float move_steps_yaw_y = 0;
float move_steps_yaw = 0;
float move_steps_kx = 0;
float move_steps_ky = 0;
float move_steps_alt = 0;
int move_c_steps[2] = {-50, 50};
int move_f_steps[2] = {-200, 200};
int move_t_steps[2] = {-200, 200};
int move_x_steps[2] = {-30, 30};
int move_y_steps[2] = {-150, 150};

//booleans to control / monitor movement routine execution
byte move_y_axis = 0;
byte move_x_axis = 0;
byte move_roll = 0;
byte move_roll_body = 0;
byte move_pitch = 0;
byte move_pitch_body = 0;
byte move_trot = 0;
byte move_forward = 0;
byte move_backward = 0;
byte move_left = 0;
byte move_right = 0;
byte move_march = 0;
byte move_wake = 0;
byte move_sequence = 0;
byte move_demo = 0;
byte move_wman = 0;
byte move_funplay = 0;
byte move_look_left = 0;
byte move_look_right = 0;
byte move_roll_x = 0;
byte move_pitch_y = 0;
byte move_kin_x = 0;
byte move_kin_y = 0;
byte move_yaw_x = 0;
byte move_yaw_y = 0;
byte move_yaw = 0;
byte move_servo = 0;
byte move_leg = 0;

//deprecated: vars used in attempt to compensate for uncalibrated front-to-back center of gravity
float step_weight_factor = 0;           
float step_height_factor = 1.25;


/*
   -------------------------------------------------------
   Function Prototypes
    :required for functions executed from servo class ( I know, I know, poor OOP design calling functions outside of a class ;p )
   -------------------------------------------------------
*/
void set_ramp(int servo, float sp, float r1_spd, float r1_dist, float r2_spd, float r2_dist);
void amperage_check(int aloop);

#include "NovaServos.h"           //include motor setup vars and data arrays
#include "AsyncServo.h"           //include motor class

//instantiate servo objects (s_XXX) with driver reference and servo ID
//coax servo objects
AsyncServo s_RFC(&pwm1, RFC);
AsyncServo s_LFC(&pwm1, LFC);
AsyncServo s_RRC(&pwm1, RRC);
AsyncServo s_LRC(&pwm1, LRC);

//femur servo objects
AsyncServo s_RFF(&pwm1, RFF);
AsyncServo s_LFF(&pwm1, LFF);
AsyncServo s_RRF(&pwm1, RRF);
AsyncServo s_LRF(&pwm1, LRF);

//tibia servo objects
AsyncServo s_RFT(&pwm1, RFT);
AsyncServo s_LFT(&pwm1, LFT);
AsyncServo s_RRT(&pwm1, RRT);
AsyncServo s_LRT(&pwm1, LRT);


void setup() {
  Serial.begin(19200);

  //allow serial, ps2, and PWM hardware to powerup & connect
  while (!Serial) {
    delay(1);
  }
  delay(500);

  if (debug) {
    Serial.println(F("\n==================================="));
    Serial.print(F("NOVA SM3 v"));
    Serial.println(VERSION);
    Serial.println(F("==================================="));
  }

  //seed arduino pin A0, otherwise random() functions will not be truely random
  randomSeed(analogRead(0));

  if (melody_active) {
    //pay homage to the first robot in my life at age 7, R2D2!! 
    for (int i=0; i<2; i++) {
      play_phrases();
      delay(random(200,800));
    }
  } else if (buzz_active) {
    //pay homage to the first video came system in my life at age 7, Atari2600!! 
    for (int b = 0; b < 8; b++) {
      tone(BUZZ, (b * 1.5)*200);
      delay(20);
      noTone(BUZZ);
      delay(20);
    }
    noTone(BUZZ);
  }

  //(re)boot slave nano
  if (slave_active) {
    command_slave("Z");
  }

  //init mpu6050
  if (mpu_active) {
    Wire.begin();
    Wire.beginTransmission(MPU);
    Wire.write(0x6B);
    Wire.write(0x00);
    Wire.endTransmission(true);
  }

  //init pir sensor
  if (pir_active) {
    pinMode(PIR_SENSOR, INPUT);
  }

  //init amp power control
  if (amp_active) {
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, LOW);
  }

  //init ps2 controller
  if (ps2_active) {
    if (ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_SEL, PS2_DAT, false, false) == 0) {
      if (debug) Serial.println(F("Controller OK"));
    } else {
      if (debug) Serial.println(F("Controller Error"));
      ps2_active = 0;
    }
  }

  //init pwm controller
  if (pwm_active) {
    pwm1.begin();
    pwm1.setOscillatorFrequency(OSCIL_FREQ);
    pwm1.setPWMFreq(SERVO_FREQ);
    delay(50);
    if (debug) Serial.println(F("pwm1 Passed Setup"));

    if (melody_active) {
      play_phrases();
    } else if (buzz_active) {
      for (int b = 3; b > 0; b--) {
        tone(BUZZ, 500);
        delay(70);
        noTone(BUZZ);
        delay(70);
      }
      noTone(BUZZ);
    } else {
      delay(1000);
    }

    //set default speed factor
    spd_factor = mapfloat(spd, min_spd, max_spd, min_spd_factor, max_spd_factor);

    //initialize servos and populate related data arrays with defaults
    init_home();
    if (debug) {
      Serial.print(TOTAL_SERVOS); Serial.println(F(" Servos Initialized"));
    }

    //DEV NOTE:  this was done because otherwise, there is some electrical(?) interference to
    //           the PS2 controller / wiring that was causing the remote to fire all buttons at
    //           arduino boot time, obviously causing Nova to go haywire with unexpected random 
    //           movement commands!
    //
    //           After testing, it appeared to be the PWM controller interfering, so I added code
    //           to halt its output until the PS2 was fully initialized, which is found at the end 
    //           of the ps2_check() function.
    //
    //disable PWM output after initializing servos
    if (ps2_active) {
      digitalWrite(OE_PIN, HIGH);
      delay(500);
    }
  }

  if (!pwm_active) {
    if (debug) Serial.print(F("PWM disabled, initializing..."));
    delay(1000);
    if (debug) Serial.println(F("Ready!"));
  }

  //calc mpu6050 error margins
  if (mpu_active) {
    calculate_IMU_error();
    delay(20);
  }

  if (debug) Serial.println(F("Ready!"));
  if (rgb_active) {
    rgb_request("Hf");
  }
  delay(1000);
  if (rgb_active) {
    rgb_request("Jf");
  }
  if (!plotter && serial_active) {
    delay(1000);
    Serial.println();
    Serial.println(F("Type a command key or 'h' for help:"));
  }
}

void loop() {

/*
   -------------------------------------------------------
   Update Servos
    :check if servo(s) need updating
    :this is the core functionality of Nova
   -------------------------------------------------------
*/
  //update coxas
  s_RFC.Update();
  s_LFC.Update();
  s_RRC.Update();
  s_LRC.Update();

  //update femurs
  s_RFF.Update();
  s_LFF.Update();
  s_RRF.Update();
  s_LRF.Update();

  //update tibias
  s_RFT.Update();
  s_LFT.Update();
  s_RRT.Update();
  s_LRT.Update();


/*
   -------------------------------------------------------
   Check for Moves
    :check if any scripted, sequenced, or dynamic moves are active
    :and execute accordingly along with any required variables defined
   -------------------------------------------------------
*/
  if (move_sequence) {
    run_sequence();
  } else if (move_x_axis) {
    x_axis();
  } else if (move_y_axis) {
    y_axis();
  } else if (move_pitch_body) {
    pitch_body();
  } else if (move_pitch) {
    pitch(x_dir);
  } else if (move_roll_body) {
    roll_body();
  } else if (move_roll) {
    roll();
  } else if (move_trot) {
    step_trot(x_dir);
  } else if (move_forward) {
    step_forward(x_dir);
  } else if (move_backward) {
    step_backward(x_dir);
  } else if (move_left) {
    ramp_dist = 0.25;
    ramp_spd = 1.5;
    use_ramp = 0;
    step_left_right(1, x_dir, y_dir);
  } else if (move_right) {
    ramp_dist = 0.25;
    ramp_spd = 1.5;
    use_ramp = 0;
    step_left_right(0, x_dir, y_dir);
  } else if (move_march) {
    step_march(x_dir, y_dir);
  } else if (move_wake) {
    wake();
  } else if (move_wman) {
    ramp_dist = 0.2;
    ramp_spd = 0.5;
    use_ramp = 1;
    wman();
  } else if (move_funplay) {
    funplay();
  } else if (move_look_left) {
    look_left();
  } else if (move_look_right) {
    look_right();
  } else if (move_roll_x) {
    roll_x();
  } else if (move_pitch_y) {
    pitch_y();
  } else if (move_kin_x) {
    move_kx();
  } else if (move_kin_y) {
    move_ky();
  } else if (move_yaw_x) {
    yaw_x();
  } else if (move_yaw_y) {
    yaw_y();
  } else if (move_yaw) {
    yaw();
  } else if (move_servo) {
    move_debug_servo();
  } else if (move_leg) {
    move_debug_leg();
  }

/*
   -------------------------------------------------------
   Check State Machines
    :check active state machine(s) for execution time by its respective interval
   -------------------------------------------------------
*/
  if (ps2_active) {
    if (millis() - lastPS2Update > ps2Interval) ps2_check();
  }

  if (serial_active) {
    if (millis() - lastSerialUpdate > serialInterval) serial_check();
//    if (millis() - lastSerialUpdate > serialInterval) slave_test();
  }

  if (pir_active && !pir_halt) {
    if (millis() - lastPIRUpdate > pirInterval) pir_check();
  }

  if (mpu_active) {
    if(millis() - lastMPUUpdate > mpuInterval) get_mpu();
  }

  if (uss_active) {
    if (millis() - lastUSSUpdate > ussInterval) uss_check();
  }

  if (amp_active) {
    if (ampInterval && millis() - lastAmpUpdate > ampInterval) amperage_check(amp_loop);
  }

  if (batt_active) {
    if(millis() - lastBatteryUpdate > batteryInterval) battery_check();
  }

  if (moveDelayInterval && millis() - lastMoveDelayUpdate > moveDelayInterval) {
    delay_sequences();
  }
}



/*
   -------------------------------------------------------
   Hardware Functions
   -------------------------------------------------------
*/
/*
   -------------------------------------------------------
   PS2 Check
    :provide general description and explanation here - too much to comment by line
   -------------------------------------------------------
*/
void ps2_check() {
  if (pwm_oe) {
    ps2x.read_gamepad(false, false);

    if (!move_demo && !move_funplay) {
      if (ps2x.Button(PSB_START)) {
        if (debug1)
          Serial.println(F("Start Pressed"));
        if (ps2_select == 1) {
          rgb_request("MVNV");

          if (mpu_active) {
            mpu_active = 0;
            rgb_request("vGn");
          } else {
            mpu_active = 1;
            rgb_request("xFn");
          }
        } else if (ps2_select == 3) {
          rgb_request("MVNV");

          if (uss_active) {
            uss_active = 0;
            rgb_request("vGn");
          } else {
            uss_active = 1;
            rgb_request("xFn");
          }
        } else if (ps2_select == 4) {
          run_demo();
        } else {
          pir_halt = 1;
        }
      } else if (ps2x.ButtonReleased(PSB_START)) {
        if (debug1)
          Serial.println(F("Start Released"));
        pir_halt = 0;
      }
  
      if (ps2x.ButtonReleased(PSB_SELECT)) {
        (ps2_select < 4) ? ps2_select++ : ps2_select = 1;
        if (rgb_active) {
          rgb_request("MQNQ");

          if (ps2_select == 1) {
            rgb_request("tEn");
          } else if (ps2_select == 2) {
            rgb_request("uEn");
          } else if (ps2_select == 3) {
            rgb_request("vEn");
          } else if (ps2_select == 4) {
            rgb_request("wEn");
          }
        }
        if (buzz_active) {
          for (int b = ps2_select; b > 0; b--) {
            tone(BUZZ, 2000);          
            delay(70);  
            noTone(BUZZ);         
            delay(70);  
          }
          noTone(BUZZ);         
        }
        if (oled_active) {
          if (ps2_select == 1) {
            oled_request("g");
          } else if (ps2_select == 2) {
            oled_request("h");
          } else if (ps2_select == 3) {
            oled_request("i");
          } else if (ps2_select == 4) {
            oled_request("j");
          }
        }
        if (debug1) {
          Serial.print(F("\tSelected ")); Serial.println(ps2_select);
        }
      }

/*
if (ps2_select == 1) {
int steps_x = map(ps2x.Analog(PSS_LX), 0, 255, move_c_steps[1], move_c_steps[0]);
Serial.print(ps2x.Analog(PSS_LX));Serial.print("\t\t");Serial.print(ps2x.Analog(PSS_RX));Serial.print("\t\t");
Serial.print(ps2x.Analog(PSS_LY));Serial.print("\t\t");Serial.println(ps2x.Analog(PSS_RY));
}
*/

      //kinematics joysticks
      if (ps2_select == 3) {
        if (ps2x.Button(PSB_R3)) {
        } else {
          move_steps_y = map(ps2x.Analog(PSS_LY), 0, 255, (move_steps_max * 1.3), (move_steps_min * 1.3));
          start_stop = 1;
          move_pitch_y = 1;
          if (debug1 && move_steps_y) {
            Serial.print(F("move pitch_y "));Serial.println(move_steps_y);
          }

          move_steps_x = map(ps2x.Analog(PSS_LX), 0, 255, (move_steps_max * 1.3), (move_steps_min * 1.3));
          start_stop = 1;
          move_roll_x = 1;
          if (debug1 && move_steps_x) {
            Serial.print(F("move roll_x "));Serial.println(move_steps_x);
          }
        }

        if (ps2x.Button(PSB_L3)) {
          //move yaw while button pressed/held
          move_steps_yaw = map(ps2x.Analog(PSS_RX), 0, 255, (move_steps_max * 1.3), (move_steps_min * 1.3));
          if (move_steps_yaw > 1 || move_steps_yaw < -1) {
            start_stop = 1;
            move_yaw = 1;
            if (debug1)
              Serial.println(F("move yaw"));
          } else {
            move_yaw = 0;
          }

          //move in y while button pressed/held
          move_steps_ky = map(ps2x.Analog(PSS_RY), 0, 255, (move_steps_min * 1.3), (move_steps_max * 1.3));
          if (move_steps_ky > 1 || move_steps_ky < -1) {
            start_stop = 1;
            move_kin_y = 1;
            if (debug1)
              Serial.println(F("move kin_y"));
          } else {
            move_kin_y = 0;
          }
        } else {
          move_steps_yaw_x = map(ps2x.Analog(PSS_RX), 0, 255, (move_steps_max * .5), (move_steps_min * .5));
          if (move_steps_yaw_x > 1 || move_steps_yaw_x < -1) {
            start_stop = 1;
            move_yaw_x = 1;
            if (debug1)
              Serial.println(F("move yaw_x"));
          } else {
            move_yaw_x = 0;
          }

          move_steps_yaw_y = map(ps2x.Analog(PSS_RY), 0, 255, (move_steps_max * .8), (move_steps_min * .8));
          if (move_steps_yaw_y > 1 || move_steps_yaw_y < -1) {
            start_stop = 1;
            move_yaw_y = 1;
            if (debug1)
              Serial.println(F("move yaw_y"));
          } else {
            move_yaw_y = 0;
          }
        }  
      }

      if (ps2x.Button(PSB_PAD_UP)) {
        if (ps2_select == 1) {
          if (!move_trot) {
            set_stop();
            move_trot = 1;
            if (debug1)
              Serial.println(F("move trot"));
          }
          x_dir = map(ps2x.Analog(PSS_RX), 0, 255, move_steps_min / 4, move_steps_max / 4);
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max / 2, move_steps_min / 2);
          if (debug2) {
            Serial.print(F("x dir: ")); Serial.print(x_dir);
            Serial.print(F("\tmove steps: ")); Serial.println(move_steps);
          }
        } else if (ps2_select == 2) {
          if (debug1)
            Serial.println(F("march"));
          if (!move_march) {
            set_stop();
            start_stop = 1;
            move_march = 1;
            if (oled_active) {
              oled_request("d");
            }
          }
          x_dir = map(ps2x.Analog(PSS_RX), 0, 255, move_steps_min / 2, move_steps_max / 2);
          y_dir = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max, move_steps_min);
          if (debug2) {
            Serial.print(F("x: ")); Serial.print(ps2x.Analog(PSS_RX));
            Serial.print(F("\ty: ")); Serial.print(ps2x.Analog(PSS_RY));
            Serial.print(F("\tx_dir: ")); Serial.print(x_dir);
            Serial.print(F("\ty_dir: ")); Serial.println(y_dir);
          }
        } else if (ps2_select == 3) {
          if (debug1)
            Serial.println(F("forward"));
          if (!move_forward) {
            set_stop();
            if (rgb_active) {
              rgb_request("Ff");
            }
            
            move_march = 1;
            spd = 12;
            set_speed();
            step_weight_factor = .2;
            move_forward = 1;
          }
        }
      } else if (ps2x.ButtonReleased(PSB_PAD_UP)) {
        if (ps2_select == 1 || ps2_select == 2 || ps2_select == 3) {
          if (move_forward) {
            move_forward = 0;
            if (debug1)
              Serial.println(F("stop forward"));
          }
          if (move_march) {
            start_stop = 0;
            move_march = 0;
          }
          if (move_trot) {
            start_stop = 0;
            move_trot = 0;
          }
        }
      }
  
      if (ps2x.Button(PSB_PAD_RIGHT)) {
        if (ps2_select == 1) {
          if (!move_right) {
            move_right = 1;
            if (debug1)
              Serial.println(F("move right"));
          }
          x_dir = map(ps2x.Analog(PSS_RX), 0, 255, move_x_steps[0], move_x_steps[1]);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        }
      } else if (ps2x.ButtonReleased(PSB_PAD_RIGHT)) {
        if (ps2_select == 1) {
          if (move_right) {
            move_right = 0;
            if (debug1)
              Serial.println(F("stop move right"));
          }
        } else if (ps2_select == 2) {
        } else if (ps2_select == 3) {
        }
      }
    
      if (ps2x.Button(PSB_PAD_LEFT)) {
        if (ps2_select == 1) {
          if (!move_left) {
            move_left = 1;
            if (debug1)
              Serial.println(F("move left"));
          }
          x_dir = map(ps2x.Analog(PSS_RX), 0, 255, move_x_steps[1], move_x_steps[0]);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        }
      } else if (ps2x.ButtonReleased(PSB_PAD_LEFT)) {
        if (ps2_select == 1) {
          if (move_left) {
            move_left = 0;
            if (debug1)
              Serial.println(F("stop move left"));
          }
        } else if (ps2_select == 2) {
        } else if (ps2_select == 3) {
        }
      }  
  
      if (ps2x.ButtonPressed(PSB_PAD_DOWN)) {
        if (ps2_select == 1 || ps2_select == 2 || ps2_select == 3 || ps2_select == 4) {
          if (!ps2x.Button(PSB_L1) && !ps2x.Button(PSB_L2) && !ps2x.Button(PSB_R1) && !ps2x.Button(PSB_R2)) {
            set_stop();
            if (debug1)
              Serial.println(F("stay"));
            if (rgb_active) {
              rgb_request("Hg");
            }
            start_stop = 0;
            set_stay();
            if (oled_active) {
              oled_request("b");
            }
          }
        }
      } else if (ps2x.ButtonReleased(PSB_PAD_DOWN)) {
        if (ps2_select == 1 || ps2_select == 2 || ps2_select == 3 || ps2_select == 4) {
          if (!ps2x.Button(PSB_L1) && !ps2x.Button(PSB_L2) && !ps2x.Button(PSB_R1) && !ps2x.Button(PSB_R2)) {
            if (!activeServo[RFF] && !activeServo[LFF] && !activeServo[RRF] && !activeServo[LRF]) {
              spd = 1;
              set_speed();
              move_loops = 2;
              move_switch = 2;
              for (int i = 0; i < TOTAL_SERVOS; i++) {
                servoPos[i] = servoHome[i];
              }
              start_stop = 1;
              move_wake = 1;
            }
          }
        }
      }
  
  
      //TRIGGER BUTTONS
      if (ps2x.Button(PSB_L1)) {
        if (ps2_select == 1) {
          if (!move_roll) {
            set_stop();
            move_roll = 1;
            if (debug1)
              Serial.println(F("move roll"));
          }
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max, move_steps_min);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        } else if (ps2_select == 2) {
          set_stop();
          if (debug1)
            Serial.println(F("sit"));
          if (rgb_active) {
            rgb_request("Hi");
          }
          start_stop = 0;
          set_sit();
        }
      } else if (ps2x.ButtonReleased(PSB_L1)) {
        if (ps2_select == 1) {
          if (move_roll) {
            move_roll = 0;
            if (debug1)
              Serial.println(F("stop roll"));
          }
        }
      }

      if (ps2x.Button(PSB_L2)) {
        if (ps2_select == 1) {
          if (!move_pitch) {
            set_stop();
            move_pitch = 1;
            if (debug1)
              Serial.println(F("move pitch"));
          }
          x_dir = map(ps2x.Analog(PSS_RX), 0, 255, move_steps_min / 2, move_steps_max / 2);
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max / 3, move_steps_min / 3);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        } else if (ps2_select == 2) {
          set_stop();
          if (debug1)
            Serial.println(F("kneel"));
          start_stop = 0;
          set_kneel();
        }
      } else if (ps2x.ButtonReleased(PSB_L2)) {
        if (ps2_select == 1) {
          if (move_pitch) {
            move_pitch = 0;
            if (debug1)
              Serial.println(F("stop pitch"));
          }
        }
      }

      if (ps2x.Button(PSB_R1)) {
        if (ps2_select == 1) {
          if (!move_roll_body) {
            set_stop();
            move_roll_body = 1;
            if (debug1)
              Serial.println(F("move roll_body"));
          }
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max / 2, move_steps_min / 2);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        } else if (ps2_select == 2) {
          set_stop();
          if (debug1)
            Serial.println(F("crouch"));
          if (rgb_active) {
            rgb_request("Hh");
          }
          start_stop = 0;
          set_crouch();
        }
      } else if (ps2x.ButtonReleased(PSB_R2)) {
        if (ps2_select == 1) {
          if (move_roll_body) {
            move_roll_body = 0;
            if (debug1)
              Serial.println(F("stop roll_body"));
          }
        }
      }

      if (ps2x.Button(PSB_R2)) {
        if (ps2_select == 1) {
          if (!move_pitch_body) {
            set_stop();
            move_pitch_body = 1;
            if (debug1)
              Serial.println(F("move pitch_body"));
          }
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_steps_max, move_steps_min);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        } else if (ps2_select == 2) {
          set_stop();
          if (debug1)
            Serial.println(F("lay"));
          if (rgb_active) {
            rgb_request("Kd");
          }
          start_stop = 0;
          set_lay();
        }
      } else if (ps2x.ButtonReleased(PSB_R2)) {
        if (ps2_select == 1) {
          if (move_pitch_body) {
            move_pitch_body = 0;
            if (debug1)
              Serial.println(F("stop pitch_body"));
          }
        }
      }
  

      //SHAPE BUTTONS
      if (ps2x.ButtonPressed(PSB_TRIANGLE)) {
        if (ps2_select == 1) {
          set_stop();
          if (!move_y_axis) {
            move_y_axis = 1;
            if (debug1)
              Serial.println(F("move y_axis"));
          }
        } else if (ps2_select == 2) {
          set_stop();
          if (!move_wman) {
            move_wman = 1;
            if (debug1)
              Serial.println(F("move wman"));
          }
        }
      } else if (ps2x.ButtonReleased(PSB_TRIANGLE)) {
        if (ps2_select == 1) {
          if (move_y_axis) {
            move_y_axis = 0;
            if (debug1)
              Serial.println(F("stop y_axis"));
          }
        } else if (ps2_select == 2) {
          if (move_wman) {
            move_wman = 0;
            if (debug1)
              Serial.println(F("stop wman"));
          }
        }
      }

      //poll steps stick
      if (ps2x.Button(PSB_TRIANGLE)) {
        if (ps2_select == 1) {
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_y_steps[0], move_y_steps[1]);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.print(move_steps);
          }
          if (move_steps < 0) {
            move_steps = map(move_steps, -150, 150, move_x_steps[0], move_x_steps[1]);
          }
          if (debug2) {
            Serial.print(F(" / ")); Serial.println(move_steps);
          }
        }
      }
  
      if (ps2x.ButtonPressed(PSB_CROSS)) {
        if (ps2_select == 1) {
          set_stop();
          if (!move_x_axis) {
            move_x_axis = 1;
            if (debug1)
              Serial.println(F("move x_axis"));
          }
        }
      } else if (ps2x.ButtonReleased(PSB_CROSS)) {
        if (ps2_select == 1) {
          if (move_x_axis) {
            move_x_axis = 0;
            if (debug1)
              Serial.println(F("stop x_axis"));
          }
        }
      }

      //poll steps stick
      if (ps2x.Button(PSB_CROSS)) {
        if (ps2_select == 1) {
          move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_x_steps[0], move_x_steps[1]);
          if (debug2) {
            Serial.print(F("move steps: ")); Serial.println(move_steps);
          }
        }
      }
  
      if (ps2x.ButtonPressed(PSB_CIRCLE)) {
        if (debug1) {
          Serial.println(F("Circle"));
          Serial.print(F("speed up +1 : "));
        }
        spd -= 1;
        if (spd < max_spd) spd = max_spd;
        set_speed();
        if (debug1) {
          Serial.println(spd);
        }
      }

      if (ps2x.ButtonPressed(PSB_SQUARE)) {
        if (debug1) {
          Serial.println(F("Square"));
          Serial.print(F("speed down -1 : "));
        }
        spd += 1;
        if (spd > min_spd) spd = min_spd;
        set_speed();
        if (debug1)
          Serial.println(spd);
      }
  
  
  
      //LEG / CALIBRATION CONTROLS
      if (ps2_select == 4) {
        //RF
        if (ps2x.Button(PSB_R1)) {
          if (ps2x.Button(PSB_PAD_UP)) {
            if (!activeServo[RFC]) {
              int ms = servoPos[RFC];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RF, RFC, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RFC: ")); Serial.println(limit_target(RFC, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_RIGHT)) {
            if (!activeServo[RFF]) {
              int ms = servoPos[RFF];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RF, RFF, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RFF: ")); Serial.println(limit_target(RFF, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_DOWN)) {
            if (!activeServo[RFT]) {
              int ms = servoPos[RFT];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RF, RFT, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RFT: ")); Serial.println(limit_target(RFT, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_LEFT)) {
            if (!activeServo[RFT]) {
              move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_c_steps[0] / 1.5, move_c_steps[1] / 1.5);
              update_sequencer(LF, LFT, spd, (servoHome[LFT] + move_steps), 0, 0);
              update_sequencer(LR, LRT, spd, (servoHome[LRT] + move_steps), 0, 0);
              update_sequencer(RF, RFT, spd, (servoHome[RFT] + move_steps), 0, 0);
              update_sequencer(RR, RRT, spd, (servoHome[RRT] + move_steps), 0, 0);
            }
          } else if (ps2x.ButtonReleased(PSB_PAD_UP) || ps2x.ButtonReleased(PSB_PAD_RIGHT) || ps2x.ButtonReleased(PSB_PAD_LEFT) || ps2x.ButtonReleased(PSB_PAD_DOWN)) {
            set_stop_active();
          }
        }
  
        //LF
        if (ps2x.Button(PSB_L1)) {
          if (ps2x.Button(PSB_PAD_UP)) {
            if (!activeServo[LFC]) {
              int ms = servoPos[LFC];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              if (debug1) {
                Serial.print(F("LFC: ")); Serial.println(limit_target(LFC, ms, 0));
              }
              update_sequencer(LF, LFC, spd, ms, 0, 0);
            }
          } else if (ps2x.Button(PSB_PAD_RIGHT)) {
            if (!activeServo[LFF]) {
              int ms = servoPos[LFF];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(LF, LFF, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("LFF: ")); Serial.println(limit_target(LFF, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_DOWN)) {
            if (!activeServo[LFT]) {
              int ms = servoPos[LFT];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(LF, LFT, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("LFT: ")); Serial.println(limit_target(LFT, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_LEFT)) {
            move_steps++;
            if (!activeServo[RRC]) {
              move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_c_steps[0] / 1.5, move_c_steps[1] / 1.5);
              update_sequencer(LF, LFC, spd, (servoHome[LFC] + move_steps), 0, 0);
              update_sequencer(LR, LRC, spd, (servoHome[LRC] + move_steps), 0, 0);
              update_sequencer(RF, RFC, spd, (servoHome[RFC] + move_steps), 0, 0);
              update_sequencer(RR, RRC, spd, (servoHome[RRC] + move_steps), 0, 0);
            }
          }
        }
  
        //RR
        if (ps2x.Button(PSB_R2)) {
          if (ps2x.Button(PSB_PAD_UP)) {
            if (!activeServo[RRC]) {
              int ms = servoPos[RRC];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RR, RRC, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RRC: ")); Serial.println(limit_target(RRC, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_RIGHT)) {
            if (!activeServo[RRF]) {
              int ms = servoPos[RRF];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RR, RRF, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RRF: ")); Serial.println(limit_target(RRF, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_DOWN)) {
            if (!activeServo[RRT]) {
              int ms = servoPos[RRT];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(RR, RRT, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("RRT: ")); Serial.println(limit_target(RRT, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_LEFT)) {
            if (!activeServo[RFC] && !activeServo[RFT]) {
              move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_c_steps[0] / 1.5, move_c_steps[1] / 1.5);
              if (debug1) {
                Serial.print(F("T/C move steps: ")); Serial.println(move_steps);
              }
              update_sequencer(LF, LFT, spd, (servoHome[LFT] + move_steps), 0, 0);
              update_sequencer(LF, LFC, spd, (servoHome[LFC] + move_steps), 0, 0);
              update_sequencer(LR, LRT, spd, (servoHome[LRT] + move_steps), 0, 0);
              update_sequencer(LR, LRC, spd, (servoHome[LRC] + move_steps), 0, 0);
              update_sequencer(RF, RFT, spd, (servoHome[RFT] + move_steps), 0, 0);
              update_sequencer(RF, RFC, spd, (servoHome[RFC] + move_steps), 0, 0);
              update_sequencer(RR, RRT, spd, (servoHome[RRT] + move_steps), 0, 0);
              update_sequencer(RR, RRC, spd, (servoHome[RRC] + move_steps), 0, 0);
            }
          }
        }
  
        //LR
        if (ps2x.Button(PSB_L2)) {
          if (ps2x.Button(PSB_PAD_UP)) {
            if (!activeServo[LRC]) {
              int ms = servoPos[LRC];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(LR, LRC, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("LRC: ")); Serial.println(limit_target(LRC, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_RIGHT)) {
            if (!activeServo[LRF]) {
              int ms = servoPos[LRF];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(LR, LRF, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("LRF: ")); Serial.println(limit_target(LRF, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_DOWN)) {
            if (!activeServo[LRT]) {
              int ms = servoPos[LRT];
              if (ps2x.Button(PSB_TRIANGLE)) {
                ms += 1;
              } else if (ps2x.Button(PSB_CROSS)) {
                ms -= 1;
              }
              update_sequencer(LR, LRT, spd, ms, 0, 0);
              if (debug1) {
                Serial.print(F("LRT: ")); Serial.println(limit_target(LRT, ms, 0));
              }
            }
          } else if (ps2x.Button(PSB_PAD_LEFT)) {
            if (!activeServo[RRF]) {
              move_steps = map(ps2x.Analog(PSS_RY), 0, 255, move_c_steps[0] / 1.5, move_c_steps[1] / 1.5);
              update_sequencer(LF, LFF, spd, (servoHome[LFF] + move_steps), 0, 0);
              update_sequencer(LR, LRF, spd, (servoHome[LRF] + move_steps), 0, 0);
              update_sequencer(RF, RFF, spd, (servoHome[RFF] + move_steps), 0, 0);
              update_sequencer(RR, RRF, spd, (servoHome[RRF] + move_steps), 0, 0);
            }
          }
        }
      }
    }
  } else {
    for (int i = 0; i < 10; i++) {
      ps2x.read_gamepad(false, false);
      delay(100);
    }
    if (rgb_active) {
      rgb_request("vGMRNRn");
    }
    digitalWrite(OE_PIN, LOW);
    pwm_oe = 1;
  }

  lastPS2Update = millis();
}

/*
   -------------------------------------------------------
   PIR Check
    :provide general description and explanation here
   -------------------------------------------------------
*/
void pir_check() {
  int mactive = mpu_active;
  int uactive = uss_active;

  pir_val = digitalRead(PIR_SENSOR);
  pir_wait--;
  if (pir_val == HIGH) {
    if (pir_state == LOW) {
      if (pir_wait < 1) {
        if (rgb_active) {
          rgb_request("Fh");
        }
        if (debug1)
          Serial.println(F("Motion detected!"));

        //disable mpu and uss sensors while in alert
        if (mpu_active) mpu_active = 0;
        if (uss_active) uss_active = 0;

        set_stop_active();
        pir_state = HIGH;
        for (int l = 0; l < TOTAL_LEGS; l++) {
          servoSequence[l] = 0;
        }
        for (int m = 0; m < TOTAL_SERVOS; m++) {
          int sp = 1;
          if (is_left_leg(m)) {
            if (!is_front_leg(m) && (is_femur(m) || is_tibia(m))) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] - 30), 0);
            } else if (is_front_leg(m) && (is_femur(m) || is_tibia(m))) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] - 60), 0);
            } else if (is_front_leg(m)) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] - 20), 0);
              servoStepMoves[m][1] = limit_target(m, (servoHome[m] + 35), 0);
            } else {
              servoStepMoves[m][1] = limit_target(m, (servoHome[m] + 35), 0);
            }
          } else {
            if (!is_front_leg(m) && (is_femur(m) || is_tibia(m))) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] + 30), 0);
            } else if (is_front_leg(m) && (is_femur(m) || is_tibia(m))) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] + 60), 0);
            } else if (is_front_leg(m)) {
              servoStepMoves[m][0] = limit_target(m, (servoHome[m] + 20), 0);
              servoStepMoves[m][1] = limit_target(m, (servoHome[m] + 35), 0);
            } else {
              servoStepMoves[m][1] = limit_target(m, (servoHome[m] + 35), 0);
            }
          }
          servoStepMoves[m][2] = servoHome[m];
        }
        spd_c = 1;
        spd_f = 1;
        spd_t = 1;
        move_loops = 0;
        move_delay = 300;
        start_stop = 1;
        move_sequence = 1;

        if (oled_active) {
          oled_request("e");
        } else {
          if (debug1)
            Serial.println(F("INTRUDER ALERT!"));
        }

        if (melody_active) {
          play_phrases();
        } else if (buzz_active) {
          for (int b = 3; b > 0; b--) {
            tone(BUZZ, 500);
            delay(70);
            noTone(BUZZ);
            delay(70);
          }
          noTone(BUZZ);
        }

        if (rgb_active) {
          rgb_request("Fj");
        }
      }
    }
  } else {
    if (pir_state == HIGH) {
      Serial.println(F("Motion ended!"));
      pir_state = LOW;
      pir_wait = 100; //state machine cycles, not ms
      if (oled_active) {
        oled_request("f");
      } else {
        if (debug1)
          Serial.println(F("ALERT COMPLETE!"));
      }

      pwm_oe = 0;
      pir_reset = 1;
    } else if (pir_reset) {
      if (!move_sequence) {
        pir_reset = 0;
        move_delays[0] = 3000;
        move_delay_sequences[0] = 7;
        move_delays[1] = 3000;
        move_delay_sequences[1] = 7;
        start_stop = 0;
        delay_sequences();

        if (rgb_active) {
          rgb_request("Jf");
        }
      }
      if (oled_active) {
        oled_request("f");
      }

      //re-enable mpu and uss sensors if enabled prior to alert
      mpu_active = mactive;
      uss_active = uactive;
    }
  }

  lastPIRUpdate = millis();
}




/*
   -------------------------------------------------------
   Check Ultrasonic Reading
    :provide general description and explanation here
   -------------------------------------------------------
*/
int uss_check() {
  //check right sensor
  int dist_rt = command_slave("b");
  if (dist_rt != prev_distance_r && ((dist_rt > (prev_distance_r + distance_tolerance)) || (dist_rt < (prev_distance_r - distance_tolerance)))) {
    distance_r = prev_distance_r = dist_rt;
  }

  //check left sensor
  int dist_lt = command_slave("a");
  if (dist_lt != prev_distance_l && ((dist_lt > (prev_distance_l + distance_tolerance)) || (dist_lt < (prev_distance_l - distance_tolerance)))) {
    distance_l = prev_distance_l = dist_lt;
  }

  if (dist_rt < distance_alarm || dist_lt < distance_alarm) {
    distance_alarm_set++;

    if (debug5 && distance_alarm_set > 0) {
      Serial.print(F("USS ALARM #"));Serial.println(distance_alarm_set);
    }

    if (oled_active && distance_alarm_set < 3) {
      oled_request("c");
    } else {
//      set_stop_active();
      if (oled_active) {
        oled_request("e");
      }
      rgb_request("MQNOFzn");
//      delay(5000);
//      set_stop();
    }
    
    if (debug5) {
      if (plotter) {
        Serial.print(F("Lt:"));
        if (distance_l > (distance_alarm*5)) {
          Serial.print((distance_alarm*5));
        } else {
          Serial.print(distance_l);
        }
        Serial.print(F("\t"));
        Serial.print(F("Rt:"));
        if (distance_r > (distance_alarm*5)) {
          Serial.print((distance_alarm*5));
        } else {
          Serial.print(distance_r);
        }
        Serial.print(F("\t"));
        Serial.println();
      } else {
        Serial.print(F("Dist Right: "));
        Serial.println(distance_r);
        Serial.print(F("Dist Left: "));
        Serial.println(distance_l);
      }
    }
  } else {
    distance_alarm_set = 0;
  }

  lastUSSUpdate = millis();
}


/*
   -------------------------------------------------------
   Get MPU Data
    :provide general description and explanation here
   -------------------------------------------------------
*/
void get_mpu() {
  // === Read acceleromter data === //
  Wire.beginTransmission(MPU);
  Wire.write(0x3B); // Start with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 6, true); // Read 6 registers total, each axis value is stored in 2 registers
  //For a range of +-2g, we need to divide the raw values by 16384, according to the datasheet
  AccX = (Wire.read() << 8 | Wire.read()) / 16384.0; // X-axis value
  AccY = (Wire.read() << 8 | Wire.read()) / 16384.0; // Y-axis value
  AccZ = (Wire.read() << 8 | Wire.read()) / 16384.0; // Z-axis value
  // Calculating Roll and Pitch from the accelerometer data
  accAngleX = (atan(AccY / sqrt(pow(AccX, 2) + pow(AccZ, 2))) * 180 / PI) - AccErrorX; // AccErrorX ~(0.58) See the calculate_IMU_error()custom function for more details
  accAngleY = (atan(-1 * AccX / sqrt(pow(AccY, 2) + pow(AccZ, 2))) * 180 / PI) - AccErrorY; // AccErrorY ~(-1.58)

  // === Read gyroscope data === //
  previousTime = currentTime;        // Previous time is stored before the actual time read
  currentTime = millis();            // Current time actual time read
  elapsedTime = (currentTime - previousTime) / 1000; // Divide by 1000 to get seconds
  Wire.beginTransmission(MPU);
  Wire.write(0x43); // Gyro data first register address 0x43
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 6, true); // Read 4 registers total, each axis value is stored in 2 registers
  GyroX = (Wire.read() << 8 | Wire.read()) / 131.0; // For a 250deg/s range we have to divide first the raw value by 131.0, according to the datasheet
  GyroY = (Wire.read() << 8 | Wire.read()) / 131.0;
  GyroZ = (Wire.read() << 8 | Wire.read()) / 131.0;

  // Correct the outputs with the calculated error values
  GyroX = GyroX + abs(GyroErrorX); // GyroErrorX ~(-0.56)
  GyroY = GyroY + abs(GyroErrorY); // GyroErrorY ~(2)
  GyroZ = GyroZ + abs(GyroErrorZ); // GyroErrorZ ~ (-0.8)

  // Currently the raw values are in degrees per seconds, deg/s, so we need to multiply by seconds (s) to get the angle in degrees
  gyroAngleX = gyroAngleX + GyroX * elapsedTime; // deg/s * s = deg
  gyroAngleY = gyroAngleY + GyroY * elapsedTime;
  myaw =  (myaw + GyroZ * elapsedTime);

  // Complementary filter - combine acceleromter and gyro angle values
  mroll = (0.96 * gyroAngleX + 0.04 * accAngleX);
  mpitch = (0.96 * gyroAngleY + 0.04 * accAngleY);

  set_axis(mroll, mpitch);

  lastMPUUpdate = millis();
}

/*
   -------------------------------------------------------
   Set MPU Error Data
    :provide general description and explanation here
   -------------------------------------------------------
*/
void calculate_IMU_error() {
  // Read accelerometer values 200 times
  while (mpu_c < 200) {
    Wire.beginTransmission(MPU);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU, 6, true);
    AccX = (Wire.read() << 8 | Wire.read()) / 16384.0 ;
    AccY = (Wire.read() << 8 | Wire.read()) / 16384.0 ;
    AccZ = (Wire.read() << 8 | Wire.read()) / 16384.0 ;
    // Sum all readings
    AccErrorX = AccErrorX + ((atan((AccY) / sqrt(pow((AccX), 2) + pow((AccZ), 2))) * 180 / PI));
    AccErrorY = AccErrorY + ((atan(-1 * (AccX) / sqrt(pow((AccY), 2) + pow((AccZ), 2))) * 180 / PI));
    mpu_c++;
  }
  //Divide the sum by 200 to get the error value
  AccErrorX = AccErrorX / 200;
  AccErrorY = AccErrorY / 200;
  mpu_c = 0;
  // Read gyro values 200 times
  while (mpu_c < 200) {
    Wire.beginTransmission(MPU);
    Wire.write(0x43);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU, 6, true);
    GyroX = Wire.read() << 8 | Wire.read();
    GyroY = Wire.read() << 8 | Wire.read();
    GyroZ = Wire.read() << 8 | Wire.read();
    // Sum all readings
    GyroErrorX = GyroErrorX + (GyroX / 131.0);
    GyroErrorY = GyroErrorY + (GyroY / 131.0);
    GyroErrorZ = GyroErrorZ + (GyroZ / 131.0);
    mpu_c++;
  }
  //Divide the sum by 200 to get the error value
  GyroErrorX = GyroErrorX / 200;
  GyroErrorY = GyroErrorY / 200;
  GyroErrorZ = GyroErrorZ / 200;

  // Print the error values on the Serial Monitor
  if (debug5) {
    Serial.print(F("AccErrorX: "));
    Serial.println(AccErrorX);
    Serial.print(F("AccErrorY: "));
    Serial.println(AccErrorY);
    Serial.print(F("GyroErrorX: "));
    Serial.println(GyroErrorX);
    Serial.print(F("GyroErrorY: "));
    Serial.println(GyroErrorY);
    Serial.print(F("GyroErrorZ: "));
    Serial.println(GyroErrorZ);
  }
}

/*
   -------------------------------------------------------
   Check Amperage Level
    :provide general description and explanation here
   -------------------------------------------------------
*/
void amperage_check(int aloop) {
  unsigned int x=0;
  float AcsValue=0.0,Samples=0.0,AvgAcs=0.0,AcsValueF=0.0;

  for (int x = 0; x < 150; x++){ //Get 150 samples
    AcsValue = analogRead(AMP_PIN);     //Read current sensor values   
    Samples = Samples + AcsValue;  //Add samples together
//state machine this if possible... adds blocking delay to code, all be it miniscule!!
    delay(3); // let ADC settle before next sample 3ms
  }
  AvgAcs=Samples/150.0;//Taking Average of Samples
  AcsValueF = (2.5 - (AvgAcs * (5.0 / 1024.0)) )/0.177;
  
  AcsValueF = abs(AcsValueF)*1.8;
  if (debug4) {
    Serial.print(AvgAcs);
    Serial.print("/");
    Serial.print(AcsValueF);
    Serial.print("/");
    Serial.print(amp_limit);
  }
  if (AcsValueF > amp_limit) {
    ampInterval = 10;
    if (amp_cnt < amp_thresh) {
      amp_cnt++;
      rgb_request("u");
      if (debug4) {
        Serial.print("\tlimit met, counting... ");Serial.println(amp_cnt);
      }
    } else {
      rgb_request("A");
      detach_all();
      if (debug4) {
        Serial.println(F("\tthresh met, shutdown pwm!"));
      }
    }

    if (rgb_active) {
      rgb_request("MONRDn");
    }
  } else if (AcsValueF > amp_limit/2) {
    if (amp_cnt < amp_thresh) {
      rgb_request("u");
      amp_cnt++;
    } else {
      rgb_request("A");
      detach_all();
      if (debug4) {
        Serial.println(F("\tsoft thresh met, shutdown!"));
      }
    }
    ampInterval = 50;
    amp_warning = 2;

    if (rgb_active) {
      rgb_request("MTNRDn");
    }
    if (debug4) {
      Serial.print("\twarning 2 set - cnt: ");Serial.println(amp_cnt);
    }
  } else if (AcsValueF > amp_limit/3) {
    if (amp_cnt) {
      amp_cnt--;
    }
    rgb_request("t");
    ampInterval = 300;
    amp_warning = 1;

    if (rgb_active) {
      rgb_request("MPNRDn");
    }
    if (debug4) {
      Serial.print("\twarning 1 set - cnt: ");Serial.println(amp_cnt);
    }
  } else {
    if (amp_cnt) {
      amp_cnt--;
    }
    ampInterval = 1000;
    if (debug4) {
      Serial.print("\tcnt: ");Serial.println(amp_cnt);
    }
  }

  if (!aloop) {
    ampInterval = 0;
  }
   lastAmpUpdate = millis();
}

/*
   -------------------------------------------------------
   Check Battery Level
    :provide general description and explanation here
   -------------------------------------------------------
*/
void battery_check() {
  int sensorValue = analogRead(BATT_MONITOR);
  batt_voltage = sensorValue * (5.00 / 1023.00) * 2.7; // Convert the reading values from 5v to 12V
  batt_voltage += 1.07; //account for voltage drop, adding it back
  if (debug4) {
//  Serial.print("batt check: ");Serial.print(sensorValue); Serial.print(F(" / ")); Serial.println(batt_voltage);
  }
  if (batt_voltage < (batt_voltage_prev - .8) && batt_cnt < 3) {
    if (debug4) {
      Serial.print(batt_voltage); Serial.println(F(" - IGNORING BATT DROP of > 0.8v"));
    }
    batt_cnt++;
  } else if (batt_voltage >= (batt_voltage_prev + .05) || batt_voltage <= (batt_voltage_prev - .05)) {
    batt_cnt = 0;
    int batt_danger = 0;
    for (int i = 0; i<4; i++) {
      if (batt_voltage <= batt_levels[i]) {
        batt_danger++;

//DEV NOTE: may need to create specific pattern(s) to support this, else compromise, else refactor logic
        pattern_cnt = (i+1)*1;
        pattern_int -= (i+1)*20;

        rgb_request("GMONO");
        if (debug4) {
          Serial.print(batt_voltage); Serial.print(" - BATTERY LOW - WARNING #"); Serial.println(i);
        }
      }
    }

    if (batt_danger) {
      if (batt_skip < batt_danger || batt_skip > 2) {
        batt_skip = batt_danger;
        if (debug4) {
          Serial.print("BUTTON DANGER LEVEL #"); Serial.println(batt_danger);
        }
  
        if (rgb_active) {
          rgb_request("vn");
        }
        if (buzz_active) {
//DEV NOTE: refactor this, incremental pattern_cnt will be removed!
          for (int b = pattern_cnt; b > 0; b--) {
            tone(BUZZ, 1000);
            delay(70);
            noTone(BUZZ);
            delay(70);
          }
          noTone(BUZZ);
        }
    
        if (batt_danger == 4) {
          if (debug4) {
            Serial.print(batt_voltage); Serial.println(F(" - HALTED!"));
            tone(BUZZ, 4000);
          }
          rgb_request("ADn");
          while(1);
        } else if (batt_danger < 3) {
          batt_voltage_prev = batt_voltage;
        }
      }
    }      
  }

  lastBatteryUpdate = millis();
}



/*
   -------------------------------------------------------
   Operational Functions
   -------------------------------------------------------
*/
void init_home() {
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    for (int j = 0; j < 6; j++) {
      servoSweep[i][j] = 0;
    }
    for (int j = 0; j < 8; j++) {
      servoRamp[i][j] = 0;
    }

    servoSequence[i] = 0;
    servoDelay[i][0] = 0;
    servoDelay[i][1] = 0;
    servoStep[i] = 0;
    servoSwitch[i] = 0;
    servoSpeed[i] = (spd * 1.5);
    activeSweep[i] = 0;
  }

  //set crouched femurs
  servoPos[RFF] = servoLimit[RFF][0] + 30;
  pwm1.setPWM(servoSetup[RFF][1], 0, servoPos[RFF]);
  servoPos[LRF] = servoLimit[LRF][0] - 30;
  pwm1.setPWM(servoSetup[LRF][1], 0, servoPos[LRF]);
  servoPos[RRF] = servoLimit[RRF][0] + 30;
  pwm1.setPWM(servoSetup[RRF][1], 0, servoPos[RRF]);
  servoPos[LFF] = servoLimit[LFF][0] - 30;
  pwm1.setPWM(servoSetup[LFF][1], 0, servoPos[LFF]);
  delay(1000);

  //set crouched coaxes
  servoPos[RFC] = servoHome[RFC];
  pwm1.setPWM(servoSetup[RFC][1], 0, servoHome[RFC]);
  servoPos[LRC] = servoHome[LRC];
  pwm1.setPWM(servoSetup[LRC][1], 0, servoHome[LRC]);
  servoPos[RRC] = servoHome[RRC];
  pwm1.setPWM(servoSetup[RRC][1], 0, servoHome[RRC]);
  servoPos[LFC] = servoHome[LFC];
  pwm1.setPWM(servoSetup[LFC][1], 0, servoHome[LFC]);
  delay(1000);

  //set crouched tibias
  servoPos[RFT] = servoLimit[RFT][1];
  pwm1.setPWM(servoSetup[RFT][1], 0, servoLimit[RFT][1]);
  servoPos[LRT] = servoLimit[LRT][1];
  pwm1.setPWM(servoSetup[LRT][1], 0, servoLimit[LRT][1]);
  servoPos[RRT] = servoLimit[RRT][1];
  pwm1.setPWM(servoSetup[RRT][1], 0, servoLimit[RRT][1]);
  servoPos[LFT] = servoLimit[LFT][1];
  pwm1.setPWM(servoSetup[LFT][1], 0, servoLimit[LFT][1]);
  delay(1000);

  set_stay();
}


void detach_all() {
  if (debug4) {
    Serial.println(F("detaching all servos!"));
  }
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    activeServo[i] = 0;
    activeSweep[i] = 0;
    pwm1.setPWM(i, 0, 0);
  }
  digitalWrite(OE_PIN, HIGH);
  digitalWrite(PWR_PIN, LOW);
  pwm_active = 0;
  amp_active = 0;

  if (rgb_active) {
    rgb_request("MONRAGn");
  }
}

void set_ramp(int servo, float sp, float r1_spd, float r1_dist, float r2_spd, float r2_dist) {
  servoRamp[servo][0] = sp;  //speed
  servoRamp[servo][1] = abs(servoPos[servo] - targetPos[servo]);  //set distance

  if (!r1_spd) r1_spd = sp + (sp * ramp_spd); 
  if (!r2_spd) r2_spd = sp + (sp * ramp_spd); 
  if (!r1_dist) r1_dist = (servoRamp[servo][1] * ramp_dist); 
  if (!r2_dist) r2_dist = (servoRamp[servo][1] * ramp_dist); 

  servoRamp[servo][2] = r1_spd;  //ramp up spd
  servoRamp[servo][3] = r1_dist;  //ramp up dist
  servoRamp[servo][4] = (servoRamp[servo][2]-sp)/r1_dist;  //ramp up spd inc
  servoRamp[servo][5] = r2_spd;  //ramp down spd
  servoRamp[servo][6] = r2_dist;  //ramp down dist
  servoRamp[servo][7] = (servoRamp[servo][5]-sp)/r2_dist;  //ramp down spd inc

  if (debug3 && servo == debug_servo) {
    Serial.print("set_ramp: sPos: "); Serial.print(servoPos[servo]);
    Serial.print("\ttPos: "); Serial.print(targetPos[servo]);
    Serial.print("\tr1_dist: "); Serial.print(r1_dist);
    Serial.print("\tr2_dist: "); Serial.println(r2_dist);
    Serial.print("ramp:"); 
    Serial.print("\t"); Serial.print(servoRamp[servo][0]);
    Serial.print("\t"); Serial.print(servoRamp[servo][1]);
    Serial.print("\t"); Serial.print(servoRamp[servo][2]);
    Serial.print("\t"); Serial.print(servoRamp[servo][3]);
    Serial.print("\t"); Serial.print(servoRamp[servo][4]);
    Serial.print("\t"); Serial.print(servoRamp[servo][5]);
    Serial.print("\t"); Serial.print(servoRamp[servo][6]);
    Serial.print("\t"); Serial.println(servoRamp[servo][7]);
    Serial.println();
  }
}

void go_home() {
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    activeServo[i] = 0;
    activeSweep[i] = 0;
    servoSpeed[i] = spd;
    servoPos[i] = servoHome[i];
    targetPos[i] = servoHome[i];
    if (servoSetup[i][0] == 1) {
      pwm1.setPWM(servoSetup[i][1], 0, servoHome[i]);
    }
    delay(20);
  }

  for (int i = 0; i < TOTAL_LEGS; i++) {
    servoSequence[i] = 0;
  }
}

void set_home() {
  for (int m = 0; m < TOTAL_SERVOS; m++) {
    activeServo[m] = 1;
    targetPos[m] = servoHome[m];
  }
}

void set_stop() {
  for (int m = 0; m < TOTAL_SERVOS; m++) {
    activeServo[m] = 0;
    activeSweep[m] = 0;
  }
  set_home();
}

void set_stop_active() {
  for (int m = 0; m < TOTAL_SERVOS; m++) {
    activeServo[m] = 0;
    activeSweep[m] = 0;
  }
  start_stop = 0;
  use_ramp = 0;

  move_y_axis = 0;
  move_x_axis = 0;
  move_roll = 0;
  move_roll_body = 0;
  move_pitch = 0;
  move_pitch_body = 0;
  move_trot = 0;
  move_forward = 0;
  move_backward = 0;
  move_left = 0;
  move_right = 0;
  move_march = 0;
  move_wake = 0;
  move_sequence = 0;
  move_demo = 0;
  move_wman = 0;
  move_funplay = 0;
  move_look_left = 0;
  move_look_right = 0;
  move_roll_x = 0;
  move_pitch_y = 0;
  move_kin_x = 0;
  move_kin_y = 0;
  move_yaw_x = 0;
  move_yaw_y = 0;
  move_yaw = 0;
  move_servo = 0;
  move_leg = 0;
}

void set_speed() {
  for (int i = 0; i < TOTAL_SERVOS; i++) {
    servoSpeed[i] = spd;
  }
  //recalc speed factor
  spd_factor = mapfloat(spd, min_spd, max_spd, min_spd_factor, max_spd_factor);
  if (rgb_active) {
    if (spd < 6) {
      rgb_request("MONOD");
    } else if (spd < 11) {
      rgb_request("MRNRE");
    } else if (spd < 21) {
      rgb_request("MPNPF");
    } else if (spd < 31) {
      rgb_request("MQNQG");
    } else {
      rgb_request("MSNSH");
    }
    rgb_request("xn");
  }
}



/*
   -------------------------------------------------------
   Move Functions
   -------------------------------------------------------
*/
//set pitch and roll axis from mpu data
void set_axis(float roll_step, float pitch_step) {
    float ar = abs(roll_step);
    float ap = abs(pitch_step);

    //check if oscillating, and on third oscillation, stop!
    if (ar > (mroll_prev + mpu_oscill_thresh) || ar < (mroll_prev - mpu_oscill_thresh)) {
      mpu_oscill_cnt--;
      Serial.print("oscillate: ");
      Serial.println(mpu_oscill_cnt);
    } else {
//      mpu_oscill_grace--;
//      if (!mpu_oscill_grace) {
//        mpu_oscill_grace = 3;
        mpu_oscill_cnt = mpu_oscill_limit;
//      }
    }

    for (int i = 0; i < TOTAL_SERVOS; i++) {
      byte skip = 0;
      float t = 0.0;
      float f = 0.0;
      if (is_tibia(i)) {
        t = servoHome[i];
      } else if (is_femur(i)) {
        f = servoHome[i];
      }

      if (ar <= (mroll_prev + mpu_trigger_thresh) && ar >= (mroll_prev - mpu_trigger_thresh)) {
        if (roll_step < 0) { //roll left
          if (is_tibia(i)) {
            t += ((abs(roll_step) * 0.65) * 4);
          } else if (is_femur(i)) {
            f -= ((abs(roll_step) * 0.4) * 4);
          }
        } else { //roll right
          if (is_tibia(i)) {
            t -= ((roll_step * 0.65) * 4);
          } else if (is_femur(i)) {
            f += ((roll_step * 0.4) * 4);
          }
        }
      } else {
        skip = 1;
      }
      mroll_prev = ar;

      if (ap <= (mpitch_prev + mpu_trigger_thresh) || ap >= (mpitch_prev - mpu_trigger_thresh)) {
        if (pitch_step < 0) { //pitch front down
          if (is_tibia(i)) {
            if (is_front_leg(i)) {
              (is_left_leg(i)) ? t -= ((abs(pitch_step) * 1.15) * 3) : t += ((abs(pitch_step) * 1.15) * 3);
            } else {
              (is_left_leg(i)) ? t += ((abs(pitch_step) * 1.15) * 3) : t -= ((abs(pitch_step) * 1.15) * 3);
            }
          }
        } else { //pitch front up
          if (is_tibia(i)) {
            if (is_front_leg(i)) {
              (is_left_leg(i)) ? t += ((abs(pitch_step) * 1.15) * 3) : t -= ((abs(pitch_step) * 1.15) * 3);
            } else {
              (is_left_leg(i)) ? t -= ((abs(pitch_step) * 1.15) * 3) : t += ((abs(pitch_step) * 1.15) * 3);
            }
          }
        }
        mpitch_prev = pitch_step;
      }
      
      if (!skip) {
        if (is_tibia(i)) {
          activeServo[i] = 1;
          servoSpeed[i] = (7*spd_factor);
          targetPos[i] = limit_target(i, t, 0);
        } else if (is_femur(i)) {
          activeServo[i] = 1;
          servoSpeed[i] = (12*spd_factor);
          targetPos[i] = limit_target(i, f, 0);
        }
        if (!mpu_oscill_cnt) {
//Serial.println("stop oscill delay");  
//maybe send home instead?    
          mpu_oscill_cnt = mpu_oscill_limit;
          delay(2000);
        }
      }
    }

}

void run_demo() {
  if (!move_delay_sequences[0] && !move_delay_sequences[9]) {
    ramp_dist = 0.2;
    ramp_spd = 0.5;
    use_ramp = 1;

    move_demo = 1;

    move_delays[0] = 300;
    move_delay_sequences[0] = 15;
    move_delays[1] = 300;
    move_delay_sequences[1] = 14;
    move_delays[2] = 1200;
    move_delay_sequences[2] = 1;
    move_delays[3] = 3000;
    move_delay_sequences[3] = 2;
    move_delays[4] = 900;
    move_delay_sequences[4] = 3;
    move_delays[5] = 900;
    move_delay_sequences[5] = 5;
    move_delays[6] = 1500;
    move_delay_sequences[6] = 6;
    move_delays[7] = 1500;
    move_delay_sequences[7] = 7;
    move_delays[8] = 1500;
    move_delay_sequences[8] = 4;
    
    move_delays[9] = 900;
    move_delay_sequences[9] = 13;

    move_delays[10] = 1500;
    move_delay_sequences[10] = 11;
    
    move_delays[11] = 1500;
    move_delay_sequences[11] = 12;

    move_delays[12] = 3000;
    move_delay_sequences[12] = 8;
    move_delays[13] = 6000;
    move_delay_sequences[13] = 9;
    move_delays[14] = 3000;
    move_delay_sequences[14] = 10;

    start_stop = 0;
    delay_sequences();
  }
}

void set_stay() {
  for (int m = 0; m < TOTAL_SERVOS; m++) {
    activeSweep[m] = 0;
    activeServo[m] = 1;
    targetPos[m] = servoHome[m];
    servoSpeed[m] = 10;
    if (is_tibia(m)) {
      servoSpeed[m] = 5;
    }
  }

  lastMoveDelayUpdate = millis();
}

void set_sit() {
  activeServo[LFC] = 1;
  servoSpeed[LFC] = 10;
  targetPos[LFC] = servoHome[LFC];
  activeServo[LRC] = 1;
  servoSpeed[LRC] = 10;
  targetPos[LRC] = servoHome[LRC];
  activeServo[RFC] = 1;
  servoSpeed[RFC] = 10;
  targetPos[RFC] = servoHome[RFC];
  activeServo[RRC] = 1;
  servoSpeed[RRC] = 10;
  targetPos[RRC] = servoHome[RRC];

  activeServo[LFT] = 1;
  servoSpeed[LFT] = 10;
  targetPos[LFT] = servoLimit[LFT][0];
  activeServo[RFT] = 1;
  servoSpeed[RFT] = 10;
  targetPos[RFT] = servoLimit[RFT][0];

  activeServo[LRT] = 1;
  servoSpeed[LRT] = 10;
  targetPos[LRT] = servoLimit[LRT][1];
  activeServo[RRT] = 1;
  servoSpeed[RRT] = 10;
  targetPos[RRT] = servoLimit[RRT][1];

  activeServo[LRF] = 1;
  servoSpeed[LRF] = 10;
  targetPos[LRF] = (servoLimit[LRF][0] - 30);
  activeServo[RRF] = 1;
  servoSpeed[RRF] = 10;
  targetPos[RRF] = (servoLimit[RRF][0] + 30);

  activeServo[LFF] = 1;
  servoSpeed[LFF] = 20;
  targetPos[LFF] = (servoLimit[LFF][1] + 90);
  activeServo[RFF] = 1;
  servoSpeed[RFF] = 20;
  targetPos[RFF] = (servoLimit[RFF][1] - 90);

  lastMoveDelayUpdate = millis();
}

void set_crouch() {
  activeServo[LFC] = 1;
  servoSpeed[LFC] = 10;
  targetPos[LFC] = servoHome[LFC];
  activeServo[LRC] = 1;
  servoSpeed[LRC] = 10;
  targetPos[LRC] = servoHome[LRC];
  activeServo[RFC] = 1;
  servoSpeed[RFC] = 10;
  targetPos[RFC] = servoHome[RFC];
  activeServo[RRC] = 1;
  servoSpeed[RRC] = 10;
  targetPos[RRC] = servoHome[RRC];

  activeServo[LFT] = 1;
  servoSpeed[LFT] = 10;
  targetPos[LFT] = servoLimit[LFT][1];
  activeServo[RFT] = 1;
  servoSpeed[RFT] = 10;
  targetPos[RFT] = servoLimit[RFT][1];

  activeServo[LRT] = 1;
  servoSpeed[LRT] = 10;
  targetPos[LRT] = servoLimit[LRT][1];
  activeServo[RRT] = 1;
  servoSpeed[RRT] = 10;
  targetPos[RRT] = servoLimit[RRT][1];

  activeServo[LRF] = 1;
  servoSpeed[LRF] = 10;
  targetPos[LRF] = (servoLimit[LRF][0] - 30);
  activeServo[RRF] = 1;
  servoSpeed[RRF] = 10;
  targetPos[RRF] = (servoLimit[RRF][0] + 30);

  activeServo[LFF] = 1;
  servoSpeed[LFF] = 20;
  targetPos[LFF] = (servoLimit[LFF][0] - 30);
  activeServo[RFF] = 1;
  servoSpeed[RFF] = 20;
  targetPos[RFF] = (servoLimit[RFF][0] + 30);

  lastMoveDelayUpdate = millis();
}

void set_lay() {
  activeServo[LFC] = 1;
  servoSpeed[LFC] = 20;
  targetPos[LFC] = (servoLimit[LFC][1]);
  activeServo[LRC] = 1;
  servoSpeed[LRC] = 20;
  targetPos[LRC] = (servoLimit[LRC][1]);
  activeServo[RFC] = 1;
  servoSpeed[RFC] = 20;
  targetPos[RFC] = (servoLimit[RFC][1]);
  activeServo[RRC] = 1;
  servoSpeed[RRC] = 20;
  targetPos[RRC] = (servoLimit[RRC][1]);

  activeServo[LFT] = 1;
  servoSpeed[LFT] = 10;
  targetPos[LFT] = servoLimit[LFT][1];
  activeServo[RFT] = 1;
  servoSpeed[RFT] = 10;
  targetPos[RFT] = servoLimit[RFT][1];

  activeServo[LRT] = 1;
  servoSpeed[LRT] = 10;
  targetPos[LRT] = servoLimit[LRT][1];
  activeServo[RRT] = 1;
  servoSpeed[RRT] = 10;
  targetPos[RRT] = servoLimit[RRT][1];

  activeServo[LRF] = 1;
  servoSpeed[LRF] = 10;
  targetPos[LRF] = (servoLimit[LRF][0]);
  activeServo[RRF] = 1;
  servoSpeed[RRF] = 10;
  targetPos[RRF] = (servoLimit[RRF][0]);

  activeServo[LFF] = 1;
  servoSpeed[LFF] = 20;
  targetPos[LFF] = (servoLimit[LFF][0]);
  activeServo[RFF] = 1;
  servoSpeed[RFF] = 20;
  targetPos[RFF] = (servoLimit[RFF][0]);

  lastMoveDelayUpdate = millis();
}

void set_kneel() {
  activeServo[LFC] = 1;
  servoSpeed[LFC] = 10;
  targetPos[LFC] = (servoLimit[LFC][0] - 10);
  activeServo[LRC] = 1;
  servoSpeed[LRC] = 10;
  targetPos[LRC] = (servoLimit[LRC][0] - 10);
  activeServo[RFC] = 1;
  servoSpeed[RFC] = 10;
  targetPos[RFC] = (servoLimit[RFC][0] + 10);
  activeServo[RRC] = 1;
  servoSpeed[RRC] = 10;
  targetPos[RRC] = (servoLimit[RRC][0] + 10);

  activeServo[LFT] = 1;
  servoSpeed[LFT] = 20;
  targetPos[LFT] = (servoHome[LFT] - 40);
  activeServo[RFT] = 1;
  servoSpeed[RFT] = 20;
  targetPos[RFT] = (servoHome[RFT] + 40);

  activeServo[LRT] = 1;
  servoSpeed[LRT] = 20;
  targetPos[LRT] = (servoHome[LRT] - 40);
  activeServo[RRT] = 1;
  servoSpeed[RRT] = 20;
  targetPos[RRT] = (servoHome[RRT] + 40);

  activeServo[LRF] = 1;
  servoSpeed[LRF] = 10;
  targetPos[LRF] = (servoLimit[LRF][1] + 90);
  activeServo[RRF] = 1;
  servoSpeed[RRF] = 10;
  targetPos[RRF] = (servoLimit[RRF][1] - 90);

  activeServo[LFF] = 1;
  servoSpeed[LFF] = 10;
  targetPos[LFF] = (servoLimit[LFF][1] + 90);
  activeServo[RFF] = 1;
  servoSpeed[RFF] = 10;
  targetPos[RFF] = (servoLimit[RFF][1] - 90);

  lastMoveDelayUpdate = millis();
}

void x_axis() {
  if (!activeSweep[RRT]) {
    servoSpeed[LFF] = limit_speed((10 * spd_factor));
    servoSweep[LFF][0] = limit_target(LFF, ((servoHome[LFF] - 70) + (move_steps * .7)), 0);
    servoSweep[LFF][1] = limit_target(LFF, ((servoHome[LFF] - 10) + (move_steps * .1)), 0);
    servoSweep[LFF][2] = 0;
    servoSweep[LFF][3] = 1;
    targetPos[LFF] = servoSweep[LFF][1];
    activeSweep[LFF] = 1;

    servoSpeed[LFT] = limit_speed((10 * spd_factor));
    servoSweep[LFT][0] = limit_target(LFT, ((servoHome[LFT] + 85) - (move_steps * .85)), 0);
    servoSweep[LFT][1] = limit_target(LFT, ((servoHome[LFT] + 25) - (move_steps * .25)), 0);
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;


    servoSpeed[RFF] = limit_speed((10 * spd_factor));
    servoSweep[RFF][0] = limit_target(RFF, ((servoHome[RFF] + 70) - (move_steps * .7)), 0); //65
    servoSweep[RFF][1] = limit_target(RFF, ((servoHome[RFF] + 10) - (move_steps * .1)), 0); //5
    servoSweep[RFF][2] = 0;
    servoSweep[RFF][3] = 1;
    targetPos[RFF] = servoSweep[RFF][1];
    activeSweep[RFF] = 1;

    servoSpeed[RFT] = limit_speed((10 * spd_factor));
    servoSweep[RFT][0] = limit_target(RFT, ((servoHome[RFT] - 85) + (move_steps * .85)), 0);
    servoSweep[RFT][1] = limit_target(RFT, ((servoHome[RFT] - 25) + (move_steps * .25)), 0);
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;


    servoSpeed[LRF] = limit_speed((10 * spd_factor));
    servoSweep[LRF][0] = limit_target(LRF, ((servoHome[LRF] - 70) + (move_steps * .7)), 0);
    servoSweep[LRF][1] = limit_target(LRF, ((servoHome[LRF] - 10) + (move_steps * .1)), 0);
    servoSweep[LRF][2] = 0;
    servoSweep[LRF][3] = 1;
    targetPos[LRF] = servoSweep[LRF][1];
    activeSweep[LRF] = 1;

    servoSpeed[LRT] = limit_speed((10 * spd_factor));
    servoSweep[LRT][0] = limit_target(LRT, ((servoHome[LRT] + 85) - (move_steps * .85)), 0); //90
    servoSweep[LRT][1] = limit_target(LRT, ((servoHome[LRT] + 25) - (move_steps * .25)), 0); //30
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;


    servoSpeed[RRF] = limit_speed((10 * spd_factor));
    servoSweep[RRF][0] = limit_target(RRF, ((servoHome[RRF] + 70) - (move_steps * .7)), 0);
    servoSweep[RRF][1] = limit_target(RRF, ((servoHome[RRF] + 10) - (move_steps * .1)), 0);
    servoSweep[RRF][2] = 0;
    servoSweep[RRF][3] = 1;
    targetPos[RRF] = servoSweep[RRF][1];
    activeSweep[RRF] = 1;

    servoSpeed[RRT] = limit_speed((10 * spd_factor));
    servoSweep[RRT][0] = limit_target(RRT, ((servoHome[RRT] - 85) + (move_steps * .85)), 0);
    servoSweep[RRT][1] = limit_target(RRT, ((servoHome[RRT] - 25) + (move_steps * .25)), 0);
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_x_axis = 0;
        start_stop = 0;
      }
    }

    lastMoveDelayUpdate = millis();  
  }
}

void move_kx() {
  int fms = (move_steps_kx * 0.8);
  int tms = (move_steps_kx * 1.3);
  int fsp = limit_speed((24 * spd_factor));
  int tsp = limit_speed((14 * spd_factor));

  update_sequencer(RF, RFF, fsp, (servoHome[RFF] - fms), 0, 0);
  update_sequencer(RF, RFT, tsp, (servoHome[RFT] + tms), 1, 0);
  update_sequencer(LF, LFF, fsp, (servoHome[LFF] + fms), 0, 0);
  update_sequencer(LF, LFT, tsp, (servoHome[LFT] - tms), 1, 0);

  update_sequencer(RR, RRF, fsp, (servoHome[RRF] - fms), 0, 0);
  update_sequencer(RR, RRT, tsp, (servoHome[RRT] + tms), 1, 0);
  update_sequencer(LR, LRF, fsp, (servoHome[LRF] + fms), 0, 0);
  update_sequencer(LR, LRT, tsp, (servoHome[LRT] - tms), 1, 0);

  move_kin_x = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void y_axis() {

  if (!activeSweep[RRT]) {
    servoSpeed[LFC] = limit_speed((96 * spd_factor));
    servoSweep[LFC][0] = (servoHome[LFC]);
    servoSweep[LFC][1] = limit_target(LFC, (servoHome[LFC] + (move_steps * .05)), 0);
    servoSweep[LFC][2] = 0;
    servoSweep[LFC][3] = 1;
    targetPos[LFC] = servoSweep[LFC][1];
    activeSweep[LFC] = 1;

    servoSpeed[LFT] = limit_speed((7 * spd_factor));
    servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] - (move_steps * .65)), 0);
    servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] + (move_steps * .65)), 0);
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    servoSpeed[LFF] = limit_speed((12 * spd_factor));
    servoSweep[LFF][0] = limit_target(LFF, (servoHome[LFF] + (move_steps * .4)), 0);
    servoSweep[LFF][1] = limit_target(LFF, (servoHome[LFF] - (move_steps * .4)), 0);
    servoSweep[LFF][2] = 0;
    servoSweep[LFF][3] = 1;
    targetPos[LFF] = servoSweep[LFF][1];
    activeSweep[LFF] = 1;


    servoSpeed[RFC] = limit_speed((96 * spd_factor));
    servoSweep[RFC][0] = (servoHome[RFC]);
    servoSweep[RFC][1] = limit_target(RFC, (servoHome[RFC] - (move_steps * .05)), 0);
    servoSweep[RFC][2] = 0;
    servoSweep[RFC][3] = 1;
    targetPos[RFC] = servoSweep[RFC][1];
    activeSweep[RFC] = 1;

    servoSpeed[RFT] = limit_speed((7 * spd_factor));
    servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] + (move_steps * .65)), 0);
    servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] - (move_steps * .65)), 0);
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[RFF] = limit_speed((12 * spd_factor));
    servoSweep[RFF][0] = limit_target(RFF, (servoHome[RFF] - (move_steps * .4)), 0);
    servoSweep[RFF][1] = limit_target(RFF, (servoHome[RFF] + (move_steps * .4)), 0);
    servoSweep[RFF][2] = 0;
    servoSweep[RFF][3] = 1;
    targetPos[RFF] = servoSweep[RFF][1];
    activeSweep[RFF] = 1;


    servoSpeed[LRC] = limit_speed((96 * spd_factor));
    servoSweep[LRC][0] = (servoHome[LRC]);
    servoSweep[LRC][1] = limit_target(LRC, (servoHome[LRC] + (move_steps * .05)), 0);
    servoSweep[LRC][2] = 0;
    servoSweep[LRC][3] = 1;
    targetPos[LRC] = servoSweep[LRC][1];
    activeSweep[LRC] = 1;

    servoSpeed[LRT] = limit_speed((7 * spd_factor));
    servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] - (move_steps * .65)), 0);
    servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] + (move_steps * .65)), 0);
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;

    servoSpeed[LRF] = limit_speed((12 * spd_factor));
    servoSweep[LRF][0] = limit_target(LRF, (servoHome[LRF] + (move_steps * .4)), 0);
    servoSweep[LRF][1] = limit_target(LRF, (servoHome[LRF] - (move_steps * .4)), 0);
    servoSweep[LRF][2] = 0;
    servoSweep[LRF][3] = 1;
    targetPos[LRF] = servoSweep[LRF][1];
    activeSweep[LRF] = 1;


    servoSpeed[RRC] = limit_speed((96 * spd_factor));
    servoSweep[RRC][0] = (servoHome[RRC]);
    servoSweep[RRC][1] = limit_target(RRC, (servoHome[RRC] - (move_steps * .05)), 0);
    servoSweep[RRC][2] = 0;
    servoSweep[RRC][3] = 1;
    targetPos[RRC] = servoSweep[RRC][1];
    activeSweep[RRC] = 1;

    servoSpeed[RRT] = limit_speed((7 * spd_factor));
    servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] + (move_steps * .65)), 0);
    servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] - (move_steps * .65)), 0);
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    servoSpeed[RRF] = limit_speed((12 * spd_factor));
    servoSweep[RRF][0] = limit_target(RRF, (servoHome[RRF] - (move_steps * .4)), 0);
    servoSweep[RRF][1] = limit_target(RRF, (servoHome[RRF] + (move_steps * .4)), 0);
    servoSweep[RRF][2] = 0;
    servoSweep[RRF][3] = 1;
    targetPos[RRF] = servoSweep[RRF][1];
    activeSweep[RRF] = 1;

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_y_axis = 0;
        start_stop = 0;
      }
    }
  
    lastMoveDelayUpdate = millis();  
  }
}

void move_ky() {

  int fms = (move_steps_ky * 0.8);
  int tms = (move_steps_ky * 1.3);
  int fsp = limit_speed((24 * spd_factor));
  int tsp = limit_speed((14 * spd_factor));

  update_sequencer(RF, RFF, fsp, (servoHome[RFF] - fms), 0, 0);
  update_sequencer(RF, RFT, tsp, (servoHome[RFT] + tms), 1, 0);
  update_sequencer(LF, LFF, fsp, (servoHome[LFF] + fms), 0, 0);
  update_sequencer(LF, LFT, tsp, (servoHome[LFT] - tms), 1, 0);

  update_sequencer(RR, RRF, fsp, (servoHome[RRF] - fms), 0, 0);
  update_sequencer(RR, RRT, tsp, (servoHome[RRT] + tms), 1, 0);
  update_sequencer(LR, LRF, fsp, (servoHome[LRF] + fms), 0, 0);
  update_sequencer(LR, LRT, tsp, (servoHome[LRT] - tms), 1, 0);

  move_kin_y = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis(); 
}

void roll() {
  if (!activeSweep[RRF]) {
    servoSpeed[LFT] = limit_speed((10 * spd_factor));
    servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] - move_steps), 0);
    servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] + move_steps), 0);
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    servoSpeed[LFF] = limit_speed((16 * spd_factor));
    servoSweep[LFF][0] = limit_target(LFF, (servoHome[LFF] + (move_steps * .667)), 0);
    servoSweep[LFF][1] = limit_target(LFF, (servoHome[LFF] - (move_steps * .667)), 0);
    servoSweep[LFF][2] = 0;
    servoSweep[LFF][3] = 1;
    targetPos[LFF] = servoSweep[LFF][1];
    activeSweep[LFF] = 1;


    servoSpeed[LRT] = limit_speed((10 * spd_factor));
    servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] - move_steps), 0);
    servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] + move_steps), 0);
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;

    servoSpeed[LRF] = limit_speed((16 * spd_factor));
    servoSweep[LRF][0] = limit_target(LRF, (servoHome[LRF] + (move_steps * .667)), 0);
    servoSweep[LRF][1] = limit_target(LRF, (servoHome[LRF] - (move_steps * .667)), 0);
    servoSweep[LRF][2] = 0;
    servoSweep[LRF][3] = 1;
    targetPos[LRF] = servoSweep[LRF][1];
    activeSweep[LRF] = 1;


    servoSpeed[RFT] = limit_speed((10 * spd_factor));
    servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] - move_steps), 0);
    servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] + move_steps), 0);
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[RFF] = limit_speed((16 * spd_factor));
    servoSweep[RFF][0] = limit_target(RFF, (servoHome[RFF] + (move_steps * .667)), 0);
    servoSweep[RFF][1] = limit_target(RFF, (servoHome[RFF] - (move_steps * .667)), 0);
    servoSweep[RFF][2] = 0;
    servoSweep[RFF][3] = 1;
    targetPos[RFF] = servoSweep[RFF][1];
    activeSweep[RFF] = 1;


    servoSpeed[RRT] = limit_speed((10 * spd_factor));
    servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] - move_steps), 0);
    servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] + move_steps), 0);
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    servoSpeed[RRF] = limit_speed((16 * spd_factor));
    servoSweep[RRF][0] = limit_target(RRF, (servoHome[RRF] + (move_steps * .667)), 0);
    servoSweep[RRF][1] = limit_target(RRF, (servoHome[RRF] - (move_steps * .667)), 0);
    servoSweep[RRF][2] = 0;
    servoSweep[RRF][3] = 1;
    targetPos[RRF] = servoSweep[RRF][1];
    activeSweep[RRF] = 1;

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_roll = 0;
        start_stop = 0;
      }
    }

    lastMoveDelayUpdate = millis();  
  }
}

void roll_body() {
  if (!activeSweep[RRC]) {
    servoSpeed[LFC] = limit_speed((10 * spd_factor));
    servoSweep[LFC][0] = limit_target(LFC, (servoHome[LFC] - move_steps), 0);
    servoSweep[LFC][1] = limit_target(LFC, (servoHome[LFC] + move_steps), 0);
    servoSweep[LFC][2] = 0;
    servoSweep[LFC][3] = 1;
    targetPos[LFC] = servoSweep[LFC][1];
    activeSweep[LFC] = 1;

    servoSpeed[LRC] = limit_speed((10 * spd_factor));
    servoSweep[LRC][0] = limit_target(LRC, (servoHome[LRC] - move_steps), 0);
    servoSweep[LRC][1] = limit_target(LRC, (servoHome[LRC] + move_steps), 0);
    servoSweep[LRC][2] = 0;
    servoSweep[LRC][3] = 1;
    targetPos[LRC] = servoSweep[LRC][1];
    activeSweep[LRC] = 1;

    servoSpeed[RFC] = limit_speed((10 * spd_factor));
    servoSweep[RFC][0] = limit_target(RFC, (servoHome[RFC] - move_steps), 0);
    servoSweep[RFC][1] = limit_target(RFC, (servoHome[RFC] + move_steps), 0);
    servoSweep[RFC][2] = 0;
    servoSweep[RFC][3] = 1;
    targetPos[RFC] = servoSweep[RFC][1];
    activeSweep[RFC] = 1;

    servoSpeed[RRC] = limit_speed((10 * spd_factor));
    servoSweep[RRC][0] = limit_target(RRC, (servoHome[RRC] - move_steps), 0);
    servoSweep[RRC][1] = limit_target(RRC, (servoHome[RRC] + move_steps), 0);
    servoSweep[RRC][2] = 0;
    servoSweep[RRC][3] = 1;
    targetPos[RRC] = servoSweep[RRC][1];
    activeSweep[RRC] = 1;

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_roll_body = 0;
        start_stop = 0;
      }
    }

    lastMoveDelayUpdate = millis();  
  }
}

void roll_x() {
  update_sequencer(LF, LFC, spd, (servoHome[LFC] + move_steps_x), 0, 0);
  update_sequencer(LR, LRC, spd, (servoHome[LRC] + move_steps_x), 0, 0);
  update_sequencer(RF, RFC, spd, (servoHome[RFC] + move_steps_x), 0, 0);
  update_sequencer(RR, RRC, spd, (servoHome[RRC] + move_steps_x), 0, 0);

  move_roll_x = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void pitch(int xdir) {
  float sinc0 = .15;
  float sinc1 = 1.15;

  if (xdir < 0) {  //turn left
    xdir = abs(xdir);
    servoStepMoves[RFC][0] = limit_target(RFC, (servoPos[RFC] + (xdir / 4)), 25);
    servoStepMoves[RRC][0] = limit_target(RRC, (servoPos[RRC] + (xdir / 4)), 25);
    servoStepMoves[LFC][0] = 0;
    servoStepMoves[LRC][0] = 0;
  } else if (xdir > 0) {  //turn right
    servoStepMoves[RFC][0] = 0;
    servoStepMoves[RRC][0] = 0;
    servoStepMoves[LFC][0] = limit_target(LFC, (servoPos[LFC] + (xdir / 4)), 25);
    servoStepMoves[LRC][0] = limit_target(LRC, (servoPos[LRC] + (xdir / 4)), 25);
  } else {
    servoStepMoves[RFC][0] = 0;
    servoStepMoves[RRC][0] = 0;
    servoStepMoves[LFC][0] = 0;
    servoStepMoves[LRC][0] = 0;
  }

  if (!activeSweep[RRT]) {
    servoSpeed[LFT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] + (move_steps * sinc0)), 0);
      servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] - (move_steps * sinc1)), 0);
    } else {
      servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] - (move_steps * sinc0)), 0);
      servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] + (move_steps * sinc1)), 0);
    }
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    servoSpeed[RFT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] - (move_steps * sinc0)), 0);
      servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] + (move_steps * sinc1)), 0);
    } else {
      servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] + (move_steps * sinc0)), 0);
      servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] - (move_steps * sinc1)), 0);
    }
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[LRT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] - (move_steps * sinc0)), 0);
      servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] + (move_steps * sinc1)), 0);
    } else {
      servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] + (move_steps * sinc0)), 0);
      servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] - (move_steps * sinc1)), 0);
    }
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;

    servoSpeed[RRT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] + (move_steps * sinc0)), 0);
      servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] - (move_steps * sinc1)), 0);
    } else {
      servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] - (move_steps * sinc0)), 0);
      servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] + (move_steps * sinc1)), 0);
    }
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    update_sequencer(LF, LFC, limit_speed((7 * spd_factor)), servoStepMoves[LFC][0], 0, 0);
    update_sequencer(RF, RFC, limit_speed((7 * spd_factor)), servoStepMoves[RFC][0], 0, 0);
    update_sequencer(LR, LRC, limit_speed((7 * spd_factor)), servoStepMoves[LRC][0], 0, 0);
    update_sequencer(RR, RRC, limit_speed((7 * spd_factor)), servoStepMoves[RRC][0], 0, 0);

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_pitch = 0;
        start_stop = 0;
      }
    }

    lastMoveDelayUpdate = millis();  
  }
}

void pitch_body() {
  if (!activeSweep[RRF]) {
    servoSpeed[LFC] = limit_speed((68 * spd_factor));
    servoSweep[LFC][0] = (servoHome[LFC]);
    servoSweep[LFC][1] = limit_target(LFC, (servoHome[LFC] + (move_steps * .05)), 0);
    servoSweep[LFC][2] = 0;
    servoSweep[LFC][3] = 1;
    targetPos[LFC] = servoSweep[LFC][1];
    activeSweep[LFC] = 1;

    servoSpeed[LFT] = limit_speed((10 * spd_factor));
    servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] - (move_steps * .35)), 0);
    servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] + (move_steps * .35)), 0);
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    servoSpeed[LFF] = limit_speed((17 * spd_factor));
    servoSweep[LFF][0] = limit_target(LFF, (servoHome[LFF] + (move_steps * .2)), 0);
    servoSweep[LFF][1] = limit_target(LFF, (servoHome[LFF] - (move_steps * .2)), 0);
    servoSweep[LFF][2] = 0;
    servoSweep[LFF][3] = 1;
    targetPos[LFF] = servoSweep[LFF][1];
    activeSweep[LFF] = 1;


    servoSpeed[RFC] = limit_speed((68 * spd_factor));
    servoSweep[RFC][0] = (servoHome[RFC]);
    servoSweep[RFC][1] = limit_target(RFC, (servoHome[RFC] - (move_steps * .05)), 0);
    servoSweep[RFC][2] = 0;
    servoSweep[RFC][3] = 1;
    targetPos[RFC] = servoSweep[RFC][1];
    activeSweep[RFC] = 1;

    servoSpeed[RFT] = limit_speed((10 * spd_factor));
    servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] + (move_steps * .35)), 0);
    servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] - (move_steps * .35)), 0);
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[RFF] = limit_speed((17 * spd_factor));
    servoSweep[RFF][0] = limit_target(RFF, (servoHome[RFF] - (move_steps * .2)), 0);
    servoSweep[RFF][1] = limit_target(RFF, (servoHome[RFF] + (move_steps * .2)), 0);
    servoSweep[RFF][2] = 0;
    servoSweep[RFF][3] = 1;
    targetPos[RFF] = servoSweep[RFF][1];
    activeSweep[RFF] = 1;


    servoSpeed[LRC] = limit_speed((68 * spd_factor));
    servoSweep[LRC][0] = limit_target(LRC, (servoHome[LRC] - (move_steps * .05)), 0);
    servoSweep[LRC][1] = (servoHome[LRC]);
    servoSweep[LRC][2] = 0;
    servoSweep[LRC][3] = 1;
    targetPos[LRC] = servoSweep[LRC][1];
    activeSweep[LRC] = 1;

    servoSpeed[LRT] = limit_speed((10 * spd_factor));
    servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] + (move_steps * .35)), 0);
    servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] - (move_steps * .35)), 0);
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;

    servoSpeed[LRF] = limit_speed((17 * spd_factor));
    servoSweep[LRF][0] = limit_target(LRF, (servoHome[LRF] - (move_steps * .2)), 0);
    servoSweep[LRF][1] = limit_target(LRF, (servoHome[LRF] + (move_steps * .2)), 0);
    servoSweep[LRF][2] = 0;
    servoSweep[LRF][3] = 1;
    targetPos[LRF] = servoSweep[LRF][1];
    activeSweep[LRF] = 1;


    servoSpeed[RRC] = limit_speed((68 * spd_factor));
    servoSweep[RRC][0] = limit_target(RRC, (servoHome[RRC] + (move_steps * .05)), 0);
    servoSweep[RRC][1] = (servoHome[RRC]);
    servoSweep[RRC][2] = 0;
    servoSweep[RRC][3] = 1;
    targetPos[RRC] = servoSweep[RRC][1];
    activeSweep[RRC] = 1;

    servoSpeed[RRT] = limit_speed((10 * spd_factor));
    servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] - (move_steps * .35)), 0);
    servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] + (move_steps * .35)), 0);
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    servoSpeed[RRF] = limit_speed((17 * spd_factor));
    servoSweep[RRF][0] = limit_target(RRF, (servoHome[RRF] + (move_steps * .2)), 0);
    servoSweep[RRF][1] = limit_target(RRF, (servoHome[RRF] - (move_steps * .2)), 0);
    servoSweep[RRF][2] = 0;
    servoSweep[RRF][3] = 1;
    targetPos[RRF] = servoSweep[RRF][1];
    activeSweep[RRF] = 1;

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_pitch_body = 0;
        start_stop = 0;
      }
    }
  
    lastMoveDelayUpdate = millis();  
  }
}

void pitch_y() {

  int fms = (move_steps_y * 0.4);
  int tms = (move_steps_y * 0.65);
  int fsp = limit_speed((24 * spd_factor));
  int tsp = limit_speed((14 * spd_factor));

  update_sequencer(RF, RFF, fsp, (servoHome[RFF] - fms), 0, 0);
  update_sequencer(RF, RFT, tsp, (servoHome[RFT] + tms), 1, 0);
  update_sequencer(LF, LFF, fsp, (servoHome[LFF] + fms), 0, 0);
  update_sequencer(LF, LFT, tsp, (servoHome[LFT] - tms), 1, 0);

  update_sequencer(RR, RRF, fsp, (servoHome[RRF] + fms), 0, 0);
  update_sequencer(RR, RRT, tsp, (servoHome[RRT] - tms), 1, 0);
  update_sequencer(LR, LRF, fsp, (servoHome[LRF] - fms), 0, 0);
  update_sequencer(LR, LRT, tsp, (servoHome[LRT] + tms), 1, 0);

  move_pitch_y = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void yaw() {
  int cms = (move_steps_yaw * 0.4);
  int fms = (move_steps_yaw * 0.3);
  int tms = (move_steps_yaw * 0.1);
  int csp = limit_speed((20 * spd_factor));
  int fsp = limit_speed((12 * spd_factor));
  int tsp = limit_speed((15 * spd_factor));

  int lfms = fms;
  int rfms = fms;
  int ltms = tms;
  int rtms = tms;
  if (move_steps_yaw < 0) {
    lfms = (move_steps_yaw * 0.5);
    ltms = (move_steps_yaw * 0.3);
  } else {
    rfms = (move_steps_yaw * 0.5);
    rtms = (move_steps_yaw * 0.3);
  }

  int lfsp = fsp;
  int rfsp = fsp;
  int ltsp = tsp;
  int rtsp = tsp;
  if (move_steps_yaw < 0) {
    lfsp = limit_speed((12 * spd_factor));
    ltsp = limit_speed((15 * spd_factor));
  } else {
    rfsp = limit_speed((12 * spd_factor));
    rtsp = limit_speed((15 * spd_factor));
  }

  update_sequencer(LF, LFC, csp, (servoHome[LFC] + cms), 0, 0);
  update_sequencer(LF, LFF, lfsp, (servoHome[LFF] + fms), 1, 0);
  update_sequencer(LF, LFT, ltsp, (servoHome[LFT] - ltms), 1, 0);

  update_sequencer(RF, RFC, csp, (servoHome[RFC] + cms), 0, 0);
  update_sequencer(RF, RFF, rfsp, (servoHome[RFF] + fms), 1, 0);
  update_sequencer(RF, RFT, rtsp, (servoHome[RFT] - rtms), 1, 0);

  update_sequencer(LR, LRC, csp, (servoHome[LRC] - cms), 0, 0);
  update_sequencer(LR, LRF, lfsp, (servoHome[LRF] - fms), 1, 0);
  update_sequencer(LR, LRT, ltsp, (servoHome[LRT] + ltms), 1, 0);

  update_sequencer(RR, RRC, csp, (servoHome[RRC] - cms), 0, 0);
  update_sequencer(RR, RRF, rfsp, (servoHome[RRF] - fms), 1, 0);
  update_sequencer(RR, RRT, rtsp, (servoHome[RRT] + rtms), 1, 0);

  move_yaw = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void yaw_x() {
  int cms = (move_steps_yaw_x * 0.9);
  int fms = (move_steps_yaw_x * 0.4);
  int tms = (move_steps_yaw_x * 0.7);
  int csp = limit_speed((20 * spd_factor));
  int fsp = limit_speed((32 * spd_factor));
  int tsp = limit_speed((20 * spd_factor));

  update_sequencer(LF, LFC, csp, (servoHome[LFC] + cms), 0, 0);
  update_sequencer(LF, LFF, fsp, (servoHome[LFF] + fms), 1, 0);
  update_sequencer(LF, LFT, tsp, (servoHome[LFT] - tms), 1, 0);

  update_sequencer(LR, LRC, csp, (servoHome[LRC] + cms), 0, 0);
  update_sequencer(LR, LRF, fsp, (servoHome[LRF] + fms), 1, 0);
  update_sequencer(LR, LRT, tsp, (servoHome[LRT] - tms), 1, 0);

  update_sequencer(RF, RFC, csp, (servoHome[RFC] + cms), 0, 0);
  update_sequencer(RF, RFF, fsp, (servoHome[RFF] + fms), 1, 0);
  update_sequencer(RF, RFT, tsp, (servoHome[RFT] - tms), 1, 0);

  update_sequencer(RR, RRC, csp, (servoHome[RRC] + cms), 0, 0);
  update_sequencer(RR, RRF, fsp, (servoHome[RRF] + fms), 1, 0);
  update_sequencer(RR, RRT, tsp, (servoHome[RRT] - tms), 1, 0);

  move_yaw_x = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void yaw_y() {

  int fms = (move_steps_yaw_y * 0.6);
  int tms = (move_steps_yaw_y * 0.2);
  int fsp = limit_speed((25 * spd_factor));
  int tsp = limit_speed((30 * spd_factor));

  int ftms = tms;
  if (move_steps_yaw_y > 0) {
    ftms = (move_steps_yaw_y * 0.6);
  }

  update_sequencer(RF, RFF, fsp, (servoHome[RFF] - fms), 0, 0);
  update_sequencer(RF, RFT, tsp, (servoHome[RFT] - ftms), 1, 0);
  update_sequencer(LF, LFF, fsp, (servoHome[LFF] + fms), 0, 0);
  update_sequencer(LF, LFT, tsp, (servoHome[LFT] + ftms), 1, 0);

  update_sequencer(RR, RRF, fsp, (servoHome[RRF] - fms), 0, 0);
  update_sequencer(RR, RRT, tsp, (servoHome[RRT] - tms), 1, 0);
  update_sequencer(LR, LRF, fsp, (servoHome[LRF] + fms), 0, 0);
  update_sequencer(LR, LRT, tsp, (servoHome[LRT] + tms), 1, 0);

  move_yaw_y = 0;
  start_stop = 0;

  lastMoveDelayUpdate = millis();  
}

void step_trot(int xdir) {
  float sinc0 = .15;
  float sinc1 = 1.15;

  if (xdir < 0) {  //turn left
    xdir = abs(xdir);
    servoStepMoves[RFC][0] = limit_target(RFC, (servoPos[RFC] - (xdir / 4)), 25);
    servoStepMoves[RRC][0] = limit_target(RRC, (servoPos[RRC] - (xdir / 4)), 25);
    servoStepMoves[LFC][0] = limit_target(LFC, (servoPos[LFC] - (xdir / 2)), 50);
    servoStepMoves[LRC][0] = limit_target(LRC, (servoPos[LRC] - (xdir / 2)), 50);
  } else if (xdir > 0) {  //turn right
    servoStepMoves[RFC][0] = limit_target(RFC, (servoPos[RFC] + (xdir / 2)), 50);
    servoStepMoves[RRC][0] = limit_target(RRC, (servoPos[RRC] + (xdir / 2)), 50);
    servoStepMoves[LFC][0] = limit_target(LFC, (servoPos[LFC] + (xdir / 4)), 25);
    servoStepMoves[LRC][0] = limit_target(LRC, (servoPos[LRC] + (xdir / 4)), 25);
  } else {
    servoStepMoves[RFC][0] = 0;
    servoStepMoves[RRC][0] = 0;
    servoStepMoves[LFC][0] = 0;
    servoStepMoves[LRC][0] = 0;
  }

  if (!activeSweep[RRT]) {
    servoSpeed[LFT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] + (move_steps * sinc0)), 0);
      servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] - (move_steps * sinc1)), 0);
    } else {
      servoSweep[LFT][0] = limit_target(LFT, (servoHome[LFT] - (move_steps * sinc0)), 0);
      servoSweep[LFT][1] = limit_target(LFT, (servoHome[LFT] + (move_steps * sinc1)), 0);
    }
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 1;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    servoSpeed[RFT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] - (move_steps * sinc0)), 0);
      servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] + (move_steps * sinc1)), 0);
    } else {
      servoSweep[RFT][0] = limit_target(RFT, (servoHome[RFT] + (move_steps * sinc0)), 0);
      servoSweep[RFT][1] = limit_target(RFT, (servoHome[RFT] - (move_steps * sinc1)), 0);
    }
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 1;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[LRT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] - (move_steps * sinc0)), 0);
      servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] + (move_steps * sinc1)), 0);
    } else {
      servoSweep[LRT][0] = limit_target(LRT, (servoHome[LRT] + (move_steps * sinc0)), 0);
      servoSweep[LRT][1] = limit_target(LRT, (servoHome[LRT] - (move_steps * sinc1)), 0);
    }
    servoSweep[LRT][2] = 0;
    servoSweep[LRT][3] = 1;
    targetPos[LRT] = servoSweep[LRT][1];
    activeSweep[LRT] = 1;

    servoSpeed[RRT] = limit_speed((7 * spd_factor));
    if (move_steps < 0) {
      servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] + (move_steps * sinc0)), 0);
      servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] - (move_steps * sinc1)), 0);
    } else {
      servoSweep[RRT][0] = limit_target(RRT, (servoHome[RRT] - (move_steps * sinc0)), 0);
      servoSweep[RRT][1] = limit_target(RRT, (servoHome[RRT] + (move_steps * sinc1)), 0);
    }
    servoSweep[RRT][2] = 0;
    servoSweep[RRT][3] = 1;
    targetPos[RRT] = servoSweep[RRT][1];
    activeSweep[RRT] = 1;

    update_sequencer(LF, LFC, limit_speed((7 * spd_factor)), servoStepMoves[LFC][0], 0, 0);
    update_sequencer(RF, RFC, limit_speed((7 * spd_factor)), servoStepMoves[RFC][0], 0, 0);
    update_sequencer(LR, LRC, limit_speed((7 * spd_factor)), servoStepMoves[LRC][0], 0, 0);
    update_sequencer(RR, RRC, limit_speed((7 * spd_factor)), servoStepMoves[RRC][0], 0, 0);

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_trot = 0;
        start_stop = 0;
      }
    }

    lastMoveDelayUpdate = millis();  
  }
}

void step_forward(int xdir) {
  int lturn = 0;
  int rturn = 0;
  int lfsf = 0;
  int ltsf = 0;
  int rfsf = 0;
  int rtsf = 0;
  if (xdir < 0) {  //turn left
    lturn = 10;
    rfsf = spd_factor * 0.25;
    rtsf = spd_factor * 0.5;
  } else if (xdir > 0) {  //turn right
    rturn = 10;
    lfsf = spd_factor * 0.25;
    ltsf = spd_factor * 0.5;
  }

  int s1c = 0;
  int s1f = 15;
  int s1t = 35;

  int s2c = 0;
  int s2f = 20 + move_steps;
  int s2t = 15 + move_steps;

  int s3c = 0;
  int s3f = 30 + move_steps;
  int s3t = 10 + move_steps;

  int s4c = 0;
  int s4f = 0;
  int s4t = 0;

  //RF & LR
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(RF, RFC, (0*spd_factor), (servoHome[RFC] + s1c), (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFF, (4*spd_factor), (servoHome[RFF] - s1f), servoSequence[RF], 0);
    update_sequencer(RF, RFT, (3*spd_factor), (servoHome[RFT] + s1t), servoSequence[RF], 0);

    update_sequencer(LR, LRC, (0*spd_factor), (servoHome[LRC] - s1c), (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRF, (4*spd_factor), (servoHome[LRF] + s1f), servoSequence[LR], 0);
    update_sequencer(LR, LRT, (3*spd_factor), (servoHome[LRT] - s1t), servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 1) {
    update_sequencer(RF, RFC, (0*spd_factor), (servoHome[RFC] - s2c), (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFF, (3*(spd_factor-rfsf)), (servoHome[RFF] + s2f - rturn), servoSequence[RF], 0);
    update_sequencer(RF, RFT, (6*spd_factor), (servoHome[RFT] + s2t), servoSequence[RF], 0);

    update_sequencer(LR, LRC, (0*spd_factor), (servoHome[LRC] + s2c), (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRF, (3*spd_factor), (servoHome[LRF] - s2f), servoSequence[LR], 0);
    update_sequencer(LR, LRT, (6*spd_factor), (servoHome[LRT] - s2t), servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    update_sequencer(RF, RFC, (3*spd_factor), (servoHome[RFC] - s3c), (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFF, (3*spd_factor), (servoHome[RFF] + s3f), servoSequence[RF], 0);
    update_sequencer(RF, RFT, (3*spd_factor), (servoHome[RFT] - s3t), servoSequence[RF], 0);

    update_sequencer(LR, LRC, (3*spd_factor), (servoHome[LRC] + s3c), (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRF, (3*spd_factor), (servoHome[LRF] - s3f), servoSequence[LR], 0);
    update_sequencer(LR, LRT, (3*spd_factor), (servoHome[LRT] + s3t), servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3) {
    update_sequencer(RF, RFC, (10*spd_factor), (servoHome[RFC] - s4c), 0, 0);
    update_sequencer(RF, RFF, (10*(spd_factor-rtsf)), (servoHome[RFF] + s4f), 0, 0);
    update_sequencer(RF, RFT, (15*(spd_factor-rtsf)), (servoHome[RFT] - s4t), 0, 0);

    update_sequencer(LR, LRC, (10*spd_factor), (servoHome[LRC] + s4c), 0, 0);
    update_sequencer(LR, LRF, (10*(spd_factor-lfsf)), (servoHome[LRF] - s4f), 0, 0);
    update_sequencer(LR, LRT, (15*(spd_factor-ltsf)), (servoHome[LRT] + s4t), 0, 0);
  }

  //LF & RR
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && !servoSequence[LF] && servoSequence[LR] == 3) {
    update_sequencer(RR, RRC, (0*spd_factor), (servoHome[RRC] + s1c), (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRF, (4*spd_factor), (servoHome[RRF] - s1f), servoSequence[RR], 0);
    update_sequencer(RR, RRT, (3*spd_factor), (servoHome[RRT] + s1t), servoSequence[RR], 0);

    update_sequencer(LF, LFC, (0*spd_factor), (servoHome[LFC] - s1c), (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFF, (4*spd_factor), (servoHome[LFF] + s1f), servoSequence[LF], 0);
    update_sequencer(LF, LFT, (3*spd_factor), (servoHome[LFT] - s1t), servoSequence[LF], 0);
  }
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 1) {
    update_sequencer(RR, RRC, (0*spd_factor), (servoHome[RRC] - s2c), (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRF, (3*(spd_factor-rfsf)), (servoHome[RRF] + s2f - rturn), servoSequence[RR], 0);
    update_sequencer(RR, RRT, (6*spd_factor), (servoHome[RRT] + s2t), servoSequence[RR], 0);

    update_sequencer(LF, LFC, (0*spd_factor), (servoHome[LFC] + s2c), (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFF, (3*spd_factor), (servoHome[LFF] - s2f), servoSequence[LF], 0);
    update_sequencer(LF, LFT, (6*spd_factor), (servoHome[LFT] - s2t), servoSequence[LF], 0);
  }
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 2) {
    update_sequencer(RR, RRC, (3*spd_factor), (servoHome[RRC] - s3c), (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRF, (3*spd_factor), (servoHome[RRF] + s3f), servoSequence[RR], 0);
    update_sequencer(RR, RRT, (3*spd_factor), (servoHome[RRT] - s3t), servoSequence[RR], 0);

    update_sequencer(LF, LFC, (3*spd_factor), (servoHome[LFC] + s3c), (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFF, (3*spd_factor), (servoHome[LFF] - s3f), servoSequence[LF], 0);
    update_sequencer(LF, LFT, (3*spd_factor), (servoHome[LFT] + s3t), servoSequence[LF], 0);
  }
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 3) {
    update_sequencer(RR, RRC, (10*spd_factor), (servoHome[RRC] - s4c), 0, 0);
    update_sequencer(RR, RRF, (10*spd_factor), (servoHome[RRF] + s4f), 0, 0);
    update_sequencer(RR, RRT, (15*spd_factor), (servoHome[RRT] - s4t), 0, 0);

    update_sequencer(LF, LFC, (10*spd_factor), (servoHome[LFC] + s4c), 0, 0);
    update_sequencer(LF, LFF, (10*spd_factor), (servoHome[LFF] - s4f), 0, 0);
    update_sequencer(LF, LFT, (15*spd_factor), (servoHome[LFT] + s4t), 0, 0);

    lastMoveDelayUpdate = millis();  
  }

}

void step_backward(int xdir) {
//this is probably not needed, since backwards is just opposite of forwards
}

void look_left() {
  if (rgb_active) {
    rgb_request("MSNSvFn");
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(LF, LFT, spd, (servoHome[LFT] + move_steps), 1, 0);
    update_sequencer(LF, LFC, spd, (servoHome[LFC] + move_steps), 1, 0);
    update_sequencer(LR, LRT, spd, (servoHome[LRT] + move_steps), 1, 0);
    update_sequencer(LR, LRC, spd, (servoHome[LRC] + move_steps), 1, 0);
    update_sequencer(RR, RRT, spd, (servoHome[RRT] + move_steps), 1, 0);
    update_sequencer(RR, RRC, spd, (servoHome[RRC] + move_steps), 1, 0);
    update_sequencer(RF, RFT, spd, (servoHome[RFT] + move_steps), 1, 0);
    update_sequencer(RF, RFC, spd, (servoHome[RFC] + move_steps), 1, 0);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 1) {
    update_sequencer(RF, RFC, spd, servoHome[RFC], 2, 1000);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    for (int i = 0; i < TOTAL_SERVOS; i++) {
      if (is_left_leg(i) && !is_front_leg(i)) {
        update_sequencer(LR, i, spd, servoHome[i], 3, 0);
      } else if (is_left_leg(i) && is_front_leg(i)) {
        update_sequencer(LF, i, spd, servoHome[i], 3, 0);
      } else if (!is_left_leg(i) && is_front_leg(i)) {
        update_sequencer(RF, i, spd, servoHome[i], 3, 0);
      } else {
        update_sequencer(RR, i, spd, servoHome[i], 3, 0);
      }
    }
  }
  
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3) {
    update_sequencer(RF, RFC, spd, servoHome[RFC], 4, 1000);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 4) {
    for (int i = 0; i < TOTAL_LEGS; i++) {
      servoSequence[i] = 0;
    }
    
    move_look_left = 0;
    move_loops = 4;
    move_switch = 2;
    move_wake = 1;
  }
}

void look_right() {
  if (rgb_active) {
    rgb_request("MSNSvFn");
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(LF, LFT, spd, (servoHome[LFT] - move_steps), 1, 0);
    update_sequencer(LF, LFC, spd, (servoHome[LFC] - move_steps), 1, 0);
    update_sequencer(LR, LRT, spd, (servoHome[LRT] - move_steps), 1, 0);
    update_sequencer(LR, LRC, spd, (servoHome[LRC] - move_steps), 1, 0);
    update_sequencer(RR, RRT, spd, (servoHome[RRT] - move_steps), 1, 0);
    update_sequencer(RR, RRC, spd, (servoHome[RRC] - move_steps), 1, 0);
    update_sequencer(RF, RFT, spd, (servoHome[RFT] - move_steps), 1, 0);
    update_sequencer(RF, RFC, spd, (servoHome[RFC] - move_steps), 1, 0);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 1) {
    update_sequencer(RF, RFC, spd, servoHome[RFC], 2, 1000);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    for (int i = 0; i < TOTAL_SERVOS; i++) {
      if (is_left_leg(i) && !is_front_leg(i)) {
        update_sequencer(LR, i, spd, servoHome[i], 3, 0);
      } else if (is_left_leg(i) && is_front_leg(i)) {
        update_sequencer(LF, i, spd, servoHome[i], 3, 0);
      } else if (!is_left_leg(i) && is_front_leg(i)) {
        update_sequencer(RF, i, spd, servoHome[i], 3, 0);
      } else {
        update_sequencer(RR, i, spd, servoHome[i], 3, 0);
      }
    }
  }
  
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3) {
    update_sequencer(RF, RFC, spd, servoHome[RFC], 4, 1000);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 4) {
    for (int i = 0; i < TOTAL_LEGS; i++) {
      servoSequence[i] = 0;
    }
    
    move_look_right = 0;
    move_loops = 4;
    move_switch = 2;
    move_wake = 1;
  }
}

void step_left_right(int lorr, int xdir, int ydir) {   //where x is +right/-left, and y is +forward/-backward
  spd = 12;  //1-10 (scale with move steps)
  move_steps = 30; //20-110

//scale either move_steps or xdir from the other (leg should lift more when greater xdir, and vice-versa)
//should we push down a bit as first "step" before lifting up?

  if (xdir < 0) xdir = 0;
  if (move_steps < 20) move_steps = 20;

  int rspd_c = limit_speed(spd);
  int rspd_f = limit_speed(spd);
  int rspd_t = limit_speed(spd * 0.5);

  int lspd_c = limit_speed(spd);
  int lspd_f = limit_speed(spd);
  int lspd_t = limit_speed(spd * 0.5);

  servoStepMoves[RFF][0] = limit_target(RFF, (servoHome[RFF] - (0.5 * move_steps)), 10);
  servoStepMoves[RFT][0] = limit_target(RFT, (servoHome[RFT] + (1 * move_steps)), (0.5 * 10));
  servoStepMoves[RRF][0] = limit_target(RRF, (servoHome[RRF] - (0.5 * move_steps)), 10);
  servoStepMoves[RRT][0] = limit_target(RRT, (servoHome[RRT] + (1 * move_steps)), (0.5 * 10));
  servoStepMoves[RFF][2] = servoHome[RFF];
  servoStepMoves[RFT][2] = servoHome[RFT];
  servoStepMoves[RRF][2] = servoHome[RRF];
  servoStepMoves[RRT][2] = servoHome[RRT];

  servoStepMoves[LFF][0] = limit_target(LFF, (servoHome[LFF] + (0.5 * move_steps)), 10);
  servoStepMoves[LFT][0] = limit_target(LFT, (servoHome[LFT] - (1 * move_steps)), (0.5 * 10));
  servoStepMoves[LRF][0] = limit_target(LRF, (servoHome[LRF] + (0.5 * move_steps)), 10);
  servoStepMoves[LRT][0] = limit_target(LRT, (servoHome[LRT] - (1 * move_steps)), (0.5 * 10));
  servoStepMoves[LFF][2] = servoHome[LFF];
  servoStepMoves[LFT][2] = servoHome[LFT];
  servoStepMoves[LRF][2] = servoHome[LRF];
  servoStepMoves[LRT][2] = servoHome[LRT];

  if (lorr == 1) {  //step left
    if (xdir > 1) {
      lspd_f = limit_speed(spd * 1.5);
      lspd_t = limit_speed(spd * 0.75);
      servoStepMoves[LFF][0] = limit_target(LFF, (servoStepMoves[LFF][0] + (0.25 * move_steps)), 10);
      servoStepMoves[LFT][0] = limit_target(LFT, (servoStepMoves[LFT][0] - (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[LRF][0] = limit_target(LRF, (servoStepMoves[LRF][0] + (0.25 * move_steps)), 10);
      servoStepMoves[LRT][0] = limit_target(LRT, (servoStepMoves[LRT][0] - (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[LFF][2] = servoHome[LFF] + (0.125 * move_steps);
      servoStepMoves[LFT][2] = servoHome[LFT] - (0.3625 * move_steps);
      servoStepMoves[LRF][2] = servoHome[LRF] + (0.125 * move_steps);
      servoStepMoves[LRT][2] = servoHome[LRT] - (0.3625 * move_steps);

      servoStepMoves[RFF][0] = limit_target(RFF, (servoStepMoves[RFF][0] + (0.25 * move_steps)), 10);
      servoStepMoves[RFT][0] = limit_target(RFT, (servoStepMoves[RFT][0] - (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[RRF][0] = limit_target(RRF, (servoStepMoves[RRF][0] + (0.25 * move_steps)), 10);
      servoStepMoves[RRT][0] = limit_target(RRT, (servoStepMoves[RRT][0] - (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[RFF][2] = servoHome[RFF] + (0.5 * move_steps);
      servoStepMoves[RFT][2] = servoHome[RFT] - (move_steps);
      servoStepMoves[RRF][2] = servoHome[RRF] + (0.5 * move_steps);
      servoStepMoves[RRT][2] = servoHome[RRT] - (move_steps);
    }
  } else {  //step right
    if (xdir > 1) {
      rspd_f = limit_speed(spd * 1.5);
      rspd_t = limit_speed(spd * 0.75);
      servoStepMoves[RFF][0] = limit_target(RFF, (servoStepMoves[RFF][0] - (0.25 * move_steps)), 10);
      servoStepMoves[RFT][0] = limit_target(RFT, (servoStepMoves[RFT][0] + (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[RRF][0] = limit_target(RRF, (servoStepMoves[RRF][0] - (0.25 * move_steps)), 10);
      servoStepMoves[RRT][0] = limit_target(RRT, (servoStepMoves[RRT][0] + (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[RFF][2] = servoHome[RFF] - (0.125 * move_steps);
      servoStepMoves[RFT][2] = servoHome[RFT] + (0.3625 * move_steps);
      servoStepMoves[RRF][2] = servoHome[RRF] - (0.125 * move_steps);
      servoStepMoves[RRT][2] = servoHome[RRT] + (0.3625 * move_steps);

      servoStepMoves[LFF][0] = limit_target(LFF, (servoStepMoves[LFF][0] - (0.25 * move_steps)), 10);
      servoStepMoves[LFT][0] = limit_target(LFT, (servoStepMoves[LFT][0] + (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[LRF][0] = limit_target(LRF, (servoStepMoves[LRF][0] - (0.25 * move_steps)), 10);
      servoStepMoves[LRT][0] = limit_target(LRT, (servoStepMoves[LRT][0] + (0.5 * move_steps)), (0.5 * 10));
      servoStepMoves[LFF][2] = servoHome[LFF] - (0.5 * move_steps);
      servoStepMoves[LFT][2] = servoHome[LFT] + (move_steps);
      servoStepMoves[LRF][2] = servoHome[LRF] - (0.5 * move_steps);
      servoStepMoves[LRT][2] = servoHome[LRT] + (move_steps);
    }
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(RF, RFC, rspd_c, servoHome[RFC], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, rspd_t, servoStepMoves[RFT][0], servoSequence[RF], 0);
    update_sequencer(RF, RFF, rspd_f, servoStepMoves[RFF][0], servoSequence[RF], 0);
    update_sequencer(RR, RRC, rspd_c, servoHome[RRC], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, rspd_t, servoStepMoves[RRT][0], servoSequence[RR], 0);
    update_sequencer(RR, RRF, rspd_f, servoStepMoves[RRF][0], servoSequence[RR], 0);
  }

  if (!activeServo[RFC] && servoSequence[RF] == 1) {
    if (lorr == 1) {  //step left
      if (xdir < 1) {
        servoStepMoves[RFC][1] = servoHome[RFC];
        servoStepMoves[RRC][1] = servoHome[RRC];
      } else {
        servoStepMoves[RFC][1] = (servoHome[RFC] + xdir);
        servoStepMoves[RRC][1] = (servoHome[RRC] + xdir);
      }
    } else {  //step right
      if (!xdir) {
        servoStepMoves[RFC][1] = servoHome[RFC];
        servoStepMoves[RRC][1] = servoHome[RRC];
      } else {
        servoStepMoves[RFC][1] = (servoHome[RFC] - xdir);
        servoStepMoves[RRC][1] = (servoHome[RRC] - xdir);
      }
    }
    update_sequencer(RF, RFC, (rspd_c), servoStepMoves[RFC][1], (servoSequence[RF] + 1), 0);
    update_sequencer(RR, RRC, (rspd_c), servoStepMoves[RRC][1], (servoSequence[RR] + 1), 0);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    update_sequencer(RF, RFC, (rspd_c/3), servoPos[RFC], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, (rspd_t/3), servoHome[RFT], servoSequence[RF], 0);
    update_sequencer(RF, RFF, (rspd_f/3), servoHome[RFF], servoSequence[RF], 0);
    update_sequencer(RR, RRC, (rspd_c/3), servoPos[RRC], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, (rspd_t/3), servoHome[RRT], servoSequence[RR], 0);
    update_sequencer(RR, RRF, (rspd_f/3), servoHome[RRF], servoSequence[RR], 0);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3) {
    //Serial.println("\treset");
    update_sequencer(RF, RFC, rspd_c, servoHome[RFC], 0, 0);
    update_sequencer(RF, RFT, rspd_t, servoHome[RFT], 0, 0);
    update_sequencer(RF, RFF, rspd_f, servoHome[RFF], 0, 0);
    update_sequencer(RR, RRC, rspd_c, servoHome[RRC], 0, 0);
    update_sequencer(RR, RRT, rspd_t, servoHome[RRT], 0, 0);
    update_sequencer(RR, RRF, rspd_f, servoHome[RRF], 0, 0);
  }

  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && !servoSequence[LF] && servoSequence[RF] == 3) {
    update_sequencer(LF, LFC, lspd_c, servoHome[LFC], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, lspd_t, servoStepMoves[LFT][0], servoSequence[LF], 0);
    update_sequencer(LF, LFF, lspd_f, servoStepMoves[LFF][0], servoSequence[LF], 0);
    update_sequencer(LR, LRC, lspd_c, servoHome[LRC], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, lspd_t, servoStepMoves[LRT][0], servoSequence[LR], 0);
    update_sequencer(LR, LRF, lspd_f, servoStepMoves[LRF][0], servoSequence[LR], 0);  
  }

  if (!activeServo[LFC] && servoSequence[LF] == 1) {
    if (lorr == 1) {  //step left
      if (xdir < 1) {
        servoStepMoves[LFC][1] = servoHome[LFC];
        servoStepMoves[LRC][1] = servoHome[LRC];
      } else {
        servoStepMoves[LFC][1] = (servoHome[LFC] + xdir);
        servoStepMoves[LRC][1] = (servoHome[LRC] + xdir);
      }
    } else {  //step right
      if (xdir < 1) {
        servoStepMoves[LFC][1] = servoHome[LFC];
        servoStepMoves[LRC][1] = servoHome[LRC];
      } else {
        servoStepMoves[LFC][1] = (servoHome[LFC] - xdir);
        servoStepMoves[LRC][1] = (servoHome[LRC] - xdir);
      }
    }
    update_sequencer(LF, LFC, (lspd_c), servoStepMoves[LFC][1], (servoSequence[LF] + 1), 0);
    update_sequencer(LR, LRC, (lspd_c), servoStepMoves[LRC][1], (servoSequence[LR] + 1), 0);
  }

  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 2) {
    update_sequencer(LF, LFC, (lspd_c/3), servoPos[LFC], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, (lspd_t/3), servoHome[LFT], servoSequence[LF], 0);
    update_sequencer(LF, LFF, (lspd_f/3), servoHome[LFF], servoSequence[LF], 0);
    update_sequencer(LR, LRC, (lspd_c/3), servoPos[LRC], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, (lspd_t/3), servoHome[LRT], servoSequence[LR], 0);
    update_sequencer(LR, LRF, (lspd_f/3), servoHome[LRF], servoSequence[LR], 0);
  }
  
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 3) {
    //Serial.println("\treset");
    update_sequencer(LF, LFC, lspd_c, servoHome[LFC], 0, 0);
    update_sequencer(LF, LFT, lspd_t, servoHome[LFT], 0, 0);
    update_sequencer(LF, LFF, lspd_f, servoHome[LFF], 0, 0);
    update_sequencer(LR, LRC, lspd_c, servoHome[LRC], 0, 0);
    update_sequencer(LR, LRT, lspd_t, servoHome[LRT], 0, 0);
    update_sequencer(LR, LRF, lspd_f, servoHome[LRF], 0, 0);

    lastMoveDelayUpdate = millis();  

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_left = 0;
        move_right = 0;
        start_stop = 0;
      }
    }
  
  }
}

void step_march(int xdir, int ydir) {   //where x is +right/-left, and y is +forward/-backward
  int moving = 1;

  spd_c = limit_speed(3 * spd_factor);
  spd_f = limit_speed(3 * spd_factor);
  spd_t = limit_speed(6 * spd_factor);


  if (ydir < 1) {  //move backward
    ydir = abs(ydir);
    spd_f = limit_speed((3 - (ydir / 2)) * spd_factor);
    spd_t = limit_speed((3 - (ydir / 2)) * spd_factor);
  }

  servoStepMoves[RFF][0] = servoHome[RFF] - (20 * step_height_factor);
  servoStepMoves[RFT][0] = servoHome[RFT] + (20 * step_height_factor);
  servoStepMoves[RRF][0] = servoHome[RRF] - (20 * step_height_factor);
  servoStepMoves[RRT][0] = servoHome[RRT] + (20 * step_height_factor);
  servoStepMoves[LFF][0] = servoHome[LFF] + (20 * step_height_factor);
  servoStepMoves[LFT][0] = servoHome[LFT] - (20 * step_height_factor);
  servoStepMoves[LRF][0] = servoHome[LRF] + (20 * step_height_factor);
  servoStepMoves[LRT][0] = servoHome[LRT] - (20 * step_height_factor);
  servoStepMoves[RFC][0] = 0;
  servoStepMoves[RRC][0] = 0;
  servoStepMoves[LFC][0] = 0;
  servoStepMoves[LRC][0] = 0;

  servoStepMoves[RFF][1] = 0;
  servoStepMoves[RRF][1] = 0;
  servoStepMoves[LFF][1] = 0;
  servoStepMoves[LRF][1] = 0;
  
  if (ydir > 1) {  //move forward
    moving = 2;

    servoStepMoves[RFF][1] = servoHome[RFF] + (ydir / 2);
    servoStepMoves[RFT][1] = servoHome[RFT] + (ydir / 6);
    servoStepMoves[RRF][1] = servoHome[RRF] + (ydir / 2);
    servoStepMoves[RRT][1] = servoHome[RRT] + (ydir / 6);
    servoStepMoves[LFF][1] = servoHome[LFF] - (ydir / 2);
    servoStepMoves[LFT][1] = servoHome[LFT] - (ydir / 6);
    servoStepMoves[LRF][1] = servoHome[LRF] - (ydir / 2);
    servoStepMoves[LRT][1] = servoHome[LRT] - (ydir / 6);
  }

  if (xdir < 0) {  //turn left
    spd_f = limit_speed(3 * spd_factor);
    spd_t = limit_speed(2 * spd_factor);

    moving = 1;
    servoStepMoves[LFC][0] = limit_target(LFC, (servoPos[LFC] + (abs(xdir) / 4)), 25);
    servoStepMoves[LFT][0] += (servoStepMoves[LFT][0] * step_height_factor);
    servoStepMoves[LRC][0] = limit_target(LRC, (servoPos[LRC] + (abs(xdir) / 4)), 25);
    servoStepMoves[LRT][0] += (servoStepMoves[LRT][0] * step_height_factor);
  } else if (xdir > 0) {  //turn right
    spd_f = limit_speed(3 * spd_factor);
    spd_t = limit_speed(2 * spd_factor);

    moving = 1;
    servoStepMoves[RFC][0] = limit_target(RFC, (servoPos[RFC] - (abs(xdir) / 4)), 25);
    servoStepMoves[RFT][0] += (servoStepMoves[RFT][0] * step_height_factor);
    servoStepMoves[RRC][0] = limit_target(RRC, (servoPos[RRC] - (abs(xdir) / 4)), 25);
    servoStepMoves[RRT][0] += (servoStepMoves[RRT][0] * step_height_factor);
  }


  //SEQ 1 : RF & LR
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    //Serial.print("1a");
    update_sequencer(RF, RFC, spd_c, servoStepMoves[RFC][0], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFF, spd_f, servoStepMoves[RFF][0], servoSequence[RF], 0);
    update_sequencer(RF, RFT, spd_t, servoStepMoves[RFT][0], servoSequence[RF], 0);

    update_sequencer(LR, LRC, spd_c, servoStepMoves[LRC][0], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRF, spd_f, servoStepMoves[LRF][0], servoSequence[LR], 0);
    update_sequencer(LR, LRT, spd_t, servoStepMoves[LRT][0], servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && servoSequence[RF] == 1) {
    //Serial.print("\t2a");
    if (!servoStepMoves[RFC][0]) {
      update_sequencer(RF, RFC, spd_c, servoHome[RFC], (servoSequence[RF] + 1), 0);
    } else {
      update_sequencer(RF, RFC, spd_c, servoPos[RFC], (servoSequence[RF] + 1), 0);
    }
    if (!servoStepMoves[LRC][0]) {
      update_sequencer(LR, LRC, spd_c, servoHome[LRC], (servoSequence[LR] + 1), 0);
    } else {
      update_sequencer(LR, LRC, spd_c, servoPos[LRC], (servoSequence[LR] + 1), 0);
    }
    update_sequencer(RF, RFF, spd_f, servoHome[RFF], servoSequence[RF], 0);
    update_sequencer(LR, LRF, spd_f, servoHome[LRF], servoSequence[LR], 0);
    if (moving != 2) {
      update_sequencer(RF, RFT, spd_t, servoHome[RFT], servoSequence[RF], 0);
      update_sequencer(LR, LRT, spd_t, servoHome[LRT], servoSequence[LR], 0);
    }
  }

  //SEQ 1 : LF & RR
  if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && !servoSequence[LF] && servoSequence[LR] == 2) {
    //Serial.print("\t1b");
    update_sequencer(LF, LFC, spd_c, servoStepMoves[LFC][0], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFF, spd_f, servoStepMoves[LFF][0], servoSequence[LF], 0);
    update_sequencer(LF, LFT, spd_t, servoStepMoves[LFT][0], servoSequence[LF], 0);

    update_sequencer(RR, RRC, spd_c, servoStepMoves[RRC][0], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRF, spd_f, servoStepMoves[RRF][0], servoSequence[RR], 0);
    update_sequencer(RR, RRT, spd_t, servoStepMoves[RRT][0], servoSequence[RR], 0);
  }
  if (!activeServo[LFC] && !activeServo[LFF] && servoSequence[LF] == 1) {
    //Serial.print("\t2b");
    if (!servoStepMoves[LFC][0]) {
      update_sequencer(LF, LFC, spd_c, servoHome[LFC], (servoSequence[LF] + 1), 0);
    } else {
      update_sequencer(LF, LFC, spd_c, servoPos[LFC], (servoSequence[LF] + 1), 0);
    }
    if (!servoStepMoves[RRC][0]) {
      update_sequencer(RR, RRC, spd_c, servoHome[RRC], (servoSequence[RR] + 1), 0);
    } else {
      update_sequencer(RR, RRC, spd_c, servoPos[RRC], (servoSequence[RR] + 1), 0);
    }
    update_sequencer(LF, LFF, spd_f, servoHome[LFF], servoSequence[LF], 0);
    update_sequencer(RR, RRF, spd_f, servoHome[RRF], servoSequence[RR], 0);
    if (moving != 2) {
      update_sequencer(LF, LFT, spd_t, servoHome[LFT], servoSequence[LF], 0);
      update_sequencer(RR, RRT, spd_t, servoHome[RRT], servoSequence[RR], 0);
    }
  }

  //SEQ 2 : RF & LR
  if (moving == 2) {
    if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
      //Serial.print("\t3a RF: ");Serial.print(servoSequence[RF]);
      update_sequencer(RF, RFC, spd_c, servoStepMoves[RFC][1], (servoSequence[RF] + 1), 0);
      update_sequencer(RF, RFF, spd_f, servoStepMoves[RFF][1], servoSequence[RF], 0);
      update_sequencer(RF, RFT, spd_t, servoStepMoves[RFT][1], servoSequence[RF], 0);

      update_sequencer(LR, LRC, spd_c, servoStepMoves[LRC][1], (servoSequence[LR] + 1), 0);
      update_sequencer(LR, LRF, spd_f, servoStepMoves[LRF][1], servoSequence[LR], 0);
      update_sequencer(LR, LRT, spd_t, servoStepMoves[LRT][1], servoSequence[LR], 0);
    }
    if (!activeServo[RFC] && !activeServo[RFF] && servoSequence[RF] == 3) {
      //Serial.print("\t4a RF: ");Serial.print(servoSequence[RF]);
      update_sequencer(RF, RFC, spd_c, servoHome[RFC], (servoSequence[RF] + 1), 0);
      update_sequencer(RF, RFT, spd_t, servoHome[RFT], servoSequence[RF], 0);
      update_sequencer(LR, LRC, spd_c, servoHome[LRC], (servoSequence[LR] + 1), 0);
      update_sequencer(LR, LRT, spd_t, servoHome[LRT], servoSequence[LR], 0);
    }
    if (!activeServo[RFT] && !activeServo[LRT] && servoSequence[RF] == 4) {
      //Serial.print("\t4a2");
      update_sequencer(RF, RFF, spd_f, servoHome[RFF], (servoSequence[RF] + 1), 0);
      update_sequencer(LR, LRF, spd_f, servoHome[LRF], (servoSequence[LR] + 1), 0);
    }

    //SEQ 2 : LF & RR
    if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 2 && servoSequence[LR] == 5) {
      //Serial.print("\t3b");
      update_sequencer(LF, LFC, spd_c, servoStepMoves[LFC][1], (servoSequence[LF] + 1), 0);
      update_sequencer(LF, LFF, spd_f, servoStepMoves[LFF][1], servoSequence[LF], 0);
      update_sequencer(LF, LFT, spd_t, servoStepMoves[LFT][1], servoSequence[LF], 0);

      update_sequencer(RR, RRC, spd_c, servoStepMoves[RRC][1], (servoSequence[RR] + 1), 0);
      update_sequencer(RR, RRF, spd_f, servoStepMoves[RRF][1], servoSequence[RR], 0);
      update_sequencer(RR, RRT, spd_t, servoStepMoves[RRT][1], servoSequence[RR], 0);
    }
    if (!activeServo[LFC] && !activeServo[LFF] && servoSequence[LF] == 3) {
      //Serial.print("\t4b");
      update_sequencer(LF, LFC, spd_c, servoHome[LFC], (servoSequence[LF] + 1), 0);
      update_sequencer(LF, LFT, spd_t, servoHome[LFT], servoSequence[LF], 0);
      update_sequencer(RR, RRC, spd_c, servoHome[RRC], (servoSequence[RR] + 1), 0);
      update_sequencer(RR, RRT, spd_t, servoHome[RRT], servoSequence[RR], 0);
    }
    if (!activeServo[LFT] && !activeServo[RRT] && servoSequence[LF] == 4) {
      //Serial.print("\t4b2");
      update_sequencer(LF, LFF, spd_f, servoHome[LFF], (servoSequence[LF] + 1), 0);
      update_sequencer(RR, RRF, spd_f, servoHome[RRF], (servoSequence[RR] + 1), 0);
    }
  }


  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && ((moving == 2 && servoSequence[RF] == 5 && servoSequence[LF] == 5 && servoSequence[RR] == 5 && servoSequence[LR] == 5) || (moving != 2 && servoSequence[RR] == 2))) {
    //Serial.println("\treset");
    for (int i = 0; i < TOTAL_LEGS; i++) {
      servoSequence[i] = 0;
    }

    lastMoveDelayUpdate = millis();  

    if (move_loops) {
      move_loops--;
      if (!move_loops) {
        move_march = 0;
        start_stop = 0;
      }
    }
  }
}

void wake() {
  int del_cnt = 0;

  servoStepMoves[RFF][0] = 15;
  servoStepMoves[LRF][0] = 15;
  servoStepMoves[LFF][0] = 15;
  servoStepMoves[RRF][0] = 15;
  servoStepMoves[RFT][0] = 20;
  servoStepMoves[LRT][0] = 20;
  servoStepMoves[LFT][0] = 20;
  servoStepMoves[RRT][0] = 20;

  if (move_loops) {
    //RF
    if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
      if (move_loops) {
        if (!move_switch) {
          if (move_loops == 1) {
            move_loops = 10;
            move_switch = 1;
          }
          servoStepMoves[RFC][0] = 0;
          servoStepMoves[LRC][0] = 0;
          servoStepMoves[LFC][0] = 0;
          servoStepMoves[RRC][0] = 0;
        } else if (move_switch == 2) {
          servoStepMoves[RFC][0] = 0;
          servoStepMoves[LRC][0] = 0;
          servoStepMoves[LFC][0] = 0;
          servoStepMoves[RRC][0] = 0;
        }
      }

      update_sequencer(RF, RFC, 1, (servoPos[RFC] - servoStepMoves[RFC][0]), (servoSequence[RF] + 1), 0);
      update_sequencer(RF, RFF, 1, (servoHome[RFF] - servoStepMoves[RFF][0]), servoSequence[RF], 0);
      update_sequencer(RF, RFT, 1, (servoHome[RFT] + servoStepMoves[RFT][0]), servoSequence[RF], 0);
    }
    if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 1) {
      update_sequencer(RF, RFC, 1, servoPos[RFC], (servoSequence[RF] + 1), 0);
      update_sequencer(RF, RFF, 1, servoHome[RFF], servoSequence[RF], 0);
      update_sequencer(RF, RFT, 1, servoHome[RFT], servoSequence[RF], 0);
    }

    //LR
    if (!activeServo[LRC] && !activeServo[LRF] && !activeServo[LRT] && !servoSequence[LR] && !activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
      update_sequencer(LR, LRC, 1, (servoPos[LRC] + servoStepMoves[LRC][0]), (servoSequence[LR] + 1), 0);
      update_sequencer(LR, LRF, 1, (servoHome[LRF] + servoStepMoves[LRF][0]), servoSequence[LR], 0);
      update_sequencer(LR, LRT, 1, ((servoHome[LRT] - servoStepMoves[LRT][0]) - (servoStepMoves[LRT][0]*step_weight_factor)), servoSequence[LR], 0);
    }
    if (!activeServo[LRC] && !activeServo[LRF] && !activeServo[LRT] && servoSequence[LR] == 1) {
      update_sequencer(LR, LRC, 1, servoPos[LRC], (servoSequence[LR] + 1), 0);
      update_sequencer(LR, LRF, 1, servoHome[LRF], servoSequence[LR], 0);
      update_sequencer(LR, LRT, 1, servoHome[LRT], servoSequence[LR], 0);
    }

    //LF
    if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && !servoSequence[LF] && !activeServo[LRC] && !activeServo[LRF] && !activeServo[LRT] && servoSequence[LR] == 2) {
      update_sequencer(LF, LFC, 1, (servoPos[LFC] + servoStepMoves[LFC][0]), (servoSequence[LF] + 1), 0);
      update_sequencer(LF, LFF, 1, (servoHome[LFF] + servoStepMoves[LFF][0]), servoSequence[LF], 0);
      update_sequencer(LF, LFT, 1, (servoHome[LFT] - servoStepMoves[LFT][0]), servoSequence[LF], 0);
    }
    if (!activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 1) {
      update_sequencer(LF, LFC, 1, servoPos[LFC], (servoSequence[LF] + 1), 0);
      update_sequencer(LF, LFF, 1, servoHome[LFF], servoSequence[LF], 0);
      update_sequencer(LF, LFT, 1, servoHome[LFT], servoSequence[LF], 0);
    }

    //RR
    if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && !servoSequence[RR] && !activeServo[LFC] && !activeServo[LFF] && !activeServo[LFT] && servoSequence[LF] == 2) {
      update_sequencer(RR, RRC, 1, (servoPos[RRC] - servoStepMoves[RRC][0]), (servoSequence[RR] + 1), 0);
      update_sequencer(RR, RRF, 1, (servoHome[RRF] - servoStepMoves[RRF][0]), servoSequence[RR], 0);
      update_sequencer(RR, RRT, 1, ((servoHome[RRT] + servoStepMoves[RRT][0]) + (servoStepMoves[RRT][0]*step_weight_factor)), servoSequence[RR], 0);
    }
    if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 1) {
      update_sequencer(RR, RRC, 1, servoPos[RRC], (servoSequence[RR] + 1), 0);
      update_sequencer(RR, RRF, 1, servoHome[RRF], servoSequence[RR], 0);
      update_sequencer(RR, RRT, 1, servoHome[RRT], servoSequence[RR], 0);


      for (int i = 0; i < TOTAL_LEGS; i++) {
        servoSequence[i] = 0;
      }

      move_loops--;
    }

    lastMoveDelayUpdate = millis();  
  } else if (move_switch == 1) {
    move_loops = 9;
    move_switch = 2;
  } else if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && !servoSequence[RR]) {
    spd = default_spd;
    set_speed();
    start_stop = 0;
    move_wake = 0;
  }
}

void wman() {

  servoStepMoves[RFF][0] = servoHome[RFF] - 17;
  servoStepMoves[RFT][0] = servoHome[RFT] + 77;
  servoStepMoves[RFF][1] = servoHome[RFF] - 32;
  servoStepMoves[RFT][1] = servoHome[RFT] + 77;
  servoStepMoves[RFF][2] = servoHome[RFF] + 44;
  servoStepMoves[RFT][2] = servoHome[RFT] - 14;
  servoStepMoves[RFF][3] = servoHome[RFF] - 12;
  servoStepMoves[RFT][3] = servoHome[RFT] - 4;
  servoStepMoves[RFF][4] = servoHome[RFF] - 24;
  servoStepMoves[RFT][4] = servoHome[RFT] + 5;
  servoStepMoves[RFF][5] = servoHome[RFF] - 16;
  servoStepMoves[RFT][5] = servoHome[RFT] - 8;

  servoStepMoves[LRF][0] = servoHome[LRF] + 17;
  servoStepMoves[LRT][0] = servoHome[LRT] - 77;
  servoStepMoves[LRF][1] = servoHome[LRF] + 32;
  servoStepMoves[LRT][1] = servoHome[LRT] - 77;
  servoStepMoves[LRF][2] = servoHome[LRF] - 44;
  servoStepMoves[LRT][2] = servoHome[LRT] + 14;
  servoStepMoves[LRF][3] = servoHome[LRF] + 12;
  servoStepMoves[LRT][3] = servoHome[LRT] + 4;
  servoStepMoves[LRF][4] = servoHome[LRF] + 24;
  servoStepMoves[LRT][4] = servoHome[LRT] - 5;
  servoStepMoves[LRF][5] = servoHome[LRF] + 16;
  servoStepMoves[LRT][5] = servoHome[LRT] + 8;


  //SEQ 1 : RF & LR
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(RF, RFF, 5*spd_factor, servoStepMoves[RFF][0], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, 1*spd_factor, servoStepMoves[RFT][0], servoSequence[RF], 0);

    update_sequencer(LR, LRF, 5*spd_factor, servoStepMoves[LRF][0], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, 1*spd_factor, servoStepMoves[LRT][0], servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 1) {
    update_sequencer(RF, RFF, 7, servoStepMoves[RFF][1], (servoSequence[RF] + 1), 5);
    update_sequencer(RF, RFT, 7, servoStepMoves[RFT][1], servoSequence[RF], 5);

    update_sequencer(LR, LRF, 7, servoStepMoves[LRF][1], (servoSequence[LR] + 1), 5);
    update_sequencer(LR, LRT, 7, servoStepMoves[LRT][1], servoSequence[LR], 5);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    use_ramp = 0;
    update_sequencer(RF, RFF, 1, servoStepMoves[RFF][2], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, 0.5, servoStepMoves[RFT][2], servoSequence[RF], 0);

    update_sequencer(LR, LRF, 1, servoStepMoves[LRF][2], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, 0.5, servoStepMoves[LRT][2], servoSequence[LR], 0);
    use_ramp = 1;
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3) {
    update_sequencer(RF, RFF, 14*(0.55*spd_factor), servoStepMoves[RFF][3], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, 44*(0.55*spd_factor), servoStepMoves[RFT][3], servoSequence[RF], 0);

    update_sequencer(LR, LRF, 14*(0.55*spd_factor), servoStepMoves[LRF][3], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, 44*(0.55*spd_factor), servoStepMoves[LRT][3], servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 4) {
    update_sequencer(RF, RFF, 2*spd_factor, servoStepMoves[RFF][4], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, 1*spd_factor, servoStepMoves[RFT][4], servoSequence[RF], 0);

    update_sequencer(LR, LRF, 2*spd_factor, servoStepMoves[LRF][4], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, 1*spd_factor, servoStepMoves[LRT][4], servoSequence[LR], 0);
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 5) {
    use_ramp = 0;
    update_sequencer(RF, RFF, 3, servoStepMoves[RFF][5], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFT, 1.5, servoStepMoves[RFT][5], servoSequence[RF], 0);

    update_sequencer(LR, LRF, 3, servoStepMoves[LRF][5], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRT, 1.5, servoStepMoves[LRT][5], servoSequence[LR], 0);
    use_ramp = 1;
  }
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 6) {
    update_sequencer(RF, RFF, 3*spd_factor, servoHome[RFF], 0, 10);
    update_sequencer(RF, RFT, 3*spd_factor, servoHome[RFT], 0, 10);
    update_sequencer(LR, LRF, 3*spd_factor, servoHome[LRF], 0, 10);
    update_sequencer(LR, LRT, 3*spd_factor, servoHome[LRT], 0, 10);
  }


  servoStepMoves[RRF][0] = servoHome[RRF] - 17;
  servoStepMoves[RRT][0] = servoHome[RRT] + 77;
  servoStepMoves[RRF][1] = servoHome[RRF] - 32;
  servoStepMoves[RRT][1] = servoHome[RRT] + 77;
  servoStepMoves[RRF][2] = servoHome[RRF] + 44;
  servoStepMoves[RRT][2] = servoHome[RRT] - 14;
  servoStepMoves[RRF][3] = servoHome[RRF] - 12;
  servoStepMoves[RRT][3] = servoHome[RRT] - 4;
  servoStepMoves[RRF][4] = servoHome[RRF] - 24;
  servoStepMoves[RRT][4] = servoHome[RRT] + 5;
  servoStepMoves[RRF][5] = servoHome[RRF] - 16;
  servoStepMoves[RRT][5] = servoHome[RRT] - 8;

  servoStepMoves[LFF][0] = servoHome[LFF] + 17;
  servoStepMoves[LFT][0] = servoHome[LFT] - 77;
  servoStepMoves[LFF][1] = servoHome[LFF] + 32;
  servoStepMoves[LFT][1] = servoHome[LFT] - 77;
  servoStepMoves[LFF][2] = servoHome[LFF] - 44;
  servoStepMoves[LFT][2] = servoHome[LFT] + 14;
  servoStepMoves[LFF][3] = servoHome[LFF] + 12;
  servoStepMoves[LFT][3] = servoHome[LFT] + 4;
  servoStepMoves[LFF][4] = servoHome[LFF] + 24;
  servoStepMoves[LFT][4] = servoHome[LFT] - 5;
  servoStepMoves[LFF][5] = servoHome[LFF] + 16;
  servoStepMoves[LFT][5] = servoHome[LFT] + 8;


  //SEQ 1 : RR & LF
  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && !servoSequence[RR] && servoSequence[RF] == 4) {
    update_sequencer(RR, RRF, 5*spd_factor, servoStepMoves[RRF][0], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, 1*spd_factor, servoStepMoves[RRT][0], servoSequence[RR], 0);

    update_sequencer(LF, LFF, 5*spd_factor, servoStepMoves[LFF][0], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, 1*spd_factor, servoStepMoves[LFT][0], servoSequence[LF], 0);
  }
  if (!activeServo[RFC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 1) {
    update_sequencer(RR, RRF, 7, servoStepMoves[RRF][1], (servoSequence[RR] + 1), 5);
    update_sequencer(RR, RRT, 7, servoStepMoves[RRT][1], servoSequence[RR], 5);

    update_sequencer(LF, LFF, 7, servoStepMoves[LFF][1], (servoSequence[LF] + 1), 5);
    update_sequencer(LF, LFT, 7, servoStepMoves[LFT][1], servoSequence[LF], 5);
  }
  if (!activeServo[RFC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 2) {
    use_ramp = 0;
    update_sequencer(RR, RRF, 1, servoStepMoves[RRF][2], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, 0.5, servoStepMoves[RRT][2], servoSequence[RR], 0);

    update_sequencer(LF, LFF, 1, servoStepMoves[LFF][2], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, 0.5, servoStepMoves[LFT][2], servoSequence[LF], 0);
    use_ramp = 1;
  }
  if (!activeServo[RFC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 3) {
    update_sequencer(RR, RRF, 14*(0.55*spd_factor), servoStepMoves[RRF][3], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, 44*(0.55*spd_factor), servoStepMoves[RRT][3], servoSequence[RR], 0);

    update_sequencer(LF, LFF, 14*(0.55*spd_factor), servoStepMoves[LFF][3], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, 44*(0.55*spd_factor), servoStepMoves[LFT][3], servoSequence[LF], 0);
  }
  if (!activeServo[RFC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 4) {
    update_sequencer(RR, RRF, 2*spd_factor, servoStepMoves[RRF][4], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, 1*spd_factor, servoStepMoves[RRT][4], servoSequence[RR], 0);

    update_sequencer(LF, LFF, 2*spd_factor, servoStepMoves[LFF][4], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, 1*spd_factor, servoStepMoves[LFT][4], servoSequence[LF], 0);
  }
  if (!activeServo[RFC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 5) {
    use_ramp = 0;
    update_sequencer(RR, RRF, 3, servoStepMoves[RRF][5], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRT, 1.5, servoStepMoves[RRT][5], servoSequence[RR], 0);

    update_sequencer(LF, LFF, 3, servoStepMoves[LFF][5], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFT, 1.5, servoStepMoves[LFT][5], servoSequence[LF], 0);
    use_ramp = 1;
  }
  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 6) {
    update_sequencer(RR, RRF, 3*spd_factor, servoHome[RRF], 0, 10);
    update_sequencer(RR, RRT, 3*spd_factor, servoHome[RRT], 0, 10);
    update_sequencer(LF, LFF, 3*spd_factor, servoHome[LFF], 0, 10);
    update_sequencer(LF, LFT, 3*spd_factor, servoHome[LFT], 0, 10);

    lastMoveDelayUpdate = millis();  
  }
}

void funplay() {
  //recover from sitting up
  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && !servoSequence[RR]) {
    update_sequencer(RR, RRT, 8, servoLimit[RRT][1]-80, 1, 0);
    update_sequencer(LR, LRT, 8, servoLimit[LRT][1]+80, 1, 0);
  }
  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 1) {
    update_sequencer(RR, RRF, 6, servoPos[RRF]+30, 2, 300);
    update_sequencer(LR, LRF, 6, servoPos[LRF]-30, 2, 300);
  }
  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 2) {
///repeat/open step!
    update_sequencer(RR, RRT, 8, servoLimit[RRT][1]-80, 3, 0);
    update_sequencer(LR, LRT, 8, servoLimit[LRT][1]+80, 3, 0);
  }

  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 3) {
    update_sequencer(RR, RRT, 12, servoLimit[RRT][0], 4, 50);
    update_sequencer(LR, LRT, 12, servoLimit[LRT][0], 4, 50);

    update_sequencer(RF, RFF, 24, servoHome[RFF], 1, 50);
    update_sequencer(LF, LFF, 24, servoHome[LFF], 1, 50);
    update_sequencer(RF, RFT, 24, servoLimit[RFT][1], 1, 50);
    update_sequencer(LF, LFT, 24, servoLimit[LFT][1], 1, 50);
  }

  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 4) {
    servoSpeed[RFF] = 3;
    servoSweep[RFF][0] = limit_target(RFF, (servoHome[RFF] + 40), 0);
    servoSweep[RFF][1] = limit_target(RFF, (servoHome[RFF] + 80), 20);
    servoSweep[RFF][2] = 0;
    servoSweep[RFF][3] = 9;
    targetPos[RFF] = servoSweep[RFF][1];
    activeSweep[RFF] = 1;

    servoSpeed[LFF] = 3;
    servoSweep[LFF][0] = limit_target(LFF, (servoHome[LFF] - 80), 20);
    servoSweep[LFF][1] = limit_target(LFF, (servoHome[LFF] - 40), 0);
    servoSweep[LFF][2] = 0;
    servoSweep[LFF][3] = 9;
    targetPos[LFF] = servoSweep[LFF][1];
    activeSweep[LFF] = 1;

    servoSpeed[RFT] = 5;
    servoSweep[RFT][0] = servoLimit[RFT][1];
    servoSweep[RFT][1] = servoHome[RFT];
    servoSweep[RFT][2] = 0;
    servoSweep[RFT][3] = 4;
    targetPos[RFT] = servoSweep[RFT][1];
    activeSweep[RFT] = 1;

    servoSpeed[LFT] = 5;
    servoSweep[LFT][0] = servoHome[LFT];
    servoSweep[LFT][1] = servoLimit[LFT][1];
    servoSweep[LFT][2] = 0;
    servoSweep[LFT][3] = 4;
    targetPos[LFT] = servoSweep[LFT][1];
    activeSweep[LFT] = 1;

    update_sequencer(RR, RRC, 3, servoPos[RRC], 5, 0);
  }

  if (!activeServo[RRC] && !activeServo[RRF] && !activeServo[RRT] && servoSequence[RR] == 5 && !activeSweep[RFT] && !activeSweep[LFT]) {
    update_sequencer(RF, RFT, 12, servoHome[RFT], 2, 50);
    update_sequencer(LF, LFT, 12, servoHome[LFT], 2, 50);
    update_sequencer(RR, RRT, 12, servoHome[RRT], 6, 100);
    update_sequencer(LR, LRT, 12, servoHome[LRT], 6, 100);
    update_sequencer(RR, RRF, 12, servoPos[RRF]-10, 6, 0);
    update_sequencer(LR, LRF, 12, servoPos[LRF]+10, 6, 0);

    update_sequencer(RF, RFF, 6, servoLimit[RFF][1], 2, 200);
    update_sequencer(LF, LFF, 6, servoLimit[LFF][1], 2, 200);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 2) {
    update_sequencer(RF, RFC, 12, servoHome[RFC], 3, 0);
    update_sequencer(RR, RRF, 12, servoPos[RRF]+20, 7, 0);
    update_sequencer(LR, LRF, 12, servoPos[LRF]-20, 7, 0);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 3 && !activeServo[RRF] && !activeServo[LRF]) {
    update_sequencer(RR, RRT, 6, servoLimit[RRT][1], 8, 50);
    update_sequencer(LR, LRT, 6, servoLimit[LRT][1], 8, 50);
    update_sequencer(RF, RFF, 12, servoHome[RFF]+30, 4, 100);
    update_sequencer(LF, LFF, 12, servoHome[LFF]-30, 4, 100);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 4 && !activeServo[RRF] && !activeServo[LRF]) {
    update_sequencer(RF, RFC, 12, servoHome[RFC], 5, 0);
    update_sequencer(RR, RRT, 6, servoLimit[RRT][1], 9, 50);
    update_sequencer(LR, LRT, 6, servoLimit[LRT][1], 9, 50);
    update_sequencer(RR, RRF, 12, servoHome[RRF]-30, 9, 100);
    update_sequencer(LR, LRF, 12, servoHome[LRF]+30, 9, 100);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 5 && !activeServo[RRF] && !activeServo[LRF]) {
    update_sequencer(RR, RRT, 6, servoHome[RRT], 10, 0);
    update_sequencer(LR, LRT, 6, servoHome[LRT], 10, 0);
    update_sequencer(RF, RFT, 6, servoHome[RFT], 6, 30);
    update_sequencer(LF, LFT, 6, servoHome[LFT], 6, 30);
    update_sequencer(RR, RRF, 6, servoHome[RRF], 9, 100);
    update_sequencer(LR, LRF, 6, servoHome[LRF], 9, 100);
    update_sequencer(RF, RFF, 6, servoHome[RFF], 6, 130);
    update_sequencer(LF, LFF, 6, servoHome[LFF], 6, 130);
  }

  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && servoSequence[RF] == 6) {
    lastMoveDelayUpdate = millis();  

    if (rgb_active) {
      rgb_request("Gm");
    }
    if (buzz_active) {
      play_phrases();
    }
    move_funplay = 0;
    set_stop_active();
    delay(3000);
  }

}




void move_debug_servo() {
  if (!activeSweep[debug_servo]) {
    servoSpeed[debug_servo] = debug_spd;
    servoSweep[debug_servo][0] = servoLimit[debug_servo][0];
    servoSweep[debug_servo][1] = servoLimit[debug_servo][1];
    servoSweep[debug_servo][2] = 0;
    servoSweep[debug_servo][3] = 1;
    targetPos[debug_servo] = servoSweep[debug_servo][1];
    activeSweep[debug_servo] = 1;

    if (debug_loops2) {
      debug_loops2--;
    }
    if (!debug_loops2) {
      set_stop_active();
      set_home();
    } 
  }
}


void move_debug_leg() {
  //check if leg active
  int lactive = 0;
  for (int i = 0; i < 3; i++) {
    int dservo = servoLeg[debug_leg][i];
    if (activeSweep[dservo]) {
      lactive = 1;
    }
  }

  if (!lactive) {
    for (int i = 0; i < 3; i++) {
      int dservo = servoLeg[debug_leg][i];
      if (!activeSweep[dservo]) {
        servoSpeed[dservo] = debug_spd;
        if (is_front_leg(dservo)) {
          servoSweep[dservo][0] = servoLimit[dservo][1];
          servoSweep[dservo][1] = servoLimit[dservo][0];
          if (is_femur(dservo)) {
            servoSweep[dservo][0] = servoLimit[dservo][0];
            servoSweep[dservo][1] = servoLimit[dservo][1];
          }
        } else {
          servoSweep[dservo][0] = servoLimit[dservo][0];
          servoSweep[dservo][1] = servoLimit[dservo][1];
          if (is_femur(dservo)) {
            servoSweep[dservo][0] = servoLimit[dservo][1];
            servoSweep[dservo][1] = servoLimit[dservo][0];
          }
        }
        servoSweep[dservo][2] = 0;
        servoSweep[dservo][3] = 1;
        targetPos[dservo] = servoSweep[dservo][1];
        activeSweep[dservo] = 1;
      }
    }

    if (debug_loops2) {
      debug_loops2--;
    } 
    if (!debug_loops2) {
      set_stop_active();
      set_home();
    } 
  }
}


/*
   -------------------------------------------------------
   Sequence Processing Functions
   -------------------------------------------------------
*/
void run_sequence() {

  //SEQ 1
  if (!activeServo[RFC] && !activeServo[RFF] && !activeServo[RFT] && !servoSequence[RF]) {
    update_sequencer(RF, RFC, spd_c, servoStepMoves[RFC][0], (servoSequence[RF] + 1), 0);
    update_sequencer(RF, RFF, spd_f, servoStepMoves[RFF][0], servoSequence[RF], 0);
    update_sequencer(RF, RFT, spd_t, servoStepMoves[RFT][0], servoSequence[RF], 0);

    update_sequencer(LF, LFC, spd_c, servoStepMoves[LFC][0], (servoSequence[LF] + 1), 0);
    update_sequencer(LF, LFF, spd_f, servoStepMoves[LFF][0], servoSequence[LF], 0);
    update_sequencer(LF, LFT, spd_t, servoStepMoves[LFT][0], servoSequence[LF], 0);

    update_sequencer(RR, RRC, spd_c, servoStepMoves[RRC][0], (servoSequence[RR] + 1), 0);
    update_sequencer(RR, RRF, spd_f, servoStepMoves[RRF][0], servoSequence[RR], 0);
    update_sequencer(RR, RRT, spd_t, servoStepMoves[RRT][0], servoSequence[RR], 0);

    update_sequencer(LR, LRC, spd_c, servoStepMoves[LRC][0], (servoSequence[LR] + 1), 0);
    update_sequencer(LR, LRF, spd_f, servoStepMoves[LRF][0], servoSequence[LR], 0);
    update_sequencer(LR, LRT, spd_t, servoStepMoves[LRT][0], servoSequence[LR], 0);
  }

  //SEQ 2
  if (!activeServo[LRC] && !activeServo[LRF] && !activeServo[LRT] && servoSequence[LR] == 1) {
    update_sequencer(RF, RFC, spd_c, servoStepMoves[RFC][1], (servoSequence[RF] + 1), move_delay);
    update_sequencer(RF, RFF, spd_f, servoStepMoves[RFF][1], servoSequence[RF], move_delay);
    update_sequencer(RF, RFT, spd_t, servoStepMoves[RFT][1], servoSequence[RF], move_delay);
  
    update_sequencer(LF, LFC, spd_c, servoStepMoves[LFC][1], (servoSequence[LF] + 1), move_delay);
    update_sequencer(LF, LFF, spd_f, servoStepMoves[LFF][1], servoSequence[LF], move_delay);
    update_sequencer(LF, LFT, spd_t, servoStepMoves[LFT][1], servoSequence[LF], move_delay);
  
    update_sequencer(RR, RRC, spd_c, servoStepMoves[RRC][1], (servoSequence[RR] + 1), move_delay);
    update_sequencer(RR, RRF, spd_f, servoStepMoves[RRF][1], servoSequence[RR], move_delay);
    update_sequencer(RR, RRT, spd_t, servoStepMoves[RRT][1], servoSequence[RR], move_delay);
  
    update_sequencer(LR, LRC, spd_c, servoStepMoves[LRC][1], (servoSequence[LR] + 1), move_delay);
    update_sequencer(LR, LRF, spd_f, servoStepMoves[LRF][1], servoSequence[LR], move_delay);
    update_sequencer(LR, LRT, spd_t, servoStepMoves[LRT][1], servoSequence[LR], move_delay);
  }

  //SEQ 3
  if (!activeServo[LRC] && !activeServo[LRF] && !activeServo[LRT] && servoSequence[LR] == 2) {
    update_sequencer(RF, RFC, spd_c, servoStepMoves[RFC][2], (servoSequence[RF] + 1), (move_delay*2));
    update_sequencer(RF, RFF, spd_f, servoStepMoves[RFF][2], servoSequence[RF], (move_delay*2));
    update_sequencer(RF, RFT, spd_t, servoStepMoves[RFT][2], servoSequence[RF], (move_delay*2));
  
    update_sequencer(LF, LFC, spd_c, servoStepMoves[LFC][2], (servoSequence[LF] + 1), (move_delay*2));
    update_sequencer(LF, LFF, spd_f, servoStepMoves[LFF][2], servoSequence[LF], (move_delay*2));
    update_sequencer(LF, LFT, spd_t, servoStepMoves[LFT][2], servoSequence[LF], (move_delay*2));
  
    update_sequencer(RR, RRC, spd_c, servoStepMoves[RRC][2], (servoSequence[RR] + 1), (move_delay*2));
    update_sequencer(RR, RRF, spd_f, servoStepMoves[RRF][2], servoSequence[RR], (move_delay*2));
    update_sequencer(RR, RRT, spd_t, servoStepMoves[RRT][2], servoSequence[RR], (move_delay*2));
  
    update_sequencer(LR, LRC, spd_c, servoStepMoves[LRC][2], (servoSequence[LR] + 1), (move_delay*2));
    update_sequencer(LR, LRF, spd_f, servoStepMoves[LRF][2], servoSequence[LR], (move_delay*2));
    update_sequencer(LR, LRT, spd_t, servoStepMoves[LRT][2], servoSequence[LR], (move_delay*2));
  }

  if (is_stepmove_complete(3) && servoSequence[LR] == 3) {
    if (move_loops) {
      move_loops--;
      for (int l = 0; l < TOTAL_LEGS; l++) {
        servoSequence[0] = 0;
        servoSequence[l] = 0;
        servoSequence[2] = 0;
        servoSequence[3] = 0;
      }
    } else {
      move_sequence = 0;
    }
  }
}

void delay_sequences() {
  int moved = 0;
  int sequence_cnt = 16;

  if (!start_stop) {
    for (int i = 0; i < sequence_cnt; i++) {
      if (move_delay_sequences[i]) {
        moved = 1;
        if (move_delay_sequences[i] == 1) {
          spd = 12;
          set_speed();
          start_stop = 1;
          move_loops = 6;
          move_steps = 20;
          move_x_axis = 1;
          if (debug1)
            Serial.print("move x");
        } else if (move_delay_sequences[i] == 2) {
          set_home();
          spd = 12;
          set_speed();
          start_stop = 1;
          move_loops = 3;
          move_steps = 70;
          move_y_axis = 1;
          if (debug1)
            Serial.print("move y large");
        } else if (move_delay_sequences[i] == 13) {
          spd = 1;
          set_speed();
          start_stop = 1;
          move_loops = 10;
          move_steps = 15;
          move_y_axis = 1;
          if (debug1)
            Serial.print("move y short");
        } else if (move_delay_sequences[i] == 3) {
          start_stop = 1;
          spd = 9;
          set_speed();
          move_loops = 10;
          move_steps = 25;
          move_pitch_body = 1;
          if (debug1)
            Serial.print("move pitch_body");
        } else if (move_delay_sequences[i] == 4) {
          use_ramp = 0;
          start_stop = 1;
          spd = 9;
          set_speed();
          move_loops = 10;
          move_steps = 25;
          move_pitch = 1;
          if (debug1)
            Serial.print("move pitch");
        } else if (move_delay_sequences[i] == 5) {
          use_ramp = 1;
          start_stop = 1;
          spd = 9;
          set_speed();
          move_loops = 6;
          move_steps = 20;
          move_roll_body = 1;
          if (debug1)
            Serial.print("move rollb");
        } else if (move_delay_sequences[i] == 6) {
          set_home();
          start_stop = 1;
          spd = 5;
          set_speed();
          move_loops = 6;
          move_steps = 30;
          move_roll = 1;
          if (debug1)
            Serial.print("move roll");
        } else if (move_delay_sequences[i] == 7) {
          set_home();
          spd = 1;
          set_speed();
          move_loops = 2;
          move_switch = 2;
          for (int i = 0; i < TOTAL_SERVOS; i++) {
            servoPos[i] = servoHome[i];
          }
          start_stop = 1;
          move_wake = 1;
          if (debug1)
            Serial.print("move wake");
        } else if (move_delay_sequences[i] == 8) {
          start_stop = 0;
          set_crouch();
          if (debug1)
            Serial.print("crouch");
        } else if (move_delay_sequences[i] == 9) {
          start_stop = 0;
          set_sit();
          if (debug1)
            Serial.print("sit");
        } else if (move_delay_sequences[i] == 10) {
          start_stop = 1;
          move_loops = 1;
          move_steps = 0;
          move_x_axis = 1;
          if (debug1)
            Serial.print("move x 1");
        } else if (move_delay_sequences[i] == 11) {
          set_home();
          start_stop = 1;
          y_dir = 0;
          x_dir = 0;
          move_loops = 16;
          move_march = 1;
          if (debug1)
            Serial.print("move march");
        } else if (move_delay_sequences[i] == 12) {
          set_home();
          set_kneel();
          if (debug1)
            Serial.print("move kneel");
        } else if (move_delay_sequences[i] == 14) {
          spd = 1;
          set_speed();
          start_stop = 1;
          move_loops = 1;
          move_steps = 30;
          move_look_left = 1;
          if (debug1)
            Serial.print("move look_left");
        } else if (move_delay_sequences[i] == 15) {
          spd = 1;
          set_speed();
          start_stop = 1;
          move_loops = 1;
          move_steps = 30;
          move_look_right = 1;
          if (debug1)
            Serial.print("move look_right");
        } else if (move_delay_sequences[i] == 16) {
          start_stop = 0;
          set_stay();
          if (debug1)
            Serial.print("stay");
        }
  
        moveDelayInterval = move_delays[i];
        if (debug1) {
          Serial.print("\ti: ");Serial.print(i);Serial.print("\tmove int: ");Serial.println(moveDelayInterval);
        }
        move_delay_sequences[i] = 0;
        i = 16;
      }
    }

    if (!moved) {
      move_demo = 0;
      moveDelayInterval = 0;

      use_ramp = 0;
      spd = 5;
      set_speed();

      servoSweep[RFC][0] = servoHome[RFC];
      servoSweep[RFC][1] = servoLimit[RFC][0];
      servoSweep[RFC][2] = 0;
      servoSweep[RFC][3] = 7;
      targetPos[RFC] = servoSweep[RFC][1];
      activeSweep[RFC] = 1;

      servoSweep[LFC][0] = servoHome[LFC];
      servoSweep[LFC][1] = servoLimit[LFC][0];
      servoSweep[LFC][2] = 0;
      servoSweep[LFC][3] = 7;
      targetPos[LFC] = servoSweep[LFC][1];
      activeSweep[LFC] = 1;

      servoSweep[RFT][0] = servoHome[RFT];
      servoSweep[RFT][1] = servoLimit[RFT][0];
      servoSweep[RFT][2] = 0;
      servoSweep[RFT][3] = 3;
      targetPos[RFT] = servoSweep[RFT][1];
      activeSweep[RFT] = 1;

      servoSweep[LFT][0] = servoHome[LFT];
      servoSweep[LFT][1] = servoLimit[LFT][0];
      servoSweep[LFT][2] = 0;
      servoSweep[LFT][3] = 3;
      targetPos[LFT] = servoSweep[LFT][1];
      activeSweep[LFT] = 1;

//      move_funplay = 1;
      if (debug1)
//        Serial.print("move funplay");

      if (debug1) {
        Serial.println(F("\treset DS"));
      }
    }  
  }
}

void update_sequencer(int leg, int servo, int sp, float tar, int seq, int del) {
  if (debug3) {
    if (tar) {
      Serial.print("leg: "); Serial.print(leg);
      Serial.print("\tservo: "); Serial.print(servo);
      Serial.print("\tdel: "); Serial.print(del);
      Serial.print("\tpos: "); Serial.print(servoPos[servo]);
      Serial.print("\ttar: "); Serial.print(tar);
      Serial.print("\tseq: "); Serial.println(servoSequence[leg]);
    } else {
      Serial.print(leg); Serial.println(F("-END"));
    }
  }
  servoSpeed[servo] = limit_speed(sp);
  servoSequence[leg] = seq;
  if (tar) {
    servoDelay[servo][0] = del;
    if (del > 0) servoDelay[servo][1] = 1;
    targetPos[servo] = limit_target(servo, tar, 0);
    activeServo[servo] = 1;
  }

  if (use_ramp) {
    set_ramp(servo, servoSpeed[servo], 0, 0, 0, 0);
  }
}



/*
   -------------------------------------------------------
   General Functions
   -------------------------------------------------------
*/
int limit_target(int sid, int tar, int thresh) {
  if (servoLimit[sid][0] > servoLimit[sid][1]) {
    if ((tar + thresh) > servoLimit[sid][0]) {
      tar = (servoLimit[sid][0] - thresh);
    } else if ((tar - thresh) < servoLimit[sid][1]) {
      tar = (servoLimit[sid][1] + thresh);
    }
  } else {
    if ((tar - thresh) < servoLimit[sid][0]) {
      tar = (servoLimit[sid][0] + thresh);
    } else if ((tar + thresh) > servoLimit[sid][1]) {
      tar = (servoLimit[sid][1] - thresh);
    }
  }

  return tar;
}

int limit_speed(float spd_lim) {
  if (spd_lim > min_spd) spd_lim = min_spd;
  if (spd_lim < max_spd) spd_lim = max_spd;

  return spd_lim;
}

byte is_stepmove_complete(int ms) {
  byte ret = 1;
  for (int m = 0; m < TOTAL_SERVOS; m++) {
    if (servoPos[m] == servoStepMoves[m][ms-1]) ret = 0;  
  }

  return ret;
}

byte is_front_leg(int leg) {
  if (leg == LFC || leg == LFF || leg == LFT || leg == RFC || leg == RFF || leg == RFT) 
    return 1;
  else 
    return 0;
}

byte is_left_leg(int leg) {
  if (leg == LFC || leg == LFF || leg == LFT || leg == LRC || leg == LRF || leg == LRT)
    return 1;
  else
    return 0;
}

byte is_femur(int leg) {
  if (leg == RFF || leg == LFF || leg == RRF || leg == LRF)
    return 1;
  else
    return 0;
}

byte is_tibia(int leg) {
  if (leg == RFT || leg == LFT || leg == RRT || leg == LRT)
    return 1;
  else
    return 0;
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int degrees_to_pwm(int pangle, int mxw, int mnw, int rng) {
  int pulse_wide, analog_value;
  pulse_wide = map(pangle, -rng/2, rng/2, mnw, mxw);

  return pulse_wide;
}

int pwm_to_degrees(int pulse_wide, int mxw, int mnw, int rng) {
  int pangle, analog_value;
  pangle = map(pulse_wide, mnw, mxw, -rng/2, rng/2);

  return pangle;
}




/*
   -------------------------------------------------------
   Serial Commands
   -------------------------------------------------------
*/
void serial_check() {
  if (serial_active && Serial.available() > 0) {
    ByteReceived = Serial.read();
    if (debug4) {
      Serial.print(ByteReceived);Serial.print("\t");
      Serial.print(ByteReceived, HEX);Serial.print("\t");
      Serial.println(char(ByteReceived));
    }

    switch (ByteReceived) {      
      case '0':
        if (!plotter) Serial.println(F("stop!"));
        set_stop_active();
        set_home();
        break;    
      case 91:
        if (!plotter) Serial.println(F("move_steps -5"));
        if (move_steps > move_steps_min) {
          move_steps -= 2;
        }
        break;
      case 93:
        if (!plotter) Serial.println(F("move_steps +5"));
        if (move_steps < move_steps_max) {
          move_steps += 2;
        }
        break;
      case 59:
        if (!plotter) Serial.println(F("y_dir -5"));
        if (y_dir > move_y_steps[0]) {
          y_dir -= 2;
        }
        if (!plotter) { Serial.print("-y_dir ");Serial.println(y_dir); }
        break;
      case 39:
        if (!plotter) Serial.println(F("y_dir +5"));
        if (y_dir < move_y_steps[1]) {
          y_dir += 2;
        }
        if (!plotter) { Serial.print("+y_dir ");Serial.println(y_dir); }
        break;
      case 46:
        if (!plotter) Serial.println(F("x_dir -5"));
        if (x_dir > move_x_steps[0]) {
          x_dir -= 2;
        }
        if (!plotter) { Serial.print("-x_dir ");Serial.println(x_dir); }
        break;
      case 47:
        if (x_dir < move_x_steps[1]) {
          x_dir += 2;
        }
        if (!plotter) { Serial.print("+x_dir ");Serial.println(x_dir); }
        break;
      case 45:
        if (debug_servo > 0) {
          debug_servo--;
        }
        if (!plotter) { Serial.print("set debug servo ");Serial.println(debug_servo); }
        break;
      case 61:
        if (debug_servo < (TOTAL_SERVOS-1)) {
          debug_servo++;
        }
        if (!plotter) { Serial.print("set debug servo ");Serial.println(debug_servo); }
        break;
      case 92:
        if (debug_leg < (TOTAL_LEGS-1)) {
          debug_leg++;
        } else {
          debug_leg = 0;
        }
        if (!plotter) { Serial.print("set debug leg ");Serial.println(debug_leg); }
        break;
      case '1':
        if (!plotter) Serial.println(F("set speed 1"));
        spd = 1;
        set_speed();
        break;
      case '2':
        if (!plotter) Serial.println(F("set speed 5"));
        spd = 5;
        set_speed();
        break;
      case '3':
        if (!plotter) Serial.println(F("set speed 15"));
        spd = 15;
        set_speed();
        break;
      case '4':
        if (!plotter) Serial.println(F("set speed 30"));
        spd = 30;
        set_speed();
        break;
      case 'z':
        if (!plotter) Serial.print("set mpu ");
        if (mpu_active) {
          mpu_active = 0;
          if (!plotter) Serial.println(F("off"));
        } else {
          mpu_active = 1;
          if (!plotter) Serial.println(F("on"));
        }
        break;
      case 'o':
        if (oled_active) {
          if (!plotter) Serial.println(F("test OLED"));
          oled_request("a");
          delay(3000);
          oled_request("b");
          delay(3000);
          oled_request("c");
          delay(3000);
          oled_request("d");
          delay(3000);
          oled_request("e");
          delay(3000);
          oled_request("f");
          delay(3000);
          oled_request("g");
          delay(3000);
          oled_request("h");
          delay(3000);
          oled_request("i");
        } else {
          if (!plotter) Serial.println(F("OLED inactive"));
        }
        break;
      case 'v':
        if (!plotter) Serial.println(F("test rgb"));
        rgb_request("voF");
        delay(6000);
        rgb_request("D");
        delay(3000);
        rgb_request("MONRHyn");
        break;
      case 'd':
        if (!plotter) Serial.println(F("demo"));
        run_demo();
        break;
      case 't':
        if (!plotter) Serial.println(F("trot"));
        move_steps = 30;
        x_dir = 0;
        move_trot = 1;
        break;
      case 'm':
        if (!plotter) Serial.println(F("march"));
        spd = 32;
        set_speed();
        y_dir = 0;
        x_dir = 0;
        step_height_factor = 1.25;
        move_march = 1;
        break;
      case 'f':
        if (!plotter) Serial.println(F("march_forward"));
        spd = 32;
        set_speed();
        y_dir = 10;
        x_dir = 0;
        step_height_factor = 1.25;
        move_march = 1;
        break;
      case 'b':
        if (!plotter) Serial.println(F("march_backward"));
        spd = 32;
        set_speed();
        y_dir = -10;
        x_dir = 0;
        step_height_factor = 1.25;
        move_march = 1;
        break;
      case 's':
        if (!plotter) Serial.println(F("stay"));
        set_stay();
        break;
      case 'i':
        if (!plotter) Serial.println(F("sit"));
        set_sit();
        break;
      case 'k':
        if (!plotter) Serial.println(F("kneel"));
        set_kneel();
        break;
      case 'c':
        if (!plotter) Serial.println(F("crouch"));
        set_crouch();
        break;
      case 'l':
        if (!plotter) Serial.println(F("lay"));
        set_lay();
        break;
      case 'r':
        if (!plotter) Serial.println(F("roll"));
        move_steps = 30;
        x_dir = 0;
        move_roll = 1;
        break;
      case 'p':
        if (!plotter) Serial.println(F("pitch"));
        move_steps = 30;
        x_dir = 0;
        move_pitch = 1;
        break;
      case 'q':
        if (!plotter) Serial.println(F("roll_body"));
        move_steps = 30;
        x_dir = 0;
        move_roll_body = 1;
        break;
      case 'n':
        if (!plotter) Serial.println(F("pitch_body"));
        move_steps = 30;
        x_dir = 0;
        move_pitch_body = 1;
        break;
      case 'w':
        if (!plotter) Serial.println(F("wman"));
        spd = 3;
        set_speed();
        move_wman = 1;
        break;
      case 'y':
        if (!plotter) Serial.println(F("y_axis"));
        move_y_axis = 1;
        y_axis();
        break;
      case 'x':
        if (!plotter) Serial.println(F("x_axis"));
        move_x_axis = 1;
        x_axis();
        break;
      case 'u':
        if (!plotter) Serial.println(F("look_left"));
        spd = 1;
        start_stop = 1;
        move_loops = 1;
        move_steps = 50;
        move_look_left = 1;
        break;
      case 'j':
        if (!plotter) Serial.println(F("look_right"));
        spd = 1;
        start_stop = 1;
        move_loops = 1;
        move_steps = 50;
        move_look_right = 1;
        break;
      case 'a':
        if (!plotter) Serial.println(F("wake"));
        if (!activeServo[RFF] && !activeServo[LFF] && !activeServo[RRF] && !activeServo[LRF]) {
          spd = 1;
          set_speed();
          move_loops = 2;
          move_switch = 2;
          for (int i = 0; i < TOTAL_SERVOS; i++) {
            servoPos[i] = servoHome[i];
          }
          start_stop = 1;
          move_wake = 1;
        }
        break;
      case '5':
        if (!plotter) Serial.println(F("sweep tibias"));
        use_ramp = 0;
        for (int i = 0; i < TOTAL_SERVOS; i++) {
          if (is_tibia(i)) {
            servoSweep[i][0] = servoLimit[i][0];
            servoSweep[i][1] = servoLimit[i][1];
            servoSweep[i][2] = 0;
            servoSweep[i][3] = 1;
            targetPos[i] = servoSweep[i][1];
            activeSweep[i] = 1;
          }
        }
        break;
      case '6':
        if (!plotter) Serial.println(F("ramp sweep tibia"));
        use_ramp = 1;
        for (int i = 0; i < TOTAL_SERVOS; i++) {
          if (is_tibia(i)) {
            servoSweep[i][0] = servoLimit[i][0];
            servoSweep[i][1] = servoLimit[i][1];
            servoSweep[i][2] = 0;
            servoSweep[i][3] = 1;
            targetPos[i] = servoSweep[i][1];
            activeSweep[i] = 1;
            set_ramp(i, servoSpeed[i], 0, 0, 0, 0);
          }
        }
        break;
      case '7':
        if (!plotter) Serial.println(F("sweep femurs"));
        use_ramp = 0;
        for (int i = 0; i < TOTAL_SERVOS; i++) {
          if (is_femur(i)) {
            servoSweep[i][0] = servoLimit[i][0];
            servoSweep[i][1] = servoLimit[i][1];
            servoSweep[i][2] = 0;
            servoSweep[i][3] = 1;
            targetPos[i] = servoSweep[i][1];
            activeSweep[i] = 1;
          }
        }
        break;
      case '8':
        if (!plotter) Serial.println(F("ramp sweep femur"));
        use_ramp = 1;
        for (int i = 0; i < TOTAL_SERVOS; i++) {
          if (is_femur(i)) {
            servoSweep[i][0] = servoLimit[i][0];
            servoSweep[i][1] = servoLimit[i][1];
            servoSweep[i][2] = 0;
            servoSweep[i][3] = 1;
            targetPos[i] = servoSweep[i][1];
            activeSweep[i] = 1;
            set_ramp(i, servoSpeed[i], 0, 0, 0, 0);
          }
        }
        break;
      case '9':
        if (!plotter) Serial.println(F("sweep coaxes"));
        use_ramp = 0;
        for (int i = 0; i < TOTAL_SERVOS; i++) {
          if (!is_femur(i) && !is_tibia(i)) {
            servoSweep[i][0] = servoLimit[i][0];
            servoSweep[i][1] = servoLimit[i][1];
            servoSweep[i][2] = 0;
            servoSweep[i][3] = 1;
            targetPos[i] = servoSweep[i][1];
            activeSweep[i] = 1;
          }
        }
        break;
      case 'g':
        if (!plotter) { Serial.print("debug servo ");Serial.println(debug_servo); }
        debug_loops2 = debug_loops;
        move_servo = 1;
        break;
      case 'e':
        if (!plotter) { Serial.print("debug leg ");Serial.println(debug_leg); }
        debug_loops2 = debug_loops;
        move_leg = 1;
        break;
      case 'h':
        Serial.println();
        Serial.println(F("\t-----------------------------------------"));
        Serial.println(F("\t|\tKEY\tCOMMAND\t\t\t|"));
        Serial.println(F("\t-----------------------------------------"));
        Serial.println(F("\t|\t0\tstop!\t\t\t|"));
        Serial.println(F("\t|\t1\tset speed 1\t\t|"));
        Serial.println(F("\t|\t2\tset speed 5\t\t|"));
        Serial.println(F("\t|\t3\tset speed 15\t\t|"));
        Serial.println(F("\t|\t4\tset speed 30\t\t|"));
        Serial.println();
        Serial.println(F("\t|\t5\tsweep tibias\t\t|"));
        Serial.println(F("\t|\t6\tramp sweep tibias\t|"));
        Serial.println(F("\t|\t7\tsweep femurs\t\t|"));
        Serial.println(F("\t|\t8\tramp sweep femurs\t|"));
        Serial.println(F("\t|\t9\tsweep coaxes\t\t|"));
        Serial.println();
        Serial.println(F("\t|\tt\ttrot\t\t\t|"));
        Serial.println(F("\t|\tm\tmarch\t\t\t|"));
        Serial.println(F("\t|\tf\tmarch_forward\t\t|"));
        Serial.println(F("\t|\tb\tmarch_backward\t\t|"));
        Serial.println();
        Serial.println(F("\t|\ts\tstay\t\t\t|"));
        Serial.println(F("\t|\ti\tsit\t\t\t|"));
        Serial.println(F("\t|\tk\tkneel\t\t\t|"));
        Serial.println(F("\t|\tc\tcrouch\t\t\t|"));
        Serial.println(F("\t|\tl\tlay\t\t\t|"));
        Serial.println();
        Serial.println(F("\t|\tr\troll\t\t\t|"));
        Serial.println(F("\t|\tp\tpitch\t\t\t|"));
        Serial.println(F("\t|\tq\troll_body\t\t|"));
        Serial.println(F("\t|\tn\tpitch_body\t\t|"));
        Serial.println();
        Serial.println(F("\t|\ty\ty_axis\t\t\t|"));
        Serial.println(F("\t|\tx\tx_axis\t\t\t|"));
        Serial.println(F("\t|\ta\twake\t\t\t|"));
        Serial.println();
        Serial.println(F("\t|\tw\twman\t\t\t|"));
        Serial.println(F("\t|\tu\tlook_left\t\t|"));
        Serial.println(F("\t|\tj\tlook_right\t\t|"));
        Serial.println(F("\t|\td\tdemo\t\t\t|"));
        Serial.println();
        Serial.println(F("\t|\t[\tmove_steps -2\t\t|"));
        Serial.println(F("\t|\t]\tmove_steps +2\t\t|"));
        Serial.println(F("\t|\t;\ty_dir -2\t\t|"));
        Serial.println(F("\t|\t'\ty_dir +2\t\t|"));
        Serial.println(F("\t|\t.\tx_dir -2\t\t|"));
        Serial.println(F("\t|\t/\tx_dir +2\t\t|"));
        Serial.println();
        Serial.println(F("\t|\tz\tmpu active\t\t|"));
        Serial.println(F("\t|\to\ttest OLED\t\t|"));
        Serial.println(F("\t|\tv\ttest RGB\t\t|"));
        Serial.print(F("\t|\tg\tdebug servo "));Serial.print(debug_servo);Serial.println(F("\t\t|"));
        Serial.println(F("\t|\t-\tprev debug_servo\t|"));
        Serial.println(F("\t|\t=\tnext debug_servo\t|"));
        Serial.print(F("\t|\te\tdebug leg "));Serial.print(debug_leg);Serial.println(F("\t\t|"));
        Serial.println(F("\t|\t\\\tnext debug_leg\t\t|"));
        Serial.println(F("\t|\th\thelp\t\t\t|"));
        Serial.println(F("\t-----------------------------------------"));
        Serial.println();
        Serial.println(F("Type a command code or 'h' for help:"));
        break;
    }
  }
}





/*
   -------------------------------------------------------
   Serial Commands
   -------------------------------------------------------
*/
void slave_test() {
  if (serial_active && Serial.available() > 0) {
    ByteReceived = Serial.read();
    if (debug6) {
      Serial.print(ByteReceived);Serial.print("\t");
      Serial.print(ByteReceived, HEX);Serial.print("\t");
      Serial.println(char(ByteReceived));
    }

        if (ByteReceived != 10) {
          Wire.beginTransmission((uint8_t)SLAVE_ID);
          Wire.write(char(ByteReceived));
          Wire.endTransmission();
          Serial.print("command: ");
          Serial.print(char(ByteReceived));
          Serial.print(" (");Serial.print(ByteReceived);Serial.print(")");
    
          Wire.beginTransmission((uint8_t)SLAVE_ID);
          int available = Wire.requestFrom((uint8_t)SLAVE_ID, (uint8_t)2);
      
          if (available == 2) {
            int receivedValue = Wire.read() << 8 | Wire.read(); 
            Serial.print("\tresponse: ");
            Serial.print(receivedValue);
          }
          Serial.println();
          Wire.endTransmission();
        }
  }
}


/*
   -------------------------------------------------------
   Serial Communication Functions
   -------------------------------------------------------
*/

void rgb_request(char* commands) {
  if (serial_oled) {
    serial_oled = command_slave("X");
  }
  serial_resp = command_slave(commands);
}

void oled_request(char* commands) {
  if (!serial_oled) {
    serial_oled = command_slave("X");
  }
  serial_resp = command_slave(commands);
  serial_oled = command_slave("X");
}

int command_slave(char* commands) {
  int command_response = 0;

  if(commands && slave_active) {
    int i = 0;
    while(commands[i]) {
      if (debug6 && (commands[i] != 'Z')) {
        (serial_oled) ? Serial.print(F("OLED Command: ")) : Serial.print(F("System Command: "));
      }

      Wire.beginTransmission((uint8_t)SLAVE_ID);
      Wire.write(char(commands[i]));
      Wire.endTransmission();
      if (debug6 && (commands[i] != 'Z')) {
        Serial.print(commands[i]);
      }
    
      Wire.beginTransmission((uint8_t)SLAVE_ID);
      int available = Wire.requestFrom((uint8_t)SLAVE_ID, (uint8_t)2);
            
      if (available == 2) {
        command_response = Wire.read() << 8 | Wire.read(); 
        if (debug6 && (commands[i] != 'Z')) {
          Serial.print("\tresponse: ");
          Serial.print(command_response);
        }
      }
      Wire.endTransmission();
      if (debug6 && (commands[i] != 'Z')) {
        Serial.println();
      }

      //if resetting slave, pause 15secs for display graphics
      if (commands[i] == 'Z') {
        if (debug) Serial.print("Booting slave...");
        delay(1000);
        if (oled_active) {
          for(int i=0;i<13;i++) {
            if (debug) Serial.print(".");
            delay(1000);
          }
        }
        if (debug) Serial.println(F(" done!"));
        delay(1000);
      }
      
      i++;
    }
  }

  return command_response;
}



/*
   -------------------------------------------------------
   TONES 
   -------------------------------------------------------
*/
void play_phrases() {
    int K = 2000;
    switch (random(1,7)) {
        case 1:phrase1(); break;
        case 2:phrase2(); break;
        case 3:phrase1(); phrase2(); break;
        case 4:phrase1(); phrase2(); phrase1();break;
        case 5:phrase1(); phrase2(); phrase1(); phrase2(); phrase1();break;
        case 6:phrase2(); phrase1(); phrase2(); break;
    }
    for (int i = 0; i <= random(3, 9); i++){
        tone(BUZZ, K + random(-1700, 2000));          
        delay(random(70, 170));  
        noTone(BUZZ);         
        delay(random(0, 30));             
    } 
    noTone(BUZZ);  
}

void phrase1() {    
    int k = random(1000,2000);
    for (int i = 0; i <=  random(100,2000); i++){
        tone(BUZZ, k+(-i*2));          
        delay(random(.9,2));             
    } 
    for (int i = 0; i <= random(100,1000); i++){
        tone(BUZZ, k + (i * 10));          
        delay(random(.9,2));             
    } 
}

void phrase2() {
    int k = random(1000,2000);
    for (int i = 0; i <= random(100,2000); i++){
        tone(BUZZ, k+(i*2));          
        delay(random(.9,2));             
    } 
    for (int i = 0; i <= random(100,1000); i++){
        tone(BUZZ, k + (-i * 10));          
        delay(random(.9,2));             
    } 
}
