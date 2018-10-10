/* Dices (Game)
 * 
 *  Setup: For 1-X players. Each player gets one Blink.
 *         When connected, the Blinks will arrange colors, so no touching Blinks share the same color.
 *
 *  Goal: Roll the dice by a single click on the Blink and roll the heighest number.
 *
 *  End: After all connected Blinks are rolling, the Blinks will stop rolling and compare the result. 
 *       The Blinks with the heighest roll will win.
 *       The game resets after another single click on any Blink.  
 *
 *  Game development by: Benjamin Buchfink (Khaos@khaos-coders.org)
 *
 *  Version 1.0
 */

// Time to forward a value, before the value is reset
#define ANSWER_THRESHOLD 600
// Time to wait for another round of asking for other Blinks rolling state
#define RECHECK_THRESHOLD 2000

// Each value is send X loop-cycles to give other Blinks a better chance to catch it
#define SEND_VALUE_TIMES 3
// Get X times in a row confirmed that all Blinks are rolling
#define MIN_ROLLING_ACK 3

// Faces receive value 0 sometimes, so no message should be 0 (even team index)
#define START_TEAM_INDEX 1

// Phase control messages
#define NOOP 60        // Nothing
#define ASK_ROLLING 61 // Ask others for rolling state
#define RESET_GAME 62  // Reset game field
#define NOT_ROLLING 9  // Blink is not rolling (in Setup-Phase)
#define ROLLING 8      // Blink is rolling

// All possible team colors
Color teamColors[] = {RED, BLUE, YELLOW, GREEN, ORANGE, CYAN, MAGENTA, WHITE};

// Assigned team index
byte teamIndex = START_TEAM_INDEX;

// Value read from face
byte readValue;

// Value to send to all faces
byte valueToSend;
// Number of loop-cycles the value should be send
byte valueToSendTimes;

// Game is in Solo-Mode (no other Blinks connected)
bool isSoloMode = true;
// Like anything isAlone() sometimes fires for no reason, so this need to be confirmed X times in a row
byte isAloneAckTimes = 0;

// The game has multiple phases 
// 0: assigning an unique color
// 1: Blink is rolling dice
// 2: all Blinks have rolled the dice and compare the results now
#define PHASE_SETUP 0
#define PHASE_MEROLL 1
#define PHASE_ALLROLL 2
byte gamePhase = PHASE_SETUP;

// The dice value
byte diceValue;

// Slow dice rotation down (for animation of rolling)
#define NEXT_VALUE_DURATION 100
Timer nextValueTimer;

// rolling-state propagation 
bool isAnswerRequested = false;
Timer sendAnswerTimer;
byte heighestAnswer;

// all-rolling state and confirmation counter
bool areAllRolling = false;
byte allRollingAckTimes = 0;

// recheck rolling-state timer
Timer recheckTimer;
bool isRecheckOn = false;

// result animation
byte heighestRoll;
bool hasLost = false;

// Setup Blink
void setup() {
  reset();
}

// Main game loop
void loop() {

  // When a value needs to send multiple times
  if (valueToSendTimes > 0) {
    setValueSentOnAllFaces(valueToSend);
    valueToSendTimes--;
    return;
  }

  // Nothing to send (jet)
  valueToSend = NOOP;

  // Read all faces
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      readValue = getLastValueReceivedOnFace(f);

      // Zero => No Blink connected
      if (readValue == 0) {
        loop;
      }

      // When anything was received, this can't be solo mode ;)
      isSoloMode = false;

      // NOOP => Nothing new
      if (readValue == NOOP) {
        loop;
      }

      // Reset game
      if (readValue == RESET_GAME && gamePhase != PHASE_SETUP) {
        reset();
        // Propagate reset to other Blinks
        sendValue(RESET_GAME);
        break;
      }

      // Receive rolled dice values from other Blinks
      if (gamePhase == PHASE_ALLROLL && readValue < 7) {
        // remember heighest roll, to propagate it
        if (readValue > heighestRoll) {
          heighestRoll = readValue;
        }
        // compare received roll with own
        if (readValue > diceValue) {
          hasLost = true;
        }
      }

      // When Blink is still rolling, but receiving dice values, then the other Blinks are in the final phase
      if (gamePhase == PHASE_MEROLL && readValue < 7) {
        areAllRolling=true;
      }
      
      // ASK_ROLLING => Forward request
      if (readValue == ASK_ROLLING && !isAnswerRequested) {
        startAnswerTimer();
        // Propagate the request
        sendValue(ASK_ROLLING);
        break;
      }

      // NOT_ROLLING, ROLLING => Propagate the value (if not done so before)
      if (readValue == NOT_ROLLING || readValue == ROLLING) {
        // always forware the heighest value (NOT_ROLLING > ROLLING)
        if (readValue > heighestAnswer) {
          heighestAnswer = readValue;
          sendValue(heighestAnswer);
        }
        areAllRolling = (heighestAnswer == ROLLING);
        loop;
      }

      if (gamePhase == PHASE_SETUP) {
        // If same color, select next free
        if (readValue == (teamIndex + 10)) {
          // let randomness decide which Blink will change its color
          if (rand(10) > 9) {
            teamIndex++;
            // When the maximum team color is reached, select the first again
            if(teamIndex > COUNT_OF(teamColors)) {
              teamIndex = START_TEAM_INDEX;      
            }
          }
        }
      }
    }
  }

  // In single-mode "all" Blinks are rolling
  if (isSoloMode && gamePhase == PHASE_MEROLL) {
    areAllRolling = true;
  }
  
  // Reset rolling ack counter, when at least one is not rolling
  if (heighestAnswer > 0 && !areAllRolling) {
    allRollingAckTimes=0;
  }

  // Stop sending answers if it was send long enouht
  if (isAnswerRequested && sendAnswerTimer.isExpired()) {
    isAnswerRequested = false;
    heighestAnswer = 0;
    valueToSend = NOOP;
    // Send requests again after some time
    isRecheckOn = true;
    recheckTimer.set(RECHECK_THRESHOLD);
    // Rolling ack counter
    if (areAllRolling) {
      allRollingAckTimes++;
    }
    areAllRolling = false;
  }
  // If there was an answer requested => send current rolling state or known state of other Blinks
  else if (isAnswerRequested && valueToSend == NOOP) {
    if (gamePhase == PHASE_SETUP) {
      sendValue(NOT_ROLLING);
    } else if (heighestAnswer > 0) {
      sendValue(heighestAnswer);
    } else if (gamePhase == PHASE_MEROLL) {
      sendValue(ROLLING);
    }
    valueToSendTimes = SEND_VALUE_TIMES;
  }
  
  // When Blick is removed from the field => reset it
  if (isAlone() && !isSoloMode) {
    isAloneAckTimes++;
    if (isAloneAckTimes >= MIN_ROLLING_ACK) {
      reset();
    }
  } else {
    isAloneAckTimes = 0;
  }

  // On button click
  if (buttonSingleClicked()) {
    if (gamePhase == PHASE_SETUP) {
      // Start rolling phase
      gamePhase = PHASE_MEROLL;
      askRollingState();
    }
    else if (gamePhase == PHASE_ALLROLL) {
      // Reset game on all connected Blinks
      reset();
      sendValue(RESET_GAME);
    }
  }

  // If recheck was activated
  if (isRecheckOn && recheckTimer.isExpired()) {
    askRollingState();
  }

  // -------------------
  // The game phases
  // -------------------
  if (gamePhase == PHASE_SETUP) {
    // show own color
    setColor(teamColors[teamIndex - START_TEAM_INDEX]);
    if (valueToSend == NOOP) {
      // send team index
      valueToSend = teamIndex + 10;
    }
  }
  else if (gamePhase == PHASE_MEROLL) {
    // Roll the dice after timer expired
    if(nextValueTimer.isExpired()) {
      nextRandomValue();
    }
    // If all Blinks are rolling => compare results
    if (allRollingAckTimes >= MIN_ROLLING_ACK) {
      gamePhase = PHASE_ALLROLL;
      heighestRoll = diceValue;
    }
    if (valueToSend == NOOP) {
      // send me rolling
      valueToSend = ROLLING;
    }
  }
  else if (gamePhase == PHASE_ALLROLL) {
    // Display rolled value (pulsing if won)
    byte rands[] = {0,1,2};
    Color color = teamColors[teamIndex - START_TEAM_INDEX];
    if (!hasLost) {
      // have the color on the Blink raise and lower to feel more alive
      byte bri = 200 + 55 * sin_d( (millis()/10) % 360);
      color=dim(color, bri);
    } else {
      color=dim(color,50);
    }
    displayDiceValue(diceValue, color, rands);
    valueToSend = heighestRoll;
  }

  // Send the desired value to other Blinks
  setValueSentOnAllFaces(valueToSend);
}

// Sets the value that will be send at the end of the loop-cycle
void sendValue(byte value) {
  valueToSend=value;
  valueToSendTimes = SEND_VALUE_TIMES;
}

// Sends a request to all Blinks for their rolling-state
void askRollingState() {
  startAnswerTimer();
  sendValue(ASK_ROLLING);
}

// Rolls the dice and displays its value
void nextRandomValue() {
  // Select a random number (1-6)
  diceValue=rand(5) + 1;
   
  // Display the dice
  byte randsNeeded = diceValue;
  if (randsNeeded > 3) {
    randsNeeded = 6-randsNeeded;
  }
  byte rands[randsNeeded];
  getUniqueRands(rands, randsNeeded);
  displayDiceValue(diceValue, teamColors[teamIndex - START_TEAM_INDEX], rands);
  
  // set timer for next random value
  nextValueTimer.set(NEXT_VALUE_DURATION);  
}

// get X unique random numbers (0-5)
void getUniqueRands(byte rands[], byte count) {
  bool unique = true;
  for(byte i=0; i<count; i++) {
    rands[i] = rand(5);
    unique = true;
    for(int j=0; j<i; j++) {
      if (rands[i] == rands[j]) {
        unique=false;
        break;
      }
    }
    if (!unique) {
      i--;
    }
  }
}

// Display a dice value through the LEDs on the Blink
void displayDiceValue(byte value, Color color, byte rands[]) {
  setColor(OFF);
  if (value == 6) {
    // 6
    setColor(color);
  }
  else if (value <= 3) {
    // 1-3 
    for(byte i=0;i<value;i++) {
      setFaceColor(rands[i], color);
    }
  } 
  else {
    // 4-5
    bool ok;
    FOREACH_FACE(f) {
      ok=true;
      for(int i=0;i<6-value;i++) {
        if (f == rands[i]) {
          ok=false;
          break;
        }
      }
      if (ok) {
        setFaceColor(f, color);   
      }
    }
  }
}

// Start timer which resets the answer cache
void startAnswerTimer() {
  if (!isAnswerRequested) {
    isAnswerRequested = true;
    heighestAnswer = 0;
    sendAnswerTimer.set(ANSWER_THRESHOLD);
  }
}

// Reset the game
void reset() {
  gamePhase = PHASE_SETUP;
  isSoloMode = true;
  isAloneAckTimes = 0;
  areAllRolling = false;
  isAnswerRequested = false;
  heighestAnswer = 0;
  heighestRoll = 0;
  isRecheckOn = false;
  allRollingAckTimes = 0;
  hasLost = false;
}

// Sin in degrees ( standard sin() takes radians )
float sin_d(uint16_t degrees) {
  return sin( (degrees / 360.0F) * 2.0F * PI);
}
