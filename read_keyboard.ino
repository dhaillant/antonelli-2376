/*
  6 shift registers in total

  4 for keyboard only
  1 for switches only
  1 for both keyboard and switches

  shift register 1: O1.C  to O1.G
                 2: O1.G# to O2.D#
                 3: O2.E  to O2.B
                 4: O3.C  to O3.G
                 5: O3.G# to S3
                 6: S4    to S10

  each octave uses 1 and half shift register
  37 keys in total (3 octaves + 1 key)

  shift registers are driven by:
    "COPI" (pin #14 "DS")     D11 on Arduino (PB3)
    "SCK"  (pin #11 "SHCP")   D13 on Arduino (PB5)
    "PS"   (pin #12 "STCP")   D10 on Arduino (PB2)

  shift registers /OE pin is held low (active) -> outputs are always either ON or OFF
  shift registers /MR pin is held low (active) -> no global reset of the registers


  Keyboard buses are on D2 and D3
  D2 is bus_NO (PD2)
  D3 is bus_NC (PD3)

  a non pressed key is in contact with bus_NC

  D2 and D3 are inputs, with internal pull-up

  Switches use two separate busses
  Switch bus A is D4
  Switch bus B is D7

  
 */


#include <MIDI.h>
#define MIDI_CHANNEL 1    // transmit on this channel
#define MIDI_BASE 42      // MIDI note for lowest key (C) (modify this value for transpose)

MIDI_CREATE_DEFAULT_INSTANCE();


// bus_NO is D2
#define BUS_NO_PIN 2

// bus NC is D3
#define BUS_NC_PIN 3

// Switches use two separate busses
// Switch bus A is D4
#define SWITCH_BUS_A_PIN 4

// Switch bus B is D7
#define SWITCH_BUS_B_PIN 7


//Pin connected to ST_CP of 74HC595 "latchPin"
#define SR_STCP 10
// bit register for ST_CP clock signal
#define SR_STCP_BIT PB2

//Pin connected to SH_CP of 74HC595 "clockPin"
#define SR_SHCP 13
// bit register for SH_CP clock signal
#define SR_SHCP_BIT PB5

//Pin connected to DS    of 74HC595 "dataPin"
#define SR_DS 11
#define SR_DS_BIT PB3


#define BV(bit)               (1 << bit)
#define setBit(byte, bit)     (byte |= BV(bit))
#define clearBit(byte, bit)   (byte &= ~BV(bit))
#define toggleBit(byte, bit)  (byte ^= BV(bit))

#define set_SHCP_high         setBit(PORTB, SR_SHCP_BIT)
#define set_SHCP_low          clearBit(PORTB, SR_SHCP_BIT)

#define set_STCP_high         setBit(PORTB, SR_STCP_BIT)
#define set_STCP_low          clearBit(PORTB, SR_STCP_BIT)

#define set_DS_high           setBit(PORTB, SR_DS_BIT)
#define set_DS_low            clearBit(PORTB, SR_DS_BIT)


#define read_bus_no           PIND & (1<<PIND2)
#define read_bus_nc           PIND & (1<<PIND3)


#define read_switch_bus_A     PIND & (1<<PIND4)
#define read_switch_bus_B     PIND & (1<<PIND7)


#define KEY_RELEASED 'o'
#define KEY_PRESSED '_'
#define KEY_UNKNOWN '.'
#define KEY_UNSET 0

#define N_KEYS 37

char keys[N_KEYS]         = { KEY_RELEASED };      // archived key states, to compare with the following:
char scanned_keys[N_KEYS] = { KEY_RELEASED };      // newly scanned key states

#define SWITCH_ON  'o'
#define SWITCH_OFF '_'

#define N_SWITCHES 10

char switches[N_SWITCHES]         = { SWITCH_OFF };   // each key or switch, 0 means not pressed, not-0 means pressed or being pressed
char scanned_switches[N_SWITCHES] = { SWITCH_OFF };


//void scan_shift_registers(void);


void setup() {
  /*
   * The two inputs are connected to the busses NO and NC
   * Pulled-up, we'll check for "low" events
   */
  pinMode(BUS_NO_PIN, INPUT_PULLUP);
  pinMode(BUS_NC_PIN, INPUT_PULLUP);

  /* 
   * Internal Pull Up is not enough. Added two 470 ohms resistors on NO and NC busses to pull up faster.
   * There's some parasitic capacitance preventing the voltage on each bus to reach +5V fast enough to not get detected twice 
   */


  pinMode(BUS_NO_PIN, INPUT_PULLUP);
  pinMode(BUS_NC_PIN, INPUT_PULLUP);


  /*
   * The 3 outputs: 2 clocks and one serial data out
   */
  pinMode(SR_STCP, OUTPUT);  // shift register latch pin
  set_STCP_low;

  pinMode(SR_SHCP, OUTPUT);  // shift register clock pin
  set_SHCP_low;

  pinMode(SR_DS, OUTPUT);    // shift register data  pin
  set_DS_low;


  for (uint8_t i = 0; i < N_KEYS; i++)
  {
    keys[i]         = KEY_RELEASED;
    scanned_keys[i] = KEY_RELEASED;
  }

  for (uint8_t i = 0; i < N_SWITCHES; i++)
  {
    switches[i]         = SWITCH_OFF;
    scanned_switches[i] = SWITCH_OFF;
  }


  // debug through serial communication
  Serial.begin(115200);
  Serial.println("Hello test keyboard");
}


void pulse_SR_shift_and_latch_clock(void)
{
  /*
   * Send a clock pulse on shift clock (SHCP) and on latch clock (STCP)
   * This allows the shift register to both move the serial data to the next bit, and to output the content of the latch register
   */
  set_SHCP_high;
  set_STCP_high;

  set_SHCP_low;
  set_STCP_low;
}



/*
void scan_keyboard(void)
{
  scan_keys();
  scan_switches();
}
*/

void scan_keys(void)
{
  /* 
   * scan N_KEYS Keys
   */
  char key_state = KEY_UNKNOWN;

  for (uint8_t i = 0; i < N_KEYS; i++)
  {
    if (i == 0)
    {
      set_DS_low;       // if it's the 1st step, send a "low" to the shift registers
    }
    else
    {
      set_DS_high;      // for the next steps, send "high" to keep all the other keys high
    }
    
    pulse_SR_shift_and_latch_clock();

    uint8_t bus_no = read_bus_no;    // read the Normally Open bus
    uint8_t bus_nc = read_bus_nc;    // read the Normally Closed bus

    /*
     * if a key is not pressed, it must be detected by bus_nc (normally Closed) by a LOW value
     * if the key is detected on bus_nc, it means the key isn't pressed (and not moving)
     * 
     * if the key isn't detected both on bus_nc and bus_no, it means it's either moving from bus_nc or there's a contact fault
     * 
     * if the key is detected on bus_no, the key is pressed (and not moving)
     * 
     */
    if (bus_nc == 0)  // if a LOW value is detected on bus_nc,
    {
      key_state = KEY_RELEASED;
    }
    else
    {
      // if a key is pressed, then bus_no should be 0 when we address that key
      if (bus_no == 0)  // if a LOW value is detected on bus_no
      {
        key_state = KEY_PRESSED;
      }
      else  // the key is in between bus_nc and bus_no
      {
        key_state = KEY_UNKNOWN;
      }
    }

    /*
    // check difference here
    if (key_state != keys[i])     // if the saved state for the adressed key is different from what we just read then
    {
      //handle_key_change(key_state, keys[i], i);
      handle_key_change(key_state, i);
    }
    */

    // save the current state
    scanned_keys[i] = key_state;
    //keys[i] = key_state;
  }
}


void scan_switches(void)
{
  /*
   * scan Switches
   */
  char switch_state = SWITCH_OFF;

  for (uint8_t i = 0; i < N_SWITCHES; i++)
  {
    pulse_SR_shift_and_latch_clock();

    uint8_t switch_bus_A = read_switch_bus_A;    // read switch bus A
    uint8_t switch_bus_B = read_switch_bus_B;    // read switch bus B
    
    if (switch_bus_B == 0)  // detected on switch_bus_B
    {
      switch_state = SWITCH_ON;
    }
    else  // NOT detected on switch_bus_B
    {
      if (switch_bus_A == 0)  // detected on switch_bus_A
      {
        switch_state = SWITCH_OFF;
      }
      else
      {
        // error or moving
      }
    }
    scanned_switches[i] = switch_state;
  }
}

void show_scanned_keys(void)
{
  for (uint8_t i = 0; i < N_KEYS; i++)
  {
    Serial.print(scanned_keys[i]);
    if (scanned_keys[i] == KEY_PRESSED)
    {
      //Serial.print(i);
    }
  }
  Serial.println();
}

void show_scanned_switches(void)
{
  for (uint8_t i = 0; i < N_SWITCHES; i++)
  {
    //Serial.print(i);
    Serial.print(scanned_switches[i]);
/*    if (scanned_switches[i] == SWITCH_ON)
    {
      //Serial.print(i);
      Serial.print('.');
    }
    else
    {
      Serial.print('x');
    }*/
  }
  Serial.println();
}


void find_changes(void)
{
  for (uint8_t i = 0; i < N_KEYS; i++)
  {
    if (scanned_keys[i] != keys[i])
    {
       handle_key_change(scanned_keys[i], i);
      /*
      Serial.print(i);
      Serial.print(' ');
      Serial.println(scanned_keys[i]);
      
      keys[i] = scanned_keys[i];

      if (keys[i] == KEY_PRESSED)
      {
        MIDI.sendNoteOn(MIDI_BASE + i, 127, MIDI_CHANNEL);    // Send a Note On (pitch MIDI_BASE + i, velo 127 on channel MIDI_CHANNEL)
      }
      if (keys[i] == KEY_RELEASED)
      {
        MIDI.sendNoteOff(MIDI_BASE + i, 0, MIDI_CHANNEL);     // Send a Note Off (pitch MIDI_BASE + i, velo 0 on channel MIDI_CHANNEL)
      }
      */
    }
  }

  for (uint8_t i = 0; i < N_SWITCHES; i++)
  {
    if (scanned_switches[i] != switches[i])
    {
      Serial.print(i);
      Serial.print(' ');
      Serial.println(scanned_switches[i]);
      
      switches[i] = scanned_switches[i];
    }
  }
}

/*
 * Measure timing only after scan of the entire keybed
 * do not send MIDI message during keybed scan, it takes much time to send data
 * 
 * 
 */
//void handle_key_change(char key_state, char previous_key_state, uint8_t key)
void handle_key_change(char key_state, uint8_t key)
{
  //Serial.print(key);
  //Serial.print(' ');
  //Serial.println(key_state);

  if (key_state == KEY_PRESSED)
  {
    Serial.print("NoteOn ");
    Serial.println(key);
    //MIDI.sendNoteOn(MIDI_BASE + key, 127, MIDI_CHANNEL);    // Send a Note On (pitch MIDI_BASE + i, velo 127 on channel MIDI_CHANNEL)
  }
  if (key_state == KEY_RELEASED)
  {
    Serial.print("NoteOff ");
    Serial.println(key);
    //MIDI.sendNoteOff(MIDI_BASE + key, 0, MIDI_CHANNEL);     // Send a Note Off (pitch MIDI_BASE + i, velo 0 on channel MIDI_CHANNEL)
  }
}

void loop()
{
  //scan_keyboard();
  scan_keys();
  scan_switches();

  //show_scanned_keys();
  //show_scanned_switches();
  find_changes();
  //measure_velocity();
  //send_midi_messages();

  // strange behaviour: (?)
  //delay(1000);
}
