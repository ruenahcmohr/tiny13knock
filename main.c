/*   
   knock pattern recognition
   
   By Steve Hoefer http://grathio.com
   Version 0.1.10.20.10
   Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
   http://creativecommons.org/licenses/by-nc-sa/3.0/us/
   (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)

  Converted to avr via rue_mohr Dec 2013
  and rewritten,
  and optimized,
  and modified for multi-pattern detection,
  
   todo, 
     - Use the pizo for a beeper to tell you that you got it.
     - modify for fixed vocabulary voice recognition.
*/
/*


 
 Tiny13:
 
 PORT B0  match pattern 1
 PORT B1  match pattern 2
 PORT B2  match pattern 3
 PORT B3  pulse input
 PORT B4  program button
 PORT B5  RESET
 
 
 


PA0-+-----/\/\150R/\/\-----
    |                     |
    --/\/\1M/\/-         --- 
               |         EEE   Pizo element
	       |         ---
               |          |
GND------------+-----------



control motors on ports B and C, port A can be used for limit switch
inputs
*/

#include "avrcommon.h"
#include <avr/io.h>
#include <avr/interrupt.h>


#define OUTPUT             1
#define INPUT              0

// remember to keep programming pullup high
#define OFF               16
#define LEDOFF()          PORTB = OFF
#define Led1()            PORTB ^= 1
#define Led2()            PORTB ^= 2
#define Led3()            PORTB ^= 4



#define IsProgram()        IsLow(4, PINB)

// Tuning constants.  Could be made vars and hoooked to potentiometers for soft configuration, etc.

// Minimum signal from the piezo to register as a knock
// 7@5Vref = .034V  == 31@1.1Vref
//#define  THRESHOLD           7   
#define THRESHOLD             40

// If an individual knock is off by this percentage of a knock we don't unlock.. (25)
#define  REJECTVALUE         25 

// If the average timing of the knocks is off by this percent we don't unlock.
#define  AVERAGEREJECTVALUE  20  

// milliseconds we allow a knock to fade before we listen for another one. (Debounce timer.) 6Hz
#define  KNOCKFADETIME       70  

// Maximum number of knocks to listen for.
// tiny13 cant take much more than 7
#define  MAXIMUMKNOCKS       7 

// Longest time to wait for a knock before we assume that it's finished. (1200)
#define  KNOCKCOMPLETE       1200


typedef struct knockSet_s {
  unsigned char set[MAXIMUMKNOCKS];
  unsigned char knockCount; // less 1
} knockSet_t;


knockSet_t code1 = {{100, 50, 100, 50, 50, 0, 0}, 5};  // . .. ...
knockSet_t code2 = {{50, 25, 25, 50, 100, 50, 0}, 6}; // . ....  ..  
knockSet_t code3 = {{50, 100, 50, 0, 0, 0, 0}, 3}; // .. ..


unsigned int  knockReadings[MAXIMUMKNOCKS];           // When someone knocks this array fills with delays between knocks.
unsigned char currentKnockCount ;
unsigned int  maxKnockInterval   ;

volatile unsigned int milliseconds;  // global clock

void           Beep(unsigned char d);
void           ADCInit(void);
void           recordKnock(void );
void           SetKnock(knockSet_t *this);
unsigned char  validateKnock(knockSet_t *this);
void           t13InitTimer(void) ;
void           Delay(unsigned int ms);


int main(void) {

  unsigned char lastCode;
  
 // Set clock prescaler: 0 gives full 2.4 MHz from internal oscillator.
//  CLKPR = (1 << CLKPCE);
//  CLKPR = 1;
  
 // set up directions 
  DDRB = (OUTPUT << PB0 | OUTPUT << PB1 |OUTPUT << PB2 |INPUT << PB3 |INPUT << PB4 |INPUT << PB5);  

  PORTB = OFF;  // pullup for programming button

/*

  ADC in continious conversion of channel 0
  RTC running counting ms with 32756Khz external osc.

*/
  ADCInit();
  t13InitTimer();  

  while(1) {
     if (ADC >= THRESHOLD) {  // Listen for any knock at all.
         Led1();
         recordKnock();    
       if (currentKnockCount >= 1) {
	 if (IsProgram()){  
           if (lastCode == 1)  SetKnock(&code1); 
	   if (lastCode == 2)  SetKnock(&code2);
	   if (lastCode == 3)  SetKnock(&code3);
	   Beep(1);
	   Beep(1);             
	 } else { 
           if (0) {              
           } else if (validateKnock(&code1) == 0) {             // validate knock
	     lastCode = 1;
             Led1(); 
           } else if (validateKnock(&code2) == 0 ) {
	     lastCode = 2;
	     Led2();
	   } else if (validateKnock(&code3) == 0 ) {
	     lastCode = 3;
	     Led3();  
	   } else {
             Beep(1);   	     
	     lastCode = 0;   
           }          
	 }
       }
     } 
   } 

  return 0;

}




// Records the timing of knocks.
void recordKnock(void ){

  unsigned char i = 0;
  unsigned char currentKnockNumber;
  unsigned int  startTime, now;
  int           delta;

  milliseconds = 0;    // reset clock.

  // Reset the listening array.
  for (i = 0; i < MAXIMUMKNOCKS; i++)     knockReadings[i] = 0;
   
  currentKnockNumber = 0;         			// Incrementer for the array.
  maxKnockInterval   = 0;
  startTime          = 0;                              // Reference for when this knock started.  
  
  Delay(KNOCKFADETIME);                       	        // wait for this peak to fade before we listen to the next one.
  Led1();
  do {                                                  // listen for the next knock or wait for it to timeout. 
    now  = milliseconds; 
    if ( ADC >= THRESHOLD){         // got another knock...
      Led1();
      delta = now - startTime ;     
      if ( delta > maxKnockInterval)
        maxKnockInterval = delta;
      knockReadings[currentKnockNumber] = delta;  
      currentKnockNumber ++;                             // increment the counter
      startTime = now;                            
      Delay(KNOCKFADETIME);                              // again, a little delay to let the knock decay.
      Led1();
    }
    
  } while ((now-startTime < KNOCKCOMPLETE) && (currentKnockNumber < MAXIMUMKNOCKS)); // did we timeout or run out of knocks?
  
  // normalize data
  for (i = 0; i < MAXIMUMKNOCKS; i++)    knockReadings[i] = (100*knockReadings[i]) / maxKnockInterval;   
  currentKnockCount = currentKnockNumber ;
}


void SetKnock(knockSet_t *this) {
   unsigned char i;
   for (i = 0; i < MAXIMUMKNOCKS; i++)   this->set[i] = knockReadings[i];
   this->knockCount = currentKnockCount;
}

/*
  Checks if our knock matches the secret.
  returns true if it's a good knock, false if it's not.
  Compare the relative intervals of our knocks, not the absolute time between them.
  (ie: if you do the same pattern slow or fast it should still open the door.)
  This makes it less picky, which while making it less secure can also make it
  less of a pain to use if you're tempo is a little slow or fast. 
  */

unsigned char validateKnock(knockSet_t *this){
  unsigned char i;  
  int totaltimeDifferences = 0;
  int timeDiff             = 0;   
  
  if (currentKnockCount != this->knockCount){
  // Beep(2);
   return 1;                      // fail if the count of knocks does not match
  }
  
  for (i = 0; i < MAXIMUMKNOCKS; i++){                                          // Normalize the times  
     timeDiff              = ABS(knockReadings[i]-(this->set[i]));
     totaltimeDifferences += timeDiff;
     if (timeDiff > REJECTVALUE) {
     //  Beep(3);
       return 2;                                  // Individual value too far out of whack
     }
  }
  
  if ((totaltimeDifferences/(this->knockCount)) > AVERAGEREJECTVALUE) {
   // Beep(4);
    return 3;  // It can also fail if the whole thing is too inaccurate.
  }
  
  return 0;                                                                  // otherwise I guess we just have to accept it
  
}


void ADCInit(void) {
  ADMUX  = 3 + (1<<REFS0);
  // Activate ADC with Prescaler of 16, continious converstion, channel 0
  // 2.4Mhz/16 = 150Khz
  // internal ref of 1.1V 
  ADCSRA =  ((1 << ADEN)|(1 << ADSC)|(1 << ADATE)|(1 << ADIF)|(0 << ADIE)|(1 << ADPS2)|(0 << ADPS1)|(0 << ADPS0));
}

/*
  Bit of a kludge, 
  run the controller at 16Mhz, this divides it by 64 (64*256) to get 976ish Hz time base
  rtc is too slow
*/
void t13InitTimer(void) {

   TCNT0 = 0x00; 
   TCCR0B |= ((0<<FOC0A)|(0<<WGM02)|(0<<WGM02)|(0<<CS02)|(1<<CS01)|(0<<CS00)); // 2.4Mhz /256*8 = ~1Khz                                
                                                   //divide by 64 for 1 ms-ish for every overflow to occur 
   TIMSK0 |= (1<<TOIE0);                            //set 8-bit Timer/Counter0 Overflow Interrupt Enable 
   sei();                                          //set the Global Interrupt Enable Bit

}

void Delay(unsigned int ms) {
  unsigned int end ;
  end = milliseconds+ms;
  while (milliseconds != end);
}


ISR(TIM0_OVF_vect) {
  milliseconds++;
}

// reverse the pizo io direction and beep to it, then wait for it to settle.

void Beep(unsigned char d) {
 unsigned char i;
 
 DDRB |= 8;
 
 for (i = 0; i < 254; i++) {
   Delay(d);
   PORTB ^= 8;
 }
 
 DDRB &= ~8;
 Delay(800);

}








































