
    #include <stdio.h>
    #include "system.h"
    #include "includes.h"
    #include "altera_avalon_pio_regs.h"
    #include "sys/alt_irq.h"
    #include "sys/alt_alarm.h"
    #include "alt_types.h"
    #include <time.h>
    #include <sys/alt_timestamp.h>
    #include <sys/alt_cache.h>

    #define N 8192

    #define M 32

    #define DEBUG 1

    #define HW_TIMER_PERIOD 100 /* 100ms */

    /* Button Patterns */

    #define GAS_PEDAL_FLAG      0x08
    #define BRAKE_PEDAL_FLAG    0x04
    #define CRUISE_CONTROL_FLAG 0x02
    #define BUTTON_FLAG         0x000f
    /* Switch Patterns */
    #define ENGINE_FLAG         0x00000001
    #define TOP_GEAR_FLAG       0x00000002
    #define SWITCH_4            0x00000010
    #define SWITCH_5            0x00000020
    #define SWITCH_6            0x00000040
    #define SWITCH_7            0x00000080
    #define SWITCH_8            0x00000100
    #define SWITCH_9            0x00000200
    #define SWITCH_FLAG         0x3f


    /* LED Patterns */

    #define LED_RED_0  0x00000001 // Engine
    #define LED_RED_1  0x00000002 // Top Gear
    #define LED_RED_12 0x00001000 //position = [0,400]
    #define LED_RED_13 0x00002000 //position = [400,800]
    #define LED_RED_14 0x00004000
    #define LED_RED_15 0x00008000
    #define LED_RED_16 0x00010000 //position = [1600,2000]
    #define LED_RED_17 0x00020000 //position = [2000m, 2400m]



    #define LED_GREEN_0 0x0001 // Cruise Control activated
    #define LED_GREEN_2 0x0002 // Cruise Control Button
    #define LED_GREEN_3 0x0004 // Cruise Control Button
    #define LED_GREEN_4 0x0010 // Brake Pedal
    #define LED_GREEN_6 0x0040 // Gas Pedal

    #define TASK_STACKSIZE 2048

    OS_STK StartTask_Stack[TASK_STACKSIZE];
    OS_STK ControlTask_Stack[TASK_STACKSIZE];
    OS_STK VehicleTask_Stack[TASK_STACKSIZE];
    OS_STK WatchdogTask_Stack[TASK_STACKSIZE];
    OS_STK DetectionTask_Stack[TASK_STACKSIZE];
    OS_STK ExtraLoadTask_Stack[TASK_STACKSIZE];
    OS_STK ButtonIO_Stack[TASK_STACKSIZE];
    OS_STK SwitchIO_Stack[TASK_STACKSIZE];
    // Task Priorities

    #define STARTTASK_PRIO      5
    #define VEHICLETASK_PRIO    10
    #define CONTROLTASK_PRIO    12
    #define DETECTIONTASK_PRIO  14
    #define WATCHDOGTASK_PRIO   15
    #define EXTRALOAD_PRIO      13
    #define BUTTON_PRIO         7
    #define SWITCH_PRIO         8


    // Task Periods

    #define CONTROL_PERIOD  300
    #define VEHICLE_PERIOD  300
    #define BUTTON_PERIOD   300
    #define SWITCH_PERIOD   300

    /*
     * Definition of Kernel Objects
     */

    // Mailboxes
    OS_EVENT *Mbox_Throttle;
    OS_EVENT *Mbox_Velocity;
    OS_EVENT *Mbox_Target;
    OS_EVENT *Mbox_Writeok;

    // Semaphores
    OS_EVENT *Sem_Vehicle;
    OS_EVENT *Sem_Control;
    OS_EVENT *Sem_Button;
    OS_EVENT *Sem_Switch;
    OS_EVENT *Sem_ExtraLoad;
    OS_EVENT *Sem_Watchdog;
    OS_EVENT *Sem_SignalOk;
    // SW-Timer
    OS_TMR *MyTmr_Vehicle;
    OS_TMR *MyTmr_Control;
    OS_TMR *MyTmr_Button;
    OS_TMR *MyTmr_Switch;
    OS_TMR *MyTmr_Watchdog;
    OS_TMR *MyTmr_ExtraLoad;

    alt_u32 ticks;
    alt_u32 time_1;
    alt_u32 time_2;
    /*
     * Types
     */
    enum active {on, off};

    enum active gas_pedal = off;
    enum active brake_pedal = off;
    enum active top_gear = off;
    enum active engine = off;
    enum active cruise_control = off;
    enum active switch4 = off;
    enum active switch5 = off;
    enum active switch6 = off;   
    enum active switch7 = off;
    enum active switch8 = off;
    enum active switch9 = off;

    //enum active status = {signalok,overload};



    /*
     * Global variables
     */
    int delay; // Delay of HW-timer
    int overload_array [6];
    INT16U led_green = 0; // Green LEDs
    INT32U led_red = 0;   // Red LEDs
    alt_u32 ticks;
    alt_u32 time_1;
    alt_u32 time_2;
    alt_u32 timer_overhead;


// Task for Time measurements
    float microseconds(int ticks)
{
  return (float) 1000000 * (float) ticks / (float) alt_timestamp_freq();
}

void start_measurement()
{
      /* Flush caches */
      alt_dcache_flush_all();
      alt_icache_flush_all();   
      /* Measure */
      alt_timestamp_start();
      time_1 = alt_timestamp();
}

void stop_measurement()
{
      time_2 = alt_timestamp();
      ticks = time_2 - time_1;
}

// Soft Timer Tasks
    void TmrCallback_Vehicle()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_Vehicle); // Releasing the key
    }
    void TmrCallback_Control()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_Control); // Releasing the key
    }
    void TmrCallback_Button()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_Button); // Releasing the key

    }
    void TmrCallback_Switch()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_Switch); // Releasing the key
    }
    void TmrCallback_Watchdog()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_Watchdog); // Releasing the key

    }
    void TmrCallback_ExtraLoad()
    {
        // Post to the semaphore to signal that it's time to run the task.
        OSSemPost(Sem_ExtraLoad); // Releasing the key
    }

    int buttons_pressed(void)
    {
        return ~IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_KEYS4_BASE);
    }

    int switches_pressed(void)
    {
        return IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_TOGGLES18_BASE);
    }

    /*
     * ISR for HW Timer
     */
    alt_u32 alarm_handler(void* context)
    {
        OSTmrSignal(); /* Signals a 'tick' to the SW timers */

        return delay;
    }
   /*
    /*void shownumberonleds(int x)
    {
    IOWR_ALTERA_AVALON_PIO_DATA( LED_PIO_BASE , x ) ;
    }*/

    static int b2sLUT[] = {0x40, //0
                           0x79, //1
                           0x24, //2
                           0x30, //3
                           0x19, //4
                           0x12, //5
                           0x02, //6
                           0x78, //7
                           0x00, //8
                           0x18, //9
                           0x3F, //-
                          };

    /*
     * convert int to seven segment display format
     */
    int int2seven(int inval) {
        return b2sLUT[inval];
    }

    /*
     * output current velocity on the seven segement display
     */
    void show_velocity_on_sevenseg(INT8S velocity) {
        int tmp = velocity;
        int out;
        INT8U out_high = 0;
        INT8U out_low = 0;
        INT8U out_sign = 0;

        if (velocity < 0) {
            out_sign = int2seven(10);
            tmp *= -1;
        } else {
            out_sign = int2seven(0);
        }

        out_high = int2seven(tmp / 10);
        out_low = int2seven(tmp - (tmp/10) * 10);

        out = int2seven(0) << 21 |
              out_sign << 14 |
              out_high << 7  |
              out_low;
        IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_LOW28_BASE,out);
    }


    /*
     * output current position on the seven segement display
     */
    void show_position_on_sevenseg(INT8S position) {


    }

    void show_target_velocity(INT8S velocity) {
        int tmp = velocity;
        int out;
        INT8U out_high = 0;
        INT8U out_low = 0;
        INT8U out_sign = 0;

        if (velocity < 0) {
            out_sign = int2seven(10);
            tmp *= -1;
        } else {
            out_sign = int2seven(0);
        }

        out_high = int2seven(tmp / 10);
        out_low = int2seven(tmp - (tmp/10) * 10);

        out = int2seven(0) << 21 |
              out_sign << 14 |
              out_high << 7  |
              out_low;
        if (cruise_control == off) {
            IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_HIGH28_BASE,out);
            //IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_GREENLED9_BASE, LED_GREEN_0); ///////////////////////////
        } else {

            IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_HIGH28_BASE,0x00);
        }
    }

    /*
     * indicates the position of the vehicle on the track with the four leftmost red LEDs
     * LEDR17: [0m, 400m)
     * LEDR16: [400m, 800m)
     * LEDR15: [800m, 1200m)
     * LEDR14: [1200m, 1600m)
     * LEDR13: [1600m, 2000m)
     * LEDR12: [2000m, 2400m]
     */
    void show_position(INT16U position)
    {
     if (position < 4000)
        if (engine==on)
            if(top_gear==on)
            // Turn ON only those LED below remain unchanged the other LEDS
              IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_17);
            else
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_17);
        else
            if(top_gear==on)
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_17);
            else
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_17);

    else
      if (position < 8000)
          if (engine==on)
              if(top_gear==on)
                  IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_16);
              else
                  IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_16);
          else
              if(top_gear==on)
                  IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_16);
              else
                  IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_16);

      else
        if (position < 12000)
            if (engine==on)
                if(top_gear==on)
                    IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_15);
                else
                    IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_15);
            else
                if(top_gear==on)
                    IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_15);
                else
                    IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_15);

        else
          if (position < 16000)
                if (engine==on)
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_14);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_14);
                else
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_14);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_14);

          else
            if (position < 20000)
                if (engine==on)
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_13);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_13);
                else
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_13);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_13);
            else
                if (engine==on)
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_1|LED_RED_12);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0|LED_RED_12);
                else
                    if(top_gear==on)
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1|LED_RED_12);
                    else
                        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_12);


    }

    /*
     * The function 'adjust_position()' adjusts the position depending on the
     * acceleration and velocity.
     */
    INT16U adjust_position(INT16U position, INT16S velocity,
                           INT8S acceleration, INT16U time_interval)
    {
        INT16S new_position = position + velocity * time_interval / 1000
                              + acceleration / 2  * (time_interval / 1000) * (time_interval / 1000);

        if (new_position > 24000) {
            new_position -= 24000;
        } else if (new_position < 0) {
            new_position += 24000;
        }

        //show_position(new_position);
        return new_position;
    }

    /*
     * The function 'adjust_velocity()' adjusts the velocity depending on the
     * acceleration.
     */
    INT16S adjust_velocity(INT16S velocity, INT8S acceleration,
                           enum active brake_pedal, INT16U time_interval)
    {
        INT16S new_velocity;
        INT8U brake_retardation = 200;

        if (brake_pedal == off)
            new_velocity = velocity  + (float) (acceleration * time_interval) / 1000.0;
        else {
            if (brake_retardation * time_interval / 1000 > velocity)
                new_velocity = 0;
            else
                new_velocity = velocity - brake_retardation * time_interval / 1000;
        }

        return new_velocity;
    }

 //turns off led and cruise control
    void cruise_off()
{
    //IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_GREENLED9_BASE,led_green &~ LED_GREEN_2);
    cruise_control = off;
    show_target_velocity(0);
}

   void ButtonIO(void* pdata)
{
    INT8U err;
    INT16S target;
    INT16S* current_velocity;
    int button;
    while(1)
    {
    OSSemPend(Sem_Button,0,&err);
    button = buttons_pressed();
    if( button & CRUISE_CONTROL_FLAG ) {  /* push button1 */
        if(cruise_control == on) {
          cruise_off();
            target = 0;
        } else if(top_gear == on) {
            current_velocity = (INT16S*) OSMboxPend(Mbox_Velocity, 0, &err);
            if( *current_velocity >= 200) {
                target = *current_velocity;
                show_target_velocity((INT8U) (target / 10));
                //err = OSMboxPost(Mbox_Target, (void *) &target);   POSTED BELOW
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,LED_GREEN_2); // LEDG2 on
                cruise_control = on;
            }
        }
    }

    if( button & BRAKE_PEDAL_FLAG ) {     /* Push Button 2 */
        if(brake_pedal == off) {
            brake_pedal = on;
            IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,LED_GREEN_4); // LEDG4 on
            cruise_off();
        }
    } else {
        if(brake_pedal == on) {
            IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,led_green &~ LED_GREEN_4); // LEDG4 off
            brake_pedal = off;
        }
    }

    if( (button & GAS_PEDAL_FLAG) && engine == on) {       /* push button 3 */
        if(gas_pedal == off) {
            gas_pedal = on;
          IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,LED_GREEN_6); // LEDG6 on
            cruise_off();
        }
    } else {
        if(gas_pedal == on) {
            IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,led_green &~ LED_GREEN_6); // LEDG6 off
            gas_pedal = off;

        }
    }

    //printf("ButtonIO working \n");
    if(cruise_control == on)
        err = OSMboxPost(Mbox_Target, (void *) &target);
  }
}

    void SwitchIO(void* pdata)
    {
      INT8U err;
      int switches;
      INT8U current_velocity;

      while(1)
      {
        OSSemPend(Sem_Switch,0,&err);
        switches = switches_pressed() & 0x3F3;
        if(switches & ENGINE_FLAG) {
            if(engine == off) {
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_0);
                engine = on;
            }
        } else {
            if(engine == on) {
                INT16S* current_velocity = (INT16S*) OSMboxPend(Mbox_Velocity, 0, &err);
                if(*current_velocity == 0) {
                    //IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,~LED_RED_0);
                    engine = off;
                }
            } // else do nothing,

        }

        if(switches & TOP_GEAR_FLAG) 
        {
            if(top_gear == off) 
            {
                IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,LED_RED_1);
                top_gear = on;
                //printf("Turning top gear on\n");
            } // else do nothing
        } 
        else
         {
            if(top_gear == on) 
            {   
                //IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_REDLED18_BASE,~LED_RED_1);
                top_gear = off;
              cruise_off();
                //printf("Turning top gear off\n");
            } // else do nothing
        }
    
        if(switches & SWITCH_4)
        {
           // printf("Turning Switch 4\n");
            if ( switch4 == off ){
                overload_array[5] = 2;
                switch4 = on;
                //OSSemPost(Sem_ExtraLoad);
               // printf("Turning Switch 4 ON\n");
            }
           
        }
         else{
                overload_array[5] = 0;
                switch4 = off;
                //OSSemPost(Sem_ExtraLoad);
               // printf("Turning Switch 4 OFF\n");
            }
            
            
        if(switches & SWITCH_5)
        {
            if ( switch5 == off )
            {
                overload_array[4] = 4;
                switch5 = on;
                //OSSemPost(Sem_ExtraLoad);
            }
           
        }
         else{
                overload_array[4] = 0;
                switch5 = off;
                //OSSemPost(Sem_ExtraLoad);
            }

        if(switches & SWITCH_6)
        {
            if ( switch6 == off ){
                overload_array[3] = 8;
                switch6 = on;
                //OSSemPost(Sem_ExtraLoad);
                }
        }
         else{
                overload_array[3] = 0;
                switch6 = off; 
                //OSSemPost(Sem_ExtraLoad);
                }

        if(switches & SWITCH_7)
        {
            if ( switch7 == off ){
                overload_array[2] = 16;
                switch7 = on; 
               // OSSemPost(Sem_ExtraLoad);
                }
        }
         else{
                overload_array[2] = 0;
                switch7 = off; 
                //OSSemPost(Sem_ExtraLoad);
                }

        if(switches & SWITCH_8)
        {
            if ( switch8 == off ){
                overload_array[1] = 32;
                switch8 = on;
                //OSSemPost(Sem_ExtraLoad);
            }
        }
          else{
                overload_array[1] = 0;
                switch8 = off;
                //OSSemPost(Sem_ExtraLoad);
            }

        if(switches & SWITCH_9)
        {
            if ( switch9 == off ){
                overload_array[0] =64;
                switch9 = on;
                //OSSemPost(Sem_ExtraLoad);
            }
        }
         else{
                overload_array[0] = 0;
                switch9 = off;
                //OSSemPost(Sem_ExtraLoad);
            }
        
        //int i;
        //for(i=0; i<6; i++)
           // printf("ARRAY : %d\n", overload_array[i]);
      }
    }

    /*
     * The task 'VehicleTask' updates the current velocity of the vehicle
     */
    void VehicleTask(void* pdata)
    {
        INT8U err;
        void* msg;
        INT8U* throttle;
        INT8S acceleration;  /* Value between 40 and -20 (4.0 m/s^2 and -2.0 m/s^2) */
        INT8S retardation;   /* Value between 20 and -10 (2.0 m/s^2 and -1.0 m/s^2) */
        INT16U position = 0; /* Value between 0 and 20000 (0.0 m and 2000.0 m)  */
        INT16S velocity = 0; /* Value between -200 and 700 (-20.0 m/s amd 70.0 m/s) */
        INT16S wind_factor;   /* Value between -10 and 20 (2.0 m/s^2 and -1.0 m/s^2) */

        printf("Vehicle task created!\n");
        while (1)
        {
            OSSemPend(Sem_Vehicle, 0, &err); // Trying to access the key
            err = OSMboxPost(Mbox_Velocity, (void *) &velocity);
            /* Non-blocking read of mailbox:
             - message in mailbox: update throttle
             - no message:         use old throttle
            */
            msg = OSMboxPend(Mbox_Throttle, 1, &err);
            if (err == OS_NO_ERR)
                throttle = (INT8U*) msg;

            /* Retardation : Factor of Terrain and Wind Resistance */
            if (velocity > 0)
                wind_factor = velocity * velocity / 10000 + 1;
            else
                wind_factor = (-1) * velocity * velocity / 10000 + 1;

            if (position < 4000)
                retardation = wind_factor; // even ground
            else if (position < 8000)
                retardation = wind_factor + 15; // traveling uphill
            else if (position < 12000)
                retardation = wind_factor + 25; // traveling steep uphill
            else if (position < 16000)
                retardation = wind_factor; // even ground
            else if (position < 20000)
                retardation = wind_factor - 10; //traveling downhill
            else
                retardation = wind_factor - 5 ; // traveling steep downhill

            acceleration = *throttle / 2 - retardation;
            position = adjust_position(position, velocity, acceleration, 300);
            //setGlobalPosition(position);
            velocity = adjust_velocity(velocity, acceleration, brake_pedal, 300);
            show_position(position);
            printf("Position: %dm\n", position / 10);
            printf("Velocity: %4.1fm/s\n", velocity /10.0);
            printf("Throttle: %dV\n", *throttle / 10);
            show_velocity_on_sevenseg((INT8S) (velocity / 10));
        }
    }

    /*
     * The task 'ControlTask' is the main task of the application. It reacts
     * on sensors and generates responses.
     */

void ControlTask(void* pdata)
{
  INT8U err;
  INT8U throttle = 0; /* Value between 0 and 80, which is interpreted as between 0.0V and 8.0V */
  void* msg;
  INT16S* current_velocity;
  INT8S diff;

  cruise_off();

  printf("Control Task created!\n");

  while(1)
    {
      msg = OSMboxPend(Mbox_Velocity, 0, &err);
      current_velocity = (INT16S*) msg;

      msg = OSMboxAccept(Mbox_Target);
      if(msg == NULL)
        diff = 0;
      else
        diff = *((INT16S *) msg) - *current_velocity;
      OSSemPend(Sem_Control, 0, &err);

      /* controller starts here */
      if (cruise_control == on) {
        if(diff < -5) {
            //printf("no throttle!\n");
            throttle = 0;
        } else if(diff > 5) {
            //printf("full throttle!\n");
            throttle = 80;
        } else {
            throttle = 10;
        }
      } else if (gas_pedal == on) {
        throttle = 80;
      } else {
        throttle = 0;
      }

      err = OSMboxPost(Mbox_Throttle, (void *) &throttle);
    }
}

void ExtraLoadTask(void *pdata)
{
    int i,j;
    int x;
    INT8U err;
    
   while(1)
    {
         int decimal = 0;
         OSSemPend(Sem_ExtraLoad,0, &err);
         for(i=0; i<6; i++)
            decimal += overload_array[i];
         
         printf("Decimal Value %d\n",decimal);
         
         start_measurement();  
         for(j=0; j<=decimal; j++)
         {
            for(i=0; i<20; i++) // 20 is a dummy integer
                x= i;
            decimal--;
         }
         stop_measurement();
         printf(" Time Measured : %5.2f ms\n", (float) microseconds(ticks));
         printf("(%d ticks)\n", (int) (ticks));
       
         OSSemPost(Sem_SignalOk);
    }
}

void WatchDogTask(void *pdata)
{
    INT8U err;
    void *msg;
    int signal;
    while(1)
    {
        msg = OSMboxPend(Mbox_Writeok, 70000, &err); // 70k ticks, 2300 ms is the time out for watchdog timer. the watchdog waits for 2.3 secs
        //signal = *(INT16S*) msg;
        msg = OSMboxAccept(Mbox_Writeok); // Accepts the message and deletes the content in Mbox_Writeok
        printf("Watchdog task msg %d\n", *((int*)msg));
        if(msg == NULL)
        {
            printf("Overload \n"); // if time out occurs msg is NULL, so we print OVELOAD
        }
        
    }
}

void DetectionTask(void *pdata)
{
    INT8U err;
    int signal = 1;
    while(1)
    {
        OSSemPend(Sem_SignalOk,0,&err); // Wait for the exttra Load task to Post the semaphore
        err = OSMboxPost(Mbox_Writeok, (void *) signal); // Write OK in Mbox_Writeok
        printf("Detection task \n");
    }
}
    void StartTask(void* pdata)
    {
        INT8U err;
        void* context;
        BOOLEAN status;
        IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_REDLED18_BASE,0X00000);
        IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_HIGH28_BASE,0X00000);
        IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,0x00000);         ///////////////////////
        static alt_alarm alarm;     /* Is needed for timer ISR function */

        /* Base resolution for SW timer : HW_TIMER_PERIOD ms */
        delay = alt_ticks_per_second() * HW_TIMER_PERIOD / 1000;
        printf("delay in ticks %d\n", delay);
        //printf("Starting time measurement\n");
        //startTimeMeasurement();
        /*
         * Create Hardware Timer with a period of 'delay'
         */
        if (alt_alarm_start (&alarm,
                             delay,
                             alarm_handler,
                             context) < 0)
        {
            printf("No system clock available!n");
        }

        /*
         * Create and start Software Timer
         */

        MyTmr_Vehicle = OSTmrCreate(0,
                              (CONTROL_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_Vehicle,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

        //IOWR_ALTERA_AVALON_PIO_DATA (DE2_PIO_GREENLED9_BASE,led_green | LED_GREEN_2);

        status = OSTmrStart(MyTmr_Vehicle,&err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }

        MyTmr_Control = OSTmrCreate(0,
                              (CONTROL_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_Control,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

        status = OSTmrStart(MyTmr_Control,
                                    &err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }

        MyTmr_Button = OSTmrCreate(0,
                              (BUTTON_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_Button,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

        status = OSTmrStart(MyTmr_Button,
                                    &err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }

        MyTmr_Switch = OSTmrCreate(0,
                              (SWITCH_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_Switch,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

        status = OSTmrStart(MyTmr_Switch,
                                    &err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }


        MyTmr_Watchdog = OSTmrCreate(0,
                              (CONTROL_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_Watchdog,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

         status = OSTmrStart(MyTmr_Watchdog,
                                    &err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }

        MyTmr_ExtraLoad = OSTmrCreate(0,
                              (CONTROL_PERIOD/100),
                              OS_TMR_OPT_PERIODIC,
                              TmrCallback_ExtraLoad,
                              NULL,
                              NULL,
                              &err);
        if (err == OS_ERR_NONE) {
            /* Timer was created */
            printf("Soft timer was created \n");
        }

        status = OSTmrStart(MyTmr_ExtraLoad,
                                    &err);
        if (status > 0 && err == OS_ERR_NONE) {
            /* Timer was started */
            printf("Soft timer was started!\n");
        }
        /*
         * Creation of Kernel Objects
         */

        // Mailboxes
        Mbox_Throttle = OSMboxCreate((void*) 0); /* Empty Mailbox - Throttle */
        Mbox_Velocity = OSMboxCreate((void*) 0); /* Empty Mailbox - Velocity */
        Mbox_Target = OSMboxCreate((void*) 0); /* Empty Mailbox - target velocity*/
        Mbox_Writeok = OSMboxCreate((void*) 0); /* Empty Mailbox - Write ok */


        //Semaphores
        Sem_Vehicle = OSSemCreate(1); // binary semaphore (1 key)
        Sem_Control = OSSemCreate(1); // binary semaphore (1 key)
        Sem_Button = OSSemCreate(1); // binary semaphore (1 key)
        Sem_Switch = OSSemCreate(1); // binary semaphore (1 key)
        Sem_Watchdog = OSSemCreate(1); // binary semaphore (1 key)
        Sem_ExtraLoad = OSSemCreate(1); // binary semaphore (1 key)

        /*
         * Create statistics task
         */

        OSStatInit();

        /*
         * Creating Tasks in the system
         */

        err = OSTaskCreateExt(
                  ControlTask, // Pointer to task code
                  NULL,        // Pointer to argument that is
                  // passed to task
                  &ControlTask_Stack[TASK_STACKSIZE-1], // Pointer to top
                  // of task stack
                  CONTROLTASK_PRIO,
                  CONTROLTASK_PRIO,
                  (void *)&ControlTask_Stack[0],
                  TASK_STACKSIZE,
                  (void *) 0,
                  OS_TASK_OPT_STK_CHK);

        err = OSTaskCreateExt(
                  VehicleTask, // Pointer to task code
                  NULL,        // Pointer to argument that is
                  // passed to task
                  &VehicleTask_Stack[TASK_STACKSIZE-1], // Pointer to top
                  // of task stack
                  VEHICLETASK_PRIO,
                  VEHICLETASK_PRIO,
                  (void *)&VehicleTask_Stack[0],
                  TASK_STACKSIZE,
                  (void *) 0,
                  OS_TASK_OPT_STK_CHK);

                  err = OSTaskCreateExt(
                       ButtonIO, // Pointer to task code
                       NULL,        // Pointer to argument that is
                                    // passed to task
                       &ButtonIO_Stack[TASK_STACKSIZE-1], // Pointer to top
                                             // of task stack
                       BUTTON_PRIO,
                       BUTTON_PRIO,
                       (void *)&ButtonIO_Stack[0],
                       TASK_STACKSIZE,
                       (void *) 0,
                       OS_TASK_OPT_STK_CHK);

                  err = OSTaskCreateExt(
                       SwitchIO, // Pointer to task code
                       NULL,        // Pointer to argument that is
                                    // passed to task
                       &SwitchIO_Stack[TASK_STACKSIZE-1], // Pointer to top
                                             // of task stack
                       SWITCH_PRIO,
                       SWITCH_PRIO,
                       (void *)&SwitchIO_Stack[0],
                       TASK_STACKSIZE,
                       (void *) 0,
                       OS_TASK_OPT_STK_CHK);

        err = OSTaskCreateExt(
                  WatchDogTask, // Pointer to task code
                  NULL,        // Pointer to argument that is
                  // passed to task
                  &WatchdogTask_Stack[TASK_STACKSIZE-1], // Pointer to top
                  // of task stack
                  WATCHDOGTASK_PRIO,
                  WATCHDOGTASK_PRIO,
                  (void *)&WatchdogTask_Stack[0],
                  TASK_STACKSIZE,
                  (void *) 0,
                  OS_TASK_OPT_STK_CHK);
                  
        err = OSTaskCreateExt(
                  ExtraLoadTask, // Pointer to task code
                  NULL,        // Pointer to argument that is
                  // passed to task
                  &ExtraLoadTask_Stack[TASK_STACKSIZE-1], // Pointer to top
                  // of task stack
                  EXTRALOAD_PRIO,
                  EXTRALOAD_PRIO,
                  (void *)&ExtraLoadTask_Stack[0],
                  TASK_STACKSIZE,
                  (void *) 0,
                  OS_TASK_OPT_STK_CHK); 
                  
        err = OSTaskCreateExt(
                  DetectionTask, // Pointer to task code
                  NULL,        // Pointer to argument that is
                  // passed to task
                  &DetectionTask_Stack[TASK_STACKSIZE-1], // Pointer to top
                  // of task stack
                  DETECTIONTASK_PRIO,
                  DETECTIONTASK_PRIO,
                  (void *)&DetectionTask_Stack[0],
                  TASK_STACKSIZE,
                  (void *) 0,
                  OS_TASK_OPT_STK_CHK);

        printf("All Tasks and Kernel Objects generated!\n");
        /* Task deletes itself */

        OSTaskDel(OS_PRIO_SELF);
    }

    int main(void) {

        printf("Cruise Control 20141010\n");

        OSTaskCreateExt(
            StartTask, // Pointer to task code
            NULL,      // Pointer to argument that is
            // passed to task
            (void *)&StartTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            STARTTASK_PRIO,
            STARTTASK_PRIO,
            (void *)&StartTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR);

        OSStart();

        return 0;
    }
