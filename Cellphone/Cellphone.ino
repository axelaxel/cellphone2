/*

(C) Copyright 2012-2013 Massachusetts Institute of Technology

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
de
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*
2014 07 25 Fabrica
*/

#include <GSM.h>
#include <GSM3ShieldV1VoiceProvider.h>
#include <GSM3Serial1.h>

#include <PhoneBook.h>
#include <GSM3ClockService.h>
#include <GSM3VolumeService.h>
#include <GSM3DTMF.h>

#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#include <Keypad.h>

#include "pitches.h"

int ringTone[] =          { NOTE_E5, NOTE_D5, NOTE_F4, NOTE_G4, NOTE_C5, NOTE_B5, NOTE_D4, NOTE_E4, NOTE_B5, NOTE_A5, NOTE_C4, NOTE_E4, NOTE_A5, 0 };
int ringToneDurations[] = { 250,     250,     500,     500,     250,     250,     500,     500,     250,     250,     500,     500,     1000,    1000 };
int ringToneIndex;

GSM gsmAccess(true);
GSMVoiceCall vcs(false);
GSM_SMS sms(false);
GSMScanner scannerNetworks;
GSM3ClockService clock;
GSM3VolumeService volume;
GSM3DTMF dtmf;
GSMPIN PINManager;
PhoneBook pb;

#define SCREEN_WIDTH 14
#define ENTRY_SIZE 20

int contrast = 50;

int signalQuality;
unsigned long lastClockCheckTime, lastSMSCheckTime, lastSignalQualityCheckTime, noteStartTime;

Adafruit_PCD8544 screen = Adafruit_PCD8544(16, 15, 14, 12, 13); // SCLK, DIN, D/C, CS, RST 

const byte ROWS = 6;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'!','U','?'},
  {'L','D','R'},
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};  
//byte rowPins[ROWS] = {25, 27, 28, 29, 30, 31}; // Original Version
//byte colPins[COLS] = {26, 24, 23}; // Original Version
byte rowPins[ROWS] = {22, 23, 31, 0, 1, 2}; // Fabrica Version
byte colPins[COLS] = {21, 20, 18}; // Fabrica Version

Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

int pwrkey = 2;

int x = 0, y = 0;

char number[20];
char name[20];
char mypin[5];

#define NAME_OR_NUMBER() (name[0] == 0 ? (number[0] == 0 ? "Unknown" : number) : name)

int missed = 0;
DateTime missedDateTime;

GSM3_voiceCall_st prevVoiceCallStatus;

enum Mode { NOMODE, TEXTALERT, MISSEDCALLALERT, ALARMALERT, LOCKED, HOME, DIAL, PHONEBOOK, EDITENTRY, EDITTEXT, MENU, MISSEDCALLS, RECEIVEDCALLS, DIALEDCALLS, TEXTS, SETTIME, SETALARM,ENTERSIMPIN,REGISTERING,NOT_CONNECTED };
Mode mode = LOCKED, prevmode, backmode = mode, interruptedmode = mode, alarminterruptedmode = mode;
boolean initmode, back, fromalert;

struct menuentry_t {
  char *name;
  Mode mode;
  void (*f)();
};

menuentry_t mainmenu[] = {
  { "Missed calls", MISSEDCALLS, 0 },
  { "Received calls", RECEIVEDCALLS, 0 },
  { "Dialed calls", DIALEDCALLS, 0 },
  { "Set date+time", SETTIME, 0 },
  { "Set alarm", SETALARM, 0 },
};

menuentry_t phoneBookEntryMenu[] = {
  { "Call", PHONEBOOK, callPhoneBookEntry },
  { "Text", EDITTEXT, initTextFromPhoneBookEntry },
  { "Add entry", EDITENTRY, initEditEntry },
  { "Edit", EDITENTRY, initEditEntryFromPhoneBookEntry },
  { "Delete", PHONEBOOK, deletePhoneBookEntry }
};

menuentry_t callLogEntryMenu[] = {
  { "Call", MISSEDCALLS, callPhoneBookEntry },
  { "Save number", EDITENTRY, initEditEntryFromCallLogEntry },
  { "Delete", MISSEDCALLS, deleteCallLogEntry }
};

menuentry_t *menu;

int menuLength;
int menuLine;

const int NUMPHONEBOOKLINES = 5;
int phoneBookIndices[NUMPHONEBOOKLINES];
char phoneBookNames[NUMPHONEBOOKLINES][ENTRY_SIZE];
char phoneBookNumbers[NUMPHONEBOOKLINES][ENTRY_SIZE];
boolean phoneBookHasDateTime[NUMPHONEBOOKLINES];
DateTime phoneBookDateTimes[NUMPHONEBOOKLINES];
int phoneBookSize;
int phoneBookIndexStart; // inclusive
int phoneBookIndexEnd; // exclusive
int phoneBookLine;
int phoneBookPage;

long phoneBookCache[256];
int phoneBookCacheSize;

int entryIndex;
char entryName[ENTRY_SIZE], entryNumber[ENTRY_SIZE];
enum EntryField { NAME, NUMBER };
EntryField entryField;

char text[161];
int textline;

char uppercase[10][10] = { 
  { '.', '?', ',', '\'', '!', '0', 0 },
  { ' ', '1', 0 },
  { 'A', 'B', 'C', '2', 0 },
  { 'D', 'E', 'F', '3', 0 },
  { 'G', 'H', 'I', '4', 0 },
  { 'J', 'K', 'L', '5', 0 },
  { 'M', 'N', 'O', '6', 0 },
  { 'P', 'Q', 'R', 'S', '7', 0 },
  { 'T', 'U', 'V', '8', 0 },
  { 'W', 'X', 'Y', 'Z', '9', 0 },
};

char lowercase[10][10] = { 
  { '.', '?', ',', '\'', '!', '0', 0 },
  { ' ', '1', 0 },
  { 'a', 'b', 'c', '2', 0 },
  { 'd', 'e', 'f', '3', 0 },
  { 'g', 'h', 'i', '4', 0 },
  { 'j', 'k', 'l', '5', 0 },
  { 'm', 'n', 'o', '6', 0 },
  { 'p', 'q', 'r', 's', '7', 0 },
  { 't', 'u', 'v', '8', 0 },
  { 'w', 'x', 'y', 'z', '9', 0 },
};

char (*letters)[10];

char lastKey;
int lastKeyIndex;
unsigned long lastKeyPressTime;
boolean shiftNextKey;

int setTimeField;
int setTimeValues[7]; // month, day, year (tens), year (ones), hour, minute (tens), minute (ones)
int setTimeMin[7] = {  1,  1, 0, 0,  1, 0, 0 };
int setTimeMax[7] = { 12, 31, 9, 9, 24, 5, 9 };
char *setTimeSeparators[7] = { "/", "/", "", " ", ":", "", "" };

boolean alarmSetTemp, alarmSet = false;
DateTime alarmTime; // note: date isn't used.

boolean unlocking, blank;

boolean auth = false;
// save input in serial by user
String user_input = "";

void setup() {
  Serial.begin(9600);
  PINManager.begin();

  // turn on display  
  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);
  delay(250);
  digitalWrite(17, LOW);
  delay(200);
  digitalWrite(17, HIGH);
  delay(150);
  digitalWrite(17, LOW);
  delay(50);
  digitalWrite(17, HIGH);
  
  screen.begin();
  screen.setContrast(contrast);
  screen.clearDisplay();
  
  
  
  //screen.setCursor(0,0);
  welcomeScreen();
  //drawRocket();
  
  
  //screen.display();
  
  delay(2000);
  screen.clearDisplay();
  mode = NOT_CONNECTED;
  
}

void loop() {

  if (mode == NOT_CONNECTED || mode == ENTERSIMPIN) 
  {
    Serial.println("not_connected");
    if (mode != REGISTERING)
    {
      
      mode = ENTERSIMPIN;
      //Serial.println("entersim");
      screen.setCursor(0,0);
      screen.setTextColor(BLACK,WHITE);
     //int pin_query = PINManager.isPIN();
     //screen.print("(isPIN");screen.print(pin_query);screen.print(")");
     screen.println(" SIM PIN"); 
      screen.println("eingeben:");
      char key = keypad.getKey();
      screen.println(key);
      //Serial.println("key");
      //Serial.println(key);
      numberInput(key, mypin, sizeof(mypin));
      softKeys("---", "ok");
      if (key == 'L') {
          
        }
        if (key == 'R') {
          screen.println("input done");
          screen.println(mypin);
          validateSimPin();
        }
     
     
     
     
     
     
      //screen.print("inPIN:");screen.println(pin_query);  
      
      
    }
  }
  else 
  {
  if (vcs.getvoiceCallStatus() == IDLE_CALL && mode == LOCKED) digitalWrite(17, LOW);
  else digitalWrite(17, HIGH);
  
  if (vcs.getvoiceCallStatus() != RECEIVINGCALL && (vcs.getvoiceCallStatus() != IDLE_CALL || mode != ALARMALERT)) noTone(4);
  
  char key = keypad.getKey();
  screen.clearDisplay();
  screen.setCursor(0, 0);

  if (millis() - lastClockCheckTime > 60000) {
    DateTime datetime;
    do {
      datetime = clock.getDateTime();
      clock.checkTime();
      while (!clock.ready());
    } while (datetime != clock.getDateTime());
    lastClockCheckTime = millis();
  }
  
  screen.setTextColor(BLACK);
  
  GSM3_voiceCall_st voiceCallStatus = vcs.getvoiceCallStatus();
  switch (voiceCallStatus) {
    case IDLE_CALL:
      if (mode != MISSEDCALLALERT && prevmode != MISSEDCALLALERT && mode != LOCKED && mode != TEXTALERT && mode != ALARMALERT && missed > 0) {
        interruptedmode = mode;
        mode = MISSEDCALLALERT;
      }
    
      if (mode != TEXTALERT && prevmode != TEXTALERT && mode != LOCKED && mode != MISSEDCALLALERT && mode != ALARMALERT && millis() - lastSMSCheckTime > 10000) {
        lastSMSCheckTime = millis();
        sms.available();
        while (!sms.ready());
        if (sms.ready() == 1) {
          interruptedmode = mode;
          mode = TEXTALERT;
        }
      }
      
      if (mode != ALARMALERT && prevmode != ALARMALERT && alarmSet && clock.getHour() == alarmTime.hour && clock.getMinute() == alarmTime.minute) {
        alarminterruptedmode = mode;
        mode = ALARMALERT;
      }

      if ((mode == HOME || (mode == LOCKED && !unlocking)) && millis() - lastSignalQualityCheckTime > 30000) {
        signalQuality = scannerNetworks.getSignalStrength().toInt();
        lastSignalQualityCheckTime = millis();
      }

      initmode = (mode != prevmode) && !back;
      back = false;
      prevmode = mode;
      
      if (mode == HOME || (mode == LOCKED && unlocking) || mode == ALARMALERT) {        
        screen.setTextColor(WHITE, BLACK);
        screen.print("     ");

        
//        screen.print(clock.getMonth());
//        screen.print("/");
//        screen.print(clock.getDay());
//        screen.print("/");
//        if (clock.getYear() < 10) screen.print('0');
//        screen.print(clock.getYear());
//
//        //screen.print(" ");
//        if (clock.getMonth() < 10) screen.print(' ');
//        if (clock.getDay() < 10) screen.print(' ');
        if (clock.getHour() < 10) screen.print(' ');
        screen.print(clock.getHour());
        screen.print(":");
        if (clock.getMinute() < 10) screen.print('0');
        screen.print(clock.getMinute());
        screen.print("    ");
        
        screen.setTextColor(BLACK);

        if (signalQuality != 99)
           for (int i = 1; i <= (signalQuality + 4) / 6; i++)
             screen.drawFastVLine(i, 7 - i, i, WHITE);
             
        long bandGap = 0;
        for (int i = 0; i < 100; i++) bandGap += readBandGap();
        int voltage = (1023L * 110 * 10 / (bandGap / 10));
        
        screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 0, 3, WHITE); // top of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 6, 1, 6, WHITE); // left side of battery
        screen.drawFastVLine(SCREEN_WIDTH * 6 - 2, 1, 6, WHITE); // right side of battery
        for (int i = 0; i < constrain((voltage - 330) / 14, 0, 6); i++) {
          screen.drawFastHLine(SCREEN_WIDTH * 6 - 5, 6 - i, 3, WHITE);
        }
      }

      if (mode == MISSEDCALLALERT) {
        screen.print("Missed: ");
        screen.println(missed);
        screen.println("Last from: ");
        screen.println(NAME_OR_NUMBER());
        screen.print(missedDateTime);
        softKeys("close", "call");
        
        if (key == 'L') {
          missed = 0;
          mode = interruptedmode;
        }
        if (key == 'R') {
          missed = 0;
          mode = interruptedmode;
          vcs.voiceCall(number);
          //while (!vcs.ready());
        }
      } else if (mode == TEXTALERT) {
        if (initmode) {
          sms.remoteNumber(number, sizeof(number));
          name[0] = 0;
          int i = 0;
          for (; i < sizeof(text) - 1; i++) {
            int c = sms.read();
            if (!c) break;
            text[i] = c;
          }
          text[i] = 0;
          textline = 0;
          sms.flush(); // XXX: should save to read message store, not delete
        }
        
        if (name[0] == 0) phoneNumberToName(number, name, sizeof(name) / sizeof(name[0]));
        
        screen.print(NAME_OR_NUMBER());
        screen.println(":");
        
        for (int i = textline * SCREEN_WIDTH; i < textline * SCREEN_WIDTH + 56; i++) {
          if (!text[i]) break;
          screen.print(text[i]);
        }
        
        softKeys("close", "reply");
        
        if (key == 'L') mode = interruptedmode;
        if (key == 'R') {
          text[0] = 0;
          mode = EDITTEXT;
          fromalert = true;
        }
        if (key == 'U') {
          if (textline > 0) textline--;
        }
        if (key == 'D') {
          if (strlen(text) > (textline * SCREEN_WIDTH + 56)) textline++;
        }
      } else if (mode == ALARMALERT) {
        if (initmode) {
          blank = false;
          noteStartTime = 0;
          ringToneIndex = sizeof(ringTone) / sizeof(ringTone[0]) - 1; // index of last note, so we'll continue to the first one
        }
        
        screen.print("Alarm!");
        softKeys("okay");
        if (millis() - noteStartTime > ringToneDurations[ringToneIndex]) {
          ringToneIndex = (ringToneIndex + 1) % (sizeof(ringTone) / sizeof(ringTone[0]));
          noteStartTime = millis();
          if (ringTone[ringToneIndex] == 0) noTone(4);
          else tone(4, ringTone[ringToneIndex]);
        }
        
        if (key == 'L') {
          alarmSet = false;
          mode = alarminterruptedmode;
          back = true;
        }
      } else if (mode == LOCKED) {
        if (initmode) {
          unlocking = false;
          blank = false;
        }
        
        if (unlocking) {
          softKeys("Unlock");
          if (key == 'L') { mode = HOME; unlocking = false; }
          if (key == 'U') { contrast += 5; screen.begin(contrast); lastKeyPressTime = millis(); }
          if (key == 'D') { contrast -= 5; screen.begin(contrast); lastKeyPressTime = millis(); }
          if (millis() - lastKeyPressTime > 3000) unlocking = false;
          blank = false;
        } else {
          if (key) {
            screen.begin(contrast);
            unlocking = true;
            lastKeyPressTime = millis();
          }
          
          if (!blank) {
            screen.display(); // since there's no call to softKeys()
            blank = true;
          }
        }
      } else if (mode == HOME) {
        softKeys("lock", "menu");
        
        if ((key >= '0' && key <= '9') || key == '#') {
          lastKeyPressTime = millis();
          number[0] = key; number[1] = 0;
          mode = DIAL;
        } else if (key == 'L') {
          mode = LOCKED;
        } else if (key == 'R') {
          mode = MENU;
          menu = mainmenu;
          menuLength = sizeof(mainmenu) / sizeof(mainmenu[0]);
          backmode = HOME;
        } else if (key == 'D') {
          mode = PHONEBOOK;
        }
      } else if (mode == DIAL) {
        numberInput(key, number, sizeof(number));
        softKeys("back", "call");
        
        if (key == 'L') {
          mode = HOME;
        } else if (key == 'R') {
          if (strlen(number) > 0) {
            mode = HOME; // for after call ends
            name[0] = 0;
            vcs.voiceCall(number);
            while (!vcs.ready());
          }
        }
      } else if (mode == PHONEBOOK || mode == MISSEDCALLS || mode == RECEIVEDCALLS || mode == DIALEDCALLS) {
        if (initmode) {
          if (mode == PHONEBOOK) pb.selectPhoneBook(PHONEBOOK_SIM);
          if (mode == MISSEDCALLS) pb.selectPhoneBook(PHONEBOOK_MISSEDCALLS);
          if (mode == RECEIVEDCALLS) pb.selectPhoneBook(PHONEBOOK_RECEIVEDCALLS);
          if (mode == DIALEDCALLS) pb.selectPhoneBook(PHONEBOOK_DIALEDCALLS);
          while (!pb.ready());
          delay(300); // otherwise the module gives an error on pb.queryPhoneBook()
          pb.queryPhoneBook();
          while (!pb.ready());
          phoneBookSize = pb.getPhoneBookSize();
          phoneBookPage = 0;
          phoneBookLine = 0;
          phoneBookIndexStart = 1;
          phoneBookIndexEnd = loadphoneBookNamesForwards(phoneBookIndexStart, NUMPHONEBOOKLINES);
          lastKeyPressTime = millis();
        }

        for (int i = 0; i < NUMPHONEBOOKLINES; i++) {
          if (i == phoneBookLine) screen.setTextColor(WHITE, BLACK);
          else screen.setTextColor(BLACK);
          
          if (phoneBookHasDateTime[i] && i == phoneBookLine && (millis() - lastKeyPressTime) % 2000 > 1000) {
            screen.print(phoneBookDateTimes[i]);
            screen.println();
          } else if (strlen(phoneBookNames[i]) > 0) {
            for (int j = 0; j < SCREEN_WIDTH && phoneBookNames[i][j]; j++) screen.print(phoneBookNames[i][j]);
            if (strlen(phoneBookNames[i]) < SCREEN_WIDTH) screen.println();
          } else if (strlen(phoneBookNumbers[i]) > 0) {
            screen.print(phoneBookNumbers[i]);
            if (strlen(phoneBookNumbers[i]) < SCREEN_WIDTH) screen.println();
          } else if (phoneBookIndices[i] != 0) {
            screen.println("Unknown");
          }
        }
        softKeys("back", "okay");
        
        if (key == 'L') mode = HOME;
        else if (key == 'R') {
          backmode = mode;
          if (mode == PHONEBOOK) {
            mode = MENU;
            menu = phoneBookEntryMenu;
            menuLength = sizeof(phoneBookEntryMenu) / sizeof(phoneBookEntryMenu[0]);
          } else {
            mode = MENU;
            menu = callLogEntryMenu;
            menuLength = sizeof(callLogEntryMenu) / sizeof(callLogEntryMenu[0]);
          }
        } else if (key == 'D') {
          if (phoneBookPage * NUMPHONEBOOKLINES + phoneBookLine + 1 < pb.getPhoneBookUsed()) {
            phoneBookLine++;
            if (phoneBookLine == NUMPHONEBOOKLINES) {
              phoneBookPage++;
              phoneBookIndexStart = phoneBookIndexEnd;
              phoneBookIndexEnd = loadphoneBookNamesForwards(phoneBookIndexStart, NUMPHONEBOOKLINES);
              phoneBookLine = 0;
            }
            lastKeyPressTime = millis();
          }
        } else if (key == 'U') {
          if (phoneBookLine > 0 || phoneBookPage > 0) {
            phoneBookLine--;
            if (phoneBookLine == -1) {
              phoneBookPage--;
              phoneBookIndexEnd = phoneBookIndexStart;
              phoneBookIndexStart = loadphoneBookNamesBackwards(phoneBookIndexEnd, NUMPHONEBOOKLINES);
              phoneBookLine = NUMPHONEBOOKLINES - 1;
            }
            lastKeyPressTime = millis();
          }
        }
      } else if (mode == EDITENTRY) {
        if (initmode) entryField = NAME;
        
        screen.println("Name:");
        if (entryField != NAME) screen.println(entryName);
        else textInput(key, entryName, sizeof(entryName));
        
        screen.println("Number:");
        if (entryField != NUMBER) screen.println(entryNumber);
        else numberInput(key, entryNumber, sizeof(entryNumber));
                
        softKeys("cancel", "save");

        if (entryField == NAME) {
          if (key == 'D') entryField = NUMBER;
        }
        
        if (entryField == NUMBER) {
          if (key == 'U') entryField = NAME;
        }
        
        if (key == 'L') {
          mode = backmode;
          back = true;
        }
        if (key == 'R') {
          savePhoneBookEntry(entryIndex, entryName, entryNumber);
          mode = PHONEBOOK;
        }
      } else if (mode == EDITTEXT) {
        textInput(key, text, sizeof(text));
        softKeys("cancel", "send");
        
        if (key == 'L') {
          mode = (fromalert ? HOME : PHONEBOOK);
          back = true;
        } 
        if (key == 'R') {
          sendText(number, text);
          mode = HOME;
          back = true;
        }
      } else if (mode == MENU) {
        if (initmode) menuLine = 0;

        for (int i = 0; i < menuLength; i++) {
          if (menuLine == i) screen.setTextColor(WHITE, BLACK);
          else screen.setTextColor(BLACK);
          screen.print(menu[i].name);
          if (strlen(menu[i].name) % SCREEN_WIDTH != 0) screen.println();
        }
        
        softKeys("back", "okay");
        
        if (key == 'U') {
          if (menuLine > 0) menuLine--;
        } else if (key == 'D') {
          if (menuLine < menuLength - 1) menuLine++;
        } else if (key == 'R') {
          mode = menu[menuLine].mode;
          if (menu[menuLine].f) menu[menuLine].f();
        } else if (key == 'L') {
          mode = backmode;
          back = true;
        }
      } else if (mode == TEXTS) {
        softKeys("back");
        
        if (key == 'L') mode = HOME;
      } else if (mode == SETTIME) {
        if (initmode) {
          setTimeValues[0] = clock.getMonth();
          setTimeValues[1] = clock.getDay();
          setTimeValues[2] = clock.getYear() / 10;
          setTimeValues[3] = clock.getYear() % 10;
          setTimeValues[4] = clock.getHour();
          setTimeValues[5] = clock.getMinute() / 10;
          setTimeValues[6] = clock.getMinute() % 10;
          setTimeField = 0;
        }
        
        screen.println("Date & Time:");
        
        for (int i = 0; i < 7; i++) {
          if (i == setTimeField) screen.setTextColor(WHITE, BLACK);
          screen.print(setTimeValues[i]);
          screen.setTextColor(BLACK);
          screen.print(setTimeSeparators[i]);
        } 
        
        if (setTimeField == 6) softKeys("cancel", "set");
        else softKeys("cancel", "next");
        
        if (key == 'L') mode = HOME;
        
        if (key == 'U') {
          setTimeValues[setTimeField]++;
          if (setTimeValues[setTimeField] > setTimeMax[setTimeField]) setTimeValues[setTimeField] = setTimeMin[setTimeField];
        }
        
        if (key == 'D') {
          setTimeValues[setTimeField]--;
          if (setTimeValues[setTimeField] < setTimeMin[setTimeField]) setTimeValues[setTimeField] = setTimeMax[setTimeField];
        }
        
        if (key == 'R') {
          setTimeField++;
          if (setTimeField == 7) {
            clock.setTime(setTimeValues[2] * 10 + setTimeValues[3], setTimeValues[0], setTimeValues[1],
                          setTimeValues[4], setTimeValues[5] * 10 + setTimeValues[6], 0);
            while (!clock.ready());
            delay(300);
            clock.checkTime();
            while (!clock.ready());
            lastClockCheckTime = millis();
            mode = HOME;
          }
        }
      } else if (mode == SETALARM) {
        if (initmode) {
          setTimeValues[4] = alarmTime.hour;
          setTimeValues[5] = alarmTime.minute / 10;
          setTimeValues[6] = alarmTime.minute % 10;
          alarmSetTemp = alarmSet;
          setTimeField = -1;
        }
        
        screen.println("Set alarm:");
        
        if (setTimeField == -1) screen.setTextColor(WHITE, BLACK);
        if (alarmSetTemp) screen.print("ON");
        else screen.print("OFF");
        screen.setTextColor(BLACK);
        screen.print(" ");
        
        if (alarmSetTemp) {
          for (int i = 4; i < 7; i++) {
            if (i == setTimeField) screen.setTextColor(WHITE, BLACK);
            screen.print(setTimeValues[i]);
            screen.setTextColor(BLACK);
            screen.print(setTimeSeparators[i]);
          }
        }
        
        if ((setTimeField == -1 && alarmSetTemp == false) || setTimeField == 6) softKeys("cancel", "set");
        else softKeys("cancel", "next");
        
        if (key == 'L') mode = HOME;
        
        if (key == 'U') {
          if (setTimeField == -1) {
            alarmSetTemp = !alarmSetTemp;
          } else {
            setTimeValues[setTimeField]++;
            if (setTimeValues[setTimeField] > setTimeMax[setTimeField]) setTimeValues[setTimeField] = setTimeMin[setTimeField];
          }
        }
        
        if (key == 'D') {
          if (setTimeField == -1) {
            alarmSetTemp = !alarmSetTemp;
          } else {
            setTimeValues[setTimeField]--;
            if (setTimeValues[setTimeField] < setTimeMin[setTimeField]) setTimeValues[setTimeField] = setTimeMax[setTimeField];
          }
        }
        
        if (key == 'R') {
          if (setTimeField == -1) {
            if (alarmSetTemp) setTimeField = 4;
            else {
              alarmSet = alarmSetTemp;
              mode = HOME;
            }
          } else {
            setTimeField++;
            if (setTimeField == 7) {
              alarmSet = alarmSetTemp;
              alarmTime.hour = setTimeValues[4];
              alarmTime.minute = setTimeValues[5] * 10 + setTimeValues[6];
              mode = HOME;
            }
          }
        }
      }
      break;
      
    case CALLING:
      //if (name[0] == 0) phoneNumberToName(number, name, sizeof(name) / sizeof(name[0]));
      
      screen.println("Calling:");
      screen.print(NAME_OR_NUMBER());
      softKeys("end");
      
      if (key == 'U' || key == 'D') {
        volume.checkVolume();
        if (checkForCommandReady(volume, 500) && volume.ready() == 1) {
          volume.setVolume(constrain(volume.getVolume() + (key == 'U' ? 5 : -5), 0, 100));
        }
      }
      
      if (key == 'L') {
        vcs.hangCall();
        while (!vcs.ready());
      }
      break;
      
    case RECEIVINGCALL:
      if (prevVoiceCallStatus != RECEIVINGCALL) {
        blank = false;
        missed++;
        name[0] = 0;
        number[0] = 0;
        noteStartTime = 0;
        ringToneIndex = sizeof(ringTone) / sizeof(ringTone[0]) - 1; // index of last note, so we'll continue to the first one
        
      }
      missedDateTime.year = clock.getYear(); missedDateTime.month = clock.getMonth(); missedDateTime.day = clock.getDay();
      missedDateTime.hour = clock.getHour(); missedDateTime.minute = clock.getMinute(); missedDateTime.second = clock.getSecond();
      if (strlen(number) == 0) vcs.retrieveCallingNumber(number, sizeof(number));
      if (strlen(number) > 0 && name[0] == 0) {
        phoneNumberToName(number, name, sizeof(name) / sizeof(name[0]));
      }
      screen.println("incoming:");
      screen.println(NAME_OR_NUMBER());
      softKeys("end", "answer");
      if (millis() - noteStartTime > ringToneDurations[ringToneIndex]) {
        ringToneIndex = (ringToneIndex + 1) % (sizeof(ringTone) / sizeof(ringTone[0]));
        noteStartTime = millis();
        if (ringTone[ringToneIndex] == 0) noTone(4);
        else tone(4, ringTone[ringToneIndex]);
      }
      if (key == 'L') {
        missed--;
        vcs.hangCall();
        while (!vcs.ready());
      }
      if (key == 'R') {
        missed--;
        vcs.answerCall();
        while (!vcs.ready());
      }
      break;
      
    case TALKING:
      screen.println("Connected:");
      screen.println(NAME_OR_NUMBER());
      softKeys("end");
      
      if ((key >= '0' && key <= '9') || key == '#' || key == '*') dtmf.tone(key);
      
      if (key == 'U' || key == 'D') {
        volume.checkVolume();
        if (checkForCommandReady(volume, 500) && volume.ready() == 1) {
          volume.setVolume(constrain(volume.getVolume() + (key == 'U' ? 5 : -5), 0, 100));
        }
      }
      
      if (key == 'L') {
        vcs.hangCall();
        while (!vcs.ready());
      }
      break;
  }
  
  prevVoiceCallStatus = voiceCallStatus;
}
}

boolean checkForCommandReady(GSM3ShieldV1BaseProvider &provider, int timeout)
{
  unsigned long commandStartTime = millis();
  
  while (millis() - commandStartTime < timeout) {
    if (provider.ready()) return true;
  }
  
  return false;
}

void initEditEntry()
{
  entryIndex = 0;
  entryName[0] = 0;
  entryNumber[0] = 0;
}

void initEditEntryFromCallLogEntry()
{
  entryIndex = 0;
  strcpy(entryName, phoneBookNames[phoneBookLine]);
  strcpy(entryNumber, phoneBookNumbers[phoneBookLine]);
}

void initEditEntryFromPhoneBookEntry()
{
  entryIndex = phoneBookIndices[phoneBookLine];
  strcpy(entryName, phoneBookNames[phoneBookLine]);
  strcpy(entryNumber, phoneBookNumbers[phoneBookLine]);
}

void initTextFromPhoneBookEntry() {
  strcpy(number, phoneBookNumbers[phoneBookLine]);
  text[0] = 0;
  fromalert = false;
}

void sendText(char *number, char *text)
{
  sms.beginSMS(number);
  for (; *text; text++) sms.write(*text);
  sms.endSMS();
  
  while (!sms.ready());
}

void callPhoneBookEntry() {
  vcs.voiceCall(phoneBookNumbers[phoneBookLine]);
  while (!vcs.ready());
  strcpy(number, phoneBookNumbers[phoneBookLine]);
  strcpy(name, phoneBookNames[phoneBookLine]);
}

void deletePhoneBookEntry() {
  pb.deletePhoneBookEntry(phoneBookIndices[phoneBookLine]);
  phoneBookCache[phoneBookIndices[phoneBookLine]] = 0;
}

void deleteCallLogEntry() {
  pb.deletePhoneBookEntry(phoneBookIndices[phoneBookLine]);
}

boolean savePhoneBookEntry(int index, char *name, char *number) {
  pb.selectPhoneBook(PHONEBOOK_SIM);
  while (!pb.ready());
  delay(300); // otherwise the module may give an error when accessing the phonebook.
  if (index == 0) {
    // search for an possible empty phone book entry by looking for a cached hash of 0
    for (int i = 1; i < phoneBookCacheSize; i++) {
      if (!phoneBookCache[i]) {
        pb.readPhoneBookEntry(i);
        while (!pb.ready());
        
        // if the entry is really empty, save the new entry there
        if (!pb.gotNumber) {
          pb.writePhoneBookEntry(i, number, name);
          while (!pb.ready());
          phoneBookCache[i] = hashPhoneNumber(number);
          break;
        }
      }
    }
  } else {
    pb.writePhoneBookEntry(index, number, name);
    while (!pb.ready());
    if (pb.ready() == 1) phoneBookCache[index] = hashPhoneNumber(number);
  }
  
  return true;
}

void cachePhoneBook()
{
  int type;
  
  pb.queryPhoneBook();
  while (!pb.ready());
  type = pb.getPhoneBookType();
  
  if (type != PHONEBOOK_SIM) {
    pb.selectPhoneBook(PHONEBOOK_SIM);
    while (!pb.ready());
    delay(300);
    pb.queryPhoneBook();
    while (!pb.ready());
  }
  
  // the phone book entries start at 1, so the size of the cache is one more than the size of the phone book.  
  phoneBookCacheSize = min(pb.getPhoneBookSize() + 1, sizeof(phoneBookCache) / sizeof(phoneBookCache[0]));
  for (int i = 1; i < phoneBookCacheSize; i++) {
    pb.readPhoneBookEntry(i);
    while (!pb.ready());
    if (pb.gotNumber) {
      phoneBookCache[i] = hashPhoneNumber(pb.number);
    }
  }
  
  if (type != PHONEBOOK_SIM) {
    pb.selectPhoneBook(type);
    while (!pb.ready());
  }
}

long hashPhoneNumber(char *s)
{
  long l = 0;
  boolean firstDigit = true;
  for (; *s; s++) {
    if ((*s) >= '0' && (*s) <= '9') {
      if (firstDigit) {
        firstDigit = false;
        if (*s == '1') continue; // skip U.S. country code
      }
      l = l * 10 + (*s) - '0';
    }
  }
  return l;
}

// return true on success, false on failure.
boolean phoneNumberToName(char *number, char *name, int namelen)
{
  if (number[0] == 0) return false;
  
  long l = hashPhoneNumber(number);
  
  for (int i = 1; i < 256; i++) {
    if (l == phoneBookCache[i]) {
      boolean success = false;
      int type;
      
      pb.queryPhoneBook();
      while (!pb.ready());
      type = pb.getPhoneBookType();
      
      if (type != PHONEBOOK_SIM) {
        pb.selectPhoneBook(PHONEBOOK_SIM);
        while (!pb.ready());
        delay(300);
      }
      
      pb.readPhoneBookEntry(i);
      if (checkForCommandReady(pb, 500) && pb.gotNumber) {
        strncpy(name, pb.name, namelen);
        name[namelen - 1] = 0;
        success = true;
      }
      
      if (type != PHONEBOOK_SIM) {
        pb.selectPhoneBook(type);
        while (!pb.ready());
        delay(300);
      }
      
      return success;
    }
  }
  
  return false;
}

int loadphoneBookNamesForwards(int startingIndex, int n)
{
  int i = 0;
  
  if (pb.getPhoneBookUsed() > 0) {
    for (; startingIndex <= phoneBookSize; startingIndex++) {
      pb.readPhoneBookEntry(startingIndex);
      while (!pb.ready());
      if (pb.gotNumber) {
        phoneBookIndices[i] = startingIndex;
        strncpy(phoneBookNames[i], pb.name, ENTRY_SIZE);
        phoneBookNames[i][ENTRY_SIZE - 1] = 0;
        strncpy(phoneBookNumbers[i], pb.number, ENTRY_SIZE);
        phoneBookNumbers[i][ENTRY_SIZE - 1] = 0;
        phoneBookHasDateTime[i] = pb.gotTime;
        if (pb.gotTime) memcpy(&phoneBookDateTimes[i], &pb.datetime, sizeof(DateTime));
        if (pb.getPhoneBookType() != PHONEBOOK_SIM) {
          phoneBookNames[i][0] = 0; // the names in the call logs are never accurate (but sometimes present)
          phoneNumberToName(phoneBookNumbers[i], phoneBookNames[i], ENTRY_SIZE); // reuses / overwrites pb
        }
        if (++i == n) break; // found a full page of entries
        if (phoneBookPage * NUMPHONEBOOKLINES + i == pb.getPhoneBookUsed()) break; // hit end of the phonebook
      }
    }
  }
  
  for (; i < n; i++) {
    phoneBookIndices[i] = 0;
    phoneBookNames[i][0] = 0;
    phoneBookNumbers[i][0] = 0;
    phoneBookHasDateTime[i] = false;
  }
  
  return startingIndex + 1;
}

int loadphoneBookNamesBackwards(int endingIndex, int n)
{
  int i = n - 1;
  for (endingIndex--; endingIndex >= 0; endingIndex--) {
    pb.readPhoneBookEntry(endingIndex);
    while (!pb.ready());
    if (pb.gotNumber) {
      phoneBookIndices[i] = endingIndex;
      strncpy(phoneBookNames[i], pb.name, ENTRY_SIZE);
      phoneBookNames[i][ENTRY_SIZE - 1] = 0;
      strncpy(phoneBookNumbers[i], pb.number, ENTRY_SIZE);
      phoneBookNumbers[i][ENTRY_SIZE - 1] = 0;
      phoneBookHasDateTime[i] = pb.gotTime;
      if (pb.gotTime) memcpy(&phoneBookDateTimes[i], &pb.datetime, sizeof(DateTime));
      if (pb.getPhoneBookType() != PHONEBOOK_SIM) {
        phoneBookNames[i][0] = 0; // the names in the call logs are never accurate (but sometimes present)
        phoneNumberToName(phoneBookNumbers[i], phoneBookNames[i], ENTRY_SIZE); // reuses / overwrites pb
      }
      if (--i == -1) break; // found four entries
    }
  }
  for (; i >= 0; i--) {
    phoneBookIndices[i] = 0;
    phoneBookNames[i][0] = 0;
    phoneBookNumbers[i][0] = 0;
    phoneBookHasDateTime[i] = false;
  }
  return endingIndex;
}

void numberInput(char key, char *buf, int len)
{
  int i = strlen(buf);
  
  if (i > 0 && (buf[i - 1] == '*' || buf[i - 1] == '#' || buf[i - 1] == '+') && millis() - lastKeyPressTime <= 1000) {
    for (int j = 0; j < strlen(buf) - 1; j++) screen.print(buf[j]);
    screen.setTextColor(WHITE, BLACK);
    screen.print(buf[strlen(buf) - 1]);
    screen.setTextColor(BLACK);
  } else {
    screen.print(buf);
    screen.setTextColor(WHITE, BLACK);
    screen.print(" ");
    screen.setTextColor(BLACK);
  }
  
  if (key >= '0' && key <= '9') {
    if (i < len - 1) { buf[i] = key; buf[i + 1] = 0; }
  }
  if (key == '*') {
    if (i > 0) { buf[i - 1] = 0; }
  }
  if (key == '#') {
    if (i > 0 && (buf[i - 1] == '*' || buf[i - 1] == '#' || buf[i - 1] == '+') && millis() - lastKeyPressTime <= 1000) {
      lastKeyPressTime = millis();
      if (buf[i - 1] == '#') buf[i - 1] = '*';
      else if (buf[i - 1] == '*') buf[i - 1] = '+';
      else if (buf[i - 1] == '+') buf[i - 1] = '#';
    } else {
      if (i < len - 1) { buf[i] = '#'; buf[i + 1] = 0; lastKeyPressTime = millis(); }
    }
  }
}

void textInput(char key, char *buf, int len)
{
  if (millis() - lastKeyPressTime > 1000) {
    screen.print(buf);
    screen.setTextColor(WHITE, BLACK);
    screen.print(" ");
    screen.setTextColor(BLACK);
  } else {
    for (int i = 0; i < strlen(buf) - 1; i++) screen.print(buf[i]);
    screen.setTextColor(WHITE, BLACK);
    screen.print(buf[strlen(buf) - 1]);
    screen.setTextColor(BLACK);
  }
  screen.println();
  
  if (key >= '0' && key <= '9') {
    if (millis() - lastKeyPressTime > 1000 || key - '0' != lastKey) {
      // append new letter
      lastKeyIndex = 0;
      lastKey = key - '0';
      int i = strlen(buf);
      
      if (i == 0) letters = uppercase;
      else {
        letters = lowercase;
        for (int j = i - 1; j >= 0; j--) {
          if (buf[j] == '.' || buf[j] == '?' || buf[j] == '!') {
            letters = uppercase;
            break;
          } else if (buf[j] != ' ') break;
        }
      }
      
      if (shiftNextKey) {
        if (letters == uppercase) letters = lowercase;
        else letters = uppercase;
        
        shiftNextKey = false;
      }
      
      if (i < len - 1) { buf[i] = letters[lastKey][lastKeyIndex]; buf[i + 1] = 0; }
    } else {
      // cycle previously entered letter
      lastKeyIndex++;
      if (letters[lastKey][lastKeyIndex] == 0) lastKeyIndex = 0; // wrap around
      int i = strlen(buf);
      if (i > 0) { buf[i - 1] = letters[lastKey][lastKeyIndex]; }
    }
    lastKeyPressTime = millis();
  }
  if (key == '*') {
    int i = strlen(buf);
    if (i > 0) { buf[i - 1] = 0; }
    lastKeyPressTime = 0;
    shiftNextKey = false;
  }
  if (key == '#') shiftNextKey = true;
}

void softKeys(char *left)
{
  softKeys(left, "");
}

void softKeys(char *left, char *right)
{
  screen.setCursor(0, 40);
  screen.setTextColor(WHITE, BLACK);
  screen.print(left);
  screen.setTextColor(BLACK);
  for (int i = 0; i < SCREEN_WIDTH - strlen(left) - strlen(right); i++) screen.print(" ");
  screen.setTextColor(WHITE, BLACK);
  screen.print(right);
  screen.display();
}

int readBandGap()
{
  uint8_t low, high;
  
  ADMUX = B01011110; // AVCC reference, right-adjust result, 1.1V input
  
  ADCSRA |= (1 << ADSC);
  
  // ADSC is cleared when the conversion finishes
  while (bit_is_set(ADCSRA, ADSC));
  
  // we have to read ADCL first; doing so locks both ADCL
  // and ADCH until ADCH is read.  reading ADCL second would
  // cause the results of each conversion to be discarded,
  // as ADCL and ADCH would be locked when it completed.
  low  = ADCL;
  high = ADCH;
  
  // combine the two bytes
  return (high << 8) | low;
}
void welcomeScreen()
{
  screen.drawCircle(10,10,10,BLACK);
  screen.drawCircle(30,10,10,BLACK);
  screen.drawCircle(0,0,50,BLACK);
  for (int16_t i=0; i<screen.height(); i+=2) {
    screen.drawCircle(screen.width()/2, screen.height()/2, i, BLACK);
    screen.display();
    delay(100);
    screen.clearDisplay();
  }
  screen.display();
  screen.drawPixel(0, 9, BLACK );
  screen.drawPixel(0, 10, BLACK );
  screen.drawPixel(0, 11, BLACK );
  screen.drawPixel(0, 12, BLACK );
  screen.drawPixel(0, 13, BLACK );
  screen.drawPixel(0, 14, BLACK );
  screen.drawPixel(0, 15, BLACK );
  screen.drawPixel(0, 16, BLACK );
  screen.drawPixel(0, 17, BLACK );
  screen.drawPixel(0, 18, BLACK );
  screen.drawPixel(0, 19, BLACK );
  screen.drawPixel(0, 20, BLACK );
  screen.drawPixel(0, 21, BLACK );
  screen.drawPixel(0, 22, BLACK );
  screen.drawPixel(0, 23, BLACK );
  screen.drawPixel(0, 24, BLACK );
  screen.drawPixel(0, 25, BLACK );
  screen.drawPixel(0, 26, BLACK );
  screen.drawPixel(0, 27, BLACK );
  screen.drawPixel(0, 28, BLACK );
  screen.drawPixel(0, 29, BLACK );
  screen.drawPixel(1, 9, BLACK );
  screen.drawPixel(1, 10, BLACK );
  screen.drawPixel(1, 11, BLACK );
  screen.drawPixel(1, 12, BLACK );
  screen.drawPixel(1, 13, BLACK );
  screen.drawPixel(1, 14, BLACK );
  screen.drawPixel(1, 15, BLACK );
  screen.drawPixel(1, 16, BLACK );
  screen.drawPixel(1, 17, BLACK );
  screen.drawPixel(1, 18, BLACK );
  screen.drawPixel(1, 19, BLACK );
  screen.drawPixel(1, 20, BLACK );
  screen.drawPixel(1, 21, BLACK );
  screen.drawPixel(1, 22, BLACK );
  screen.drawPixel(1, 23, BLACK );
  screen.drawPixel(1, 24, BLACK );
  screen.drawPixel(1, 25, BLACK );
  screen.drawPixel(1, 26, BLACK );
  screen.drawPixel(1, 27, BLACK );
  screen.drawPixel(1, 28, BLACK );
  screen.drawPixel(1, 29, BLACK );
  screen.drawPixel(1, 30, BLACK );
  screen.drawPixel(2, 8, BLACK );
  screen.drawPixel(2, 9, BLACK );
  screen.drawPixel(2, 10, BLACK );
  screen.drawPixel(2, 11, BLACK );
  screen.drawPixel(2, 12, BLACK );
  screen.drawPixel(2, 13, BLACK );
  screen.drawPixel(2, 14, BLACK );
  screen.drawPixel(2, 15, BLACK );
  screen.drawPixel(2, 16, BLACK );
  screen.drawPixel(2, 17, BLACK );
  screen.drawPixel(2, 18, BLACK );
  screen.drawPixel(2, 19, BLACK );
  screen.drawPixel(2, 20, BLACK );
  screen.drawPixel(2, 21, BLACK );
  screen.drawPixel(2, 22, BLACK );
  screen.drawPixel(2, 23, BLACK );
  screen.drawPixel(2, 24, BLACK );
  screen.drawPixel(2, 25, BLACK );
  screen.drawPixel(2, 26, BLACK );
  screen.drawPixel(2, 27, BLACK );
  screen.drawPixel(2, 28, BLACK );
  screen.drawPixel(2, 29, BLACK );
  screen.drawPixel(2, 30, BLACK );
  screen.drawPixel(2, 31, BLACK );
  screen.drawPixel(3, 8, BLACK );
  screen.drawPixel(3, 9, BLACK );
  screen.drawPixel(3, 10, BLACK );
  screen.drawPixel(3, 11, BLACK );
  screen.drawPixel(3, 12, BLACK );
  screen.drawPixel(3, 13, BLACK );
  screen.drawPixel(3, 14, BLACK );
  screen.drawPixel(3, 15, BLACK );
  screen.drawPixel(3, 16, BLACK );
  screen.drawPixel(3, 17, BLACK );
  screen.drawPixel(3, 18, BLACK );
  screen.drawPixel(3, 19, BLACK );
  screen.drawPixel(3, 20, BLACK );
  screen.drawPixel(3, 21, BLACK );
  screen.drawPixel(3, 22, BLACK );
  screen.drawPixel(3, 23, BLACK );
  screen.drawPixel(3, 24, BLACK );
  screen.drawPixel(3, 25, BLACK );
  screen.drawPixel(3, 26, BLACK );
  screen.drawPixel(3, 27, BLACK );
  screen.drawPixel(3, 28, BLACK );
  screen.drawPixel(3, 29, BLACK );
  screen.drawPixel(3, 30, BLACK );
  screen.drawPixel(3, 31, BLACK );
  screen.drawPixel(4, 8, BLACK );
  screen.drawPixel(4, 9, BLACK );
  screen.drawPixel(4, 10, BLACK );
  screen.drawPixel(4, 11, BLACK );
  screen.drawPixel(4, 12, BLACK );
  screen.drawPixel(4, 13, BLACK );
  screen.drawPixel(4, 14, BLACK );
  screen.drawPixel(4, 15, BLACK );
  screen.drawPixel(4, 16, BLACK );
  screen.drawPixel(4, 17, BLACK );
  screen.drawPixel(4, 18, BLACK );
  screen.drawPixel(4, 19, BLACK );
  screen.drawPixel(4, 20, BLACK );
  screen.drawPixel(4, 21, BLACK );
  screen.drawPixel(4, 22, BLACK );
  screen.drawPixel(4, 23, BLACK );
  screen.drawPixel(4, 24, BLACK );
  screen.drawPixel(4, 25, BLACK );
  screen.drawPixel(4, 26, BLACK );
  screen.drawPixel(4, 27, BLACK );
  screen.drawPixel(4, 28, BLACK );
  screen.drawPixel(4, 29, BLACK );
  screen.drawPixel(4, 30, BLACK );
  screen.drawPixel(4, 31, BLACK );
  screen.drawPixel(4, 32, BLACK );
  screen.drawPixel(5, 7, BLACK );
  screen.drawPixel(5, 8, BLACK );
  screen.drawPixel(5, 9, BLACK );
  screen.drawPixel(5, 10, BLACK );
  screen.drawPixel(5, 11, BLACK );
  screen.drawPixel(5, 12, BLACK );
  screen.drawPixel(5, 13, BLACK );
  screen.drawPixel(5, 14, BLACK );
  screen.drawPixel(5, 15, BLACK );
  screen.drawPixel(5, 16, BLACK );
  screen.drawPixel(5, 17, BLACK );
  screen.drawPixel(5, 18, BLACK );
  screen.drawPixel(5, 19, BLACK );
  screen.drawPixel(5, 20, BLACK );
  screen.drawPixel(5, 21, BLACK );
  screen.drawPixel(5, 22, BLACK );
  screen.drawPixel(5, 23, BLACK );
  screen.drawPixel(5, 24, BLACK );
  screen.drawPixel(5, 25, BLACK );
  screen.drawPixel(5, 26, BLACK );
  screen.drawPixel(5, 27, BLACK );
  screen.drawPixel(5, 28, BLACK );
  screen.drawPixel(5, 29, BLACK );
  screen.drawPixel(5, 30, BLACK );
  screen.drawPixel(5, 31, BLACK );
  screen.drawPixel(5, 32, BLACK );
  screen.drawPixel(6, 7, BLACK );
  screen.drawPixel(6, 8, BLACK );
  screen.drawPixel(6, 9, BLACK );
  screen.drawPixel(6, 10, BLACK );
  screen.drawPixel(6, 11, BLACK );
  screen.drawPixel(6, 12, BLACK );
  screen.drawPixel(6, 13, BLACK );
  screen.drawPixel(6, 14, BLACK );
  screen.drawPixel(6, 15, BLACK );
  screen.drawPixel(6, 16, BLACK );
  screen.drawPixel(6, 17, BLACK );
  screen.drawPixel(6, 18, BLACK );
  screen.drawPixel(6, 19, BLACK );
  screen.drawPixel(6, 20, BLACK );
  screen.drawPixel(6, 21, BLACK );
  screen.drawPixel(6, 22, BLACK );
  screen.drawPixel(6, 23, BLACK );
  screen.drawPixel(6, 24, BLACK );
  screen.drawPixel(6, 25, BLACK );
  screen.drawPixel(6, 26, BLACK );
  screen.drawPixel(6, 27, BLACK );
  screen.drawPixel(6, 28, BLACK );
  screen.drawPixel(6, 29, BLACK );
  screen.drawPixel(6, 30, BLACK );
  screen.drawPixel(6, 31, BLACK );
  screen.drawPixel(6, 32, BLACK );
  screen.drawPixel(7, 6, BLACK );
  screen.drawPixel(7, 7, BLACK );
  screen.drawPixel(7, 8, BLACK );
  screen.drawPixel(7, 9, BLACK );
  screen.drawPixel(7, 10, BLACK );
  screen.drawPixel(7, 11, BLACK );
  screen.drawPixel(7, 12, BLACK );
  screen.drawPixel(7, 13, BLACK );
  screen.drawPixel(7, 14, BLACK );
  screen.drawPixel(7, 15, BLACK );
  screen.drawPixel(7, 16, BLACK );
  screen.drawPixel(7, 17, BLACK );
  screen.drawPixel(7, 18, BLACK );
  screen.drawPixel(7, 19, BLACK );
  screen.drawPixel(7, 33, BLACK );
  screen.drawPixel(8, 5, BLACK );
  screen.drawPixel(8, 6, BLACK );
  screen.drawPixel(8, 7, BLACK );
  screen.drawPixel(8, 8, BLACK );
  screen.drawPixel(8, 9, BLACK );
  screen.drawPixel(8, 10, BLACK );
  screen.drawPixel(8, 11, BLACK );
  screen.drawPixel(8, 12, BLACK );
  screen.drawPixel(8, 13, BLACK );
  screen.drawPixel(8, 14, BLACK );
  screen.drawPixel(8, 15, BLACK );
  screen.drawPixel(8, 16, BLACK );
  screen.drawPixel(8, 17, BLACK );
  screen.drawPixel(8, 18, BLACK );
  screen.drawPixel(8, 19, BLACK );
  screen.drawPixel(8, 20, BLACK );
  screen.drawPixel(8, 33, BLACK );
  screen.drawPixel(9, 5, BLACK );
  screen.drawPixel(9, 6, BLACK );
  screen.drawPixel(9, 7, BLACK );
  screen.drawPixel(9, 8, BLACK );
  screen.drawPixel(9, 9, BLACK );
  screen.drawPixel(9, 10, BLACK );
  screen.drawPixel(9, 11, BLACK );
  screen.drawPixel(9, 14, BLACK );
  screen.drawPixel(9, 15, BLACK );
  screen.drawPixel(9, 16, BLACK );
  screen.drawPixel(9, 17, BLACK );
  screen.drawPixel(9, 18, BLACK );
  screen.drawPixel(9, 19, BLACK );
  screen.drawPixel(9, 20, BLACK );
  screen.drawPixel(9, 21, BLACK );
  screen.drawPixel(9, 34, BLACK );
  screen.drawPixel(10, 4, BLACK );
  screen.drawPixel(10, 5, BLACK );
  screen.drawPixel(10, 6, BLACK );
  screen.drawPixel(10, 7, BLACK );
  screen.drawPixel(10, 8, BLACK );
  screen.drawPixel(10, 9, BLACK );
  screen.drawPixel(10, 10, BLACK );
  screen.drawPixel(10, 11, BLACK );
  screen.drawPixel(10, 15, BLACK );
  screen.drawPixel(10, 16, BLACK );
  screen.drawPixel(10, 17, BLACK );
  screen.drawPixel(10, 18, BLACK );
  screen.drawPixel(10, 19, BLACK );
  screen.drawPixel(10, 20, BLACK );
  screen.drawPixel(10, 21, BLACK );
  screen.drawPixel(10, 34, BLACK );
  screen.drawPixel(10, 35, BLACK );
  screen.drawPixel(11, 4, BLACK );
  screen.drawPixel(11, 5, BLACK );
  screen.drawPixel(11, 6, BLACK );
  screen.drawPixel(11, 7, BLACK );
  screen.drawPixel(11, 8, BLACK );
  screen.drawPixel(11, 9, BLACK );
  screen.drawPixel(11, 10, BLACK );
  screen.drawPixel(11, 15, BLACK );
  screen.drawPixel(11, 16, BLACK );
  screen.drawPixel(11, 17, BLACK );
  screen.drawPixel(11, 18, BLACK );
  screen.drawPixel(11, 19, BLACK );
  screen.drawPixel(11, 20, BLACK );
  screen.drawPixel(11, 21, BLACK );
  screen.drawPixel(11, 22, BLACK );
  screen.drawPixel(11, 35, BLACK );
  screen.drawPixel(12, 3, BLACK );
  screen.drawPixel(12, 4, BLACK );
  screen.drawPixel(12, 5, BLACK );
  screen.drawPixel(12, 6, BLACK );
  screen.drawPixel(12, 7, BLACK );
  screen.drawPixel(12, 8, BLACK );
  screen.drawPixel(12, 9, BLACK );
  screen.drawPixel(12, 16, BLACK );
  screen.drawPixel(12, 17, BLACK );
  screen.drawPixel(12, 18, BLACK );
  screen.drawPixel(12, 19, BLACK );
  screen.drawPixel(12, 20, BLACK );
  screen.drawPixel(12, 21, BLACK );
  screen.drawPixel(12, 22, BLACK );
  screen.drawPixel(12, 36, BLACK );
  screen.drawPixel(13, 3, BLACK );
  screen.drawPixel(13, 4, BLACK );
  screen.drawPixel(13, 5, BLACK );
  screen.drawPixel(13, 6, BLACK );
  screen.drawPixel(13, 7, BLACK );
  screen.drawPixel(13, 8, BLACK );
  screen.drawPixel(13, 9, BLACK );
  screen.drawPixel(13, 17, BLACK );
  screen.drawPixel(13, 18, BLACK );
  screen.drawPixel(13, 19, BLACK );
  screen.drawPixel(13, 20, BLACK );
  screen.drawPixel(13, 21, BLACK );
  screen.drawPixel(13, 22, BLACK );
  screen.drawPixel(13, 36, BLACK );
  screen.drawPixel(13, 37, BLACK );
  screen.drawPixel(14, 2, BLACK );
  screen.drawPixel(14, 3, BLACK );
  screen.drawPixel(14, 4, BLACK );
  screen.drawPixel(14, 5, BLACK );
  screen.drawPixel(14, 6, BLACK );
  screen.drawPixel(14, 7, BLACK );
  screen.drawPixel(14, 8, BLACK );
  screen.drawPixel(14, 17, BLACK );
  screen.drawPixel(14, 18, BLACK );
  screen.drawPixel(14, 19, BLACK );
  screen.drawPixel(14, 20, BLACK );
  screen.drawPixel(14, 21, BLACK );
  screen.drawPixel(14, 22, BLACK );
  screen.drawPixel(14, 23, BLACK );
  screen.drawPixel(14, 37, BLACK );
  screen.drawPixel(15, 2, BLACK );
  screen.drawPixel(15, 3, BLACK );
  screen.drawPixel(15, 4, BLACK );
  screen.drawPixel(15, 5, BLACK );
  screen.drawPixel(15, 6, BLACK );
  screen.drawPixel(15, 7, BLACK );
  screen.drawPixel(15, 8, BLACK );
  screen.drawPixel(15, 18, BLACK );
  screen.drawPixel(15, 19, BLACK );
  screen.drawPixel(15, 20, BLACK );
  screen.drawPixel(15, 21, BLACK );
  screen.drawPixel(15, 22, BLACK );
  screen.drawPixel(15, 23, BLACK );
  screen.drawPixel(16, 1, BLACK );
  screen.drawPixel(16, 2, BLACK );
  screen.drawPixel(16, 3, BLACK );
  screen.drawPixel(16, 4, BLACK );
  screen.drawPixel(16, 5, BLACK );
  screen.drawPixel(16, 6, BLACK );
  screen.drawPixel(16, 7, BLACK );
  screen.drawPixel(16, 18, BLACK );
  screen.drawPixel(16, 19, BLACK );
  screen.drawPixel(16, 20, BLACK );
  screen.drawPixel(16, 21, BLACK );
  screen.drawPixel(16, 22, BLACK );
  screen.drawPixel(16, 23, BLACK );
  screen.drawPixel(16, 24, BLACK );
  screen.drawPixel(16, 38, BLACK );
  screen.drawPixel(17, 1, BLACK );
  screen.drawPixel(17, 2, BLACK );
  screen.drawPixel(17, 3, BLACK );
  screen.drawPixel(17, 4, BLACK );
  screen.drawPixel(17, 5, BLACK );
  screen.drawPixel(17, 6, BLACK );
  screen.drawPixel(17, 7, BLACK );
  screen.drawPixel(17, 18, BLACK );
  screen.drawPixel(17, 19, BLACK );
  screen.drawPixel(17, 20, BLACK );
  screen.drawPixel(17, 21, BLACK );
  screen.drawPixel(17, 22, BLACK );
  screen.drawPixel(17, 23, BLACK );
  screen.drawPixel(17, 24, BLACK );
  screen.drawPixel(17, 25, BLACK );
  screen.drawPixel(17, 38, BLACK );
  screen.drawPixel(18, 0, BLACK );
  screen.drawPixel(18, 1, BLACK );
  screen.drawPixel(18, 2, BLACK );
  screen.drawPixel(18, 3, BLACK );
  screen.drawPixel(18, 4, BLACK );
  screen.drawPixel(18, 5, BLACK );
  screen.drawPixel(18, 6, BLACK );
  screen.drawPixel(18, 19, BLACK );
  screen.drawPixel(18, 20, BLACK );
  screen.drawPixel(18, 21, BLACK );
  screen.drawPixel(18, 22, BLACK );
  screen.drawPixel(18, 23, BLACK );
  screen.drawPixel(18, 24, BLACK );
  screen.drawPixel(18, 25, BLACK );
  screen.drawPixel(18, 39, BLACK );
  screen.drawPixel(19, 0, BLACK );
  screen.drawPixel(19, 1, BLACK );
  screen.drawPixel(19, 2, BLACK );
  screen.drawPixel(19, 3, BLACK );
  screen.drawPixel(19, 4, BLACK );
  screen.drawPixel(19, 5, BLACK );
  screen.drawPixel(19, 6, BLACK );
  screen.drawPixel(19, 39, BLACK );
  screen.drawPixel(20, 1, BLACK );
  screen.drawPixel(20, 2, BLACK );
  screen.drawPixel(20, 3, BLACK );
  screen.drawPixel(20, 4, BLACK );
  screen.drawPixel(20, 5, BLACK );
  screen.drawPixel(20, 6, BLACK );
  screen.drawPixel(20, 7, BLACK );
  screen.drawPixel(20, 38, BLACK );
  screen.drawPixel(21, 1, BLACK );
  screen.drawPixel(21, 2, BLACK );
  screen.drawPixel(21, 3, BLACK );
  screen.drawPixel(21, 4, BLACK );
  screen.drawPixel(21, 5, BLACK );
  screen.drawPixel(21, 6, BLACK );
  screen.drawPixel(21, 7, BLACK );
  screen.drawPixel(21, 38, BLACK );
  screen.drawPixel(22, 2, BLACK );
  screen.drawPixel(22, 3, BLACK );
  screen.drawPixel(22, 4, BLACK );
  screen.drawPixel(22, 5, BLACK );
  screen.drawPixel(22, 6, BLACK );
  screen.drawPixel(22, 7, BLACK );
  screen.drawPixel(23, 3, BLACK );
  screen.drawPixel(23, 4, BLACK );
  screen.drawPixel(23, 5, BLACK );
  screen.drawPixel(23, 6, BLACK );
  screen.drawPixel(23, 7, BLACK );
  screen.drawPixel(23, 8, BLACK );
  screen.drawPixel(23, 37, BLACK );
  screen.drawPixel(24, 3, BLACK );
  screen.drawPixel(24, 4, BLACK );
  screen.drawPixel(24, 5, BLACK );
  screen.drawPixel(24, 6, BLACK );
  screen.drawPixel(24, 7, BLACK );
  screen.drawPixel(24, 8, BLACK );
  screen.drawPixel(24, 36, BLACK );
  screen.drawPixel(24, 37, BLACK );
  screen.drawPixel(25, 3, BLACK );
  screen.drawPixel(25, 4, BLACK );
  screen.drawPixel(25, 5, BLACK );
  screen.drawPixel(25, 6, BLACK );
  screen.drawPixel(25, 7, BLACK );
  screen.drawPixel(25, 8, BLACK );
  screen.drawPixel(25, 9, BLACK );
  screen.drawPixel(25, 36, BLACK );
  screen.drawPixel(26, 4, BLACK );
  screen.drawPixel(26, 5, BLACK );
  screen.drawPixel(26, 6, BLACK );
  screen.drawPixel(26, 7, BLACK );
  screen.drawPixel(26, 8, BLACK );
  screen.drawPixel(26, 9, BLACK );
  screen.drawPixel(26, 10, BLACK );
  screen.drawPixel(26, 35, BLACK );
  screen.drawPixel(27, 4, BLACK );
  screen.drawPixel(27, 5, BLACK );
  screen.drawPixel(27, 6, BLACK );
  screen.drawPixel(27, 7, BLACK );
  screen.drawPixel(27, 8, BLACK );
  screen.drawPixel(27, 9, BLACK );
  screen.drawPixel(27, 10, BLACK );
  screen.drawPixel(27, 34, BLACK );
  screen.drawPixel(27, 35, BLACK );
  screen.drawPixel(28, 5, BLACK );
  screen.drawPixel(28, 6, BLACK );
  screen.drawPixel(28, 7, BLACK );
  screen.drawPixel(28, 8, BLACK );
  screen.drawPixel(28, 9, BLACK );
  screen.drawPixel(28, 10, BLACK );
  screen.drawPixel(28, 11, BLACK );
  screen.drawPixel(28, 34, BLACK );
  screen.drawPixel(29, 5, BLACK );
  screen.drawPixel(29, 6, BLACK );
  screen.drawPixel(29, 7, BLACK );
  screen.drawPixel(29, 8, BLACK );
  screen.drawPixel(29, 9, BLACK );
  screen.drawPixel(29, 10, BLACK );
  screen.drawPixel(29, 11, BLACK );
  screen.drawPixel(29, 12, BLACK );
  screen.drawPixel(29, 33, BLACK );
  screen.drawPixel(30, 6, BLACK );
  screen.drawPixel(30, 7, BLACK );
  screen.drawPixel(30, 8, BLACK );
  screen.drawPixel(30, 9, BLACK );
  screen.drawPixel(30, 10, BLACK );
  screen.drawPixel(30, 11, BLACK );
  screen.drawPixel(30, 12, BLACK );
  screen.drawPixel(30, 33, BLACK );
  screen.drawPixel(31, 7, BLACK );
  screen.drawPixel(31, 8, BLACK );
  screen.drawPixel(31, 9, BLACK );
  screen.drawPixel(31, 10, BLACK );
  screen.drawPixel(31, 11, BLACK );
  screen.drawPixel(31, 12, BLACK );
  screen.drawPixel(31, 13, BLACK );
  screen.drawPixel(32, 7, BLACK );
  screen.drawPixel(32, 8, BLACK );
  screen.drawPixel(32, 9, BLACK );
  screen.drawPixel(32, 10, BLACK );
  screen.drawPixel(32, 11, BLACK );
  screen.drawPixel(32, 12, BLACK );
  screen.drawPixel(32, 32, BLACK );
  screen.drawPixel(33, 8, BLACK );
  screen.drawPixel(33, 9, BLACK );
  screen.drawPixel(33, 10, BLACK );
  screen.drawPixel(33, 11, BLACK );
  screen.drawPixel(33, 12, BLACK );
  screen.drawPixel(33, 32, BLACK );
  screen.drawPixel(34, 8, BLACK );
  screen.drawPixel(34, 9, BLACK );
  screen.drawPixel(34, 10, BLACK );
  screen.drawPixel(34, 11, BLACK );
  screen.drawPixel(34, 31, BLACK );
  screen.drawPixel(35, 8, BLACK );
  screen.drawPixel(35, 9, BLACK );
  screen.drawPixel(35, 10, BLACK );
  screen.drawPixel(35, 30, BLACK );
  screen.drawPixel(36, 9, BLACK );
  screen.drawPixel(36, 10, BLACK );
  screen.drawPixel(36, 30, BLACK );
  screen.drawPixel(37, 9, BLACK );
  screen.drawPixel(37, 10, BLACK );
  screen.drawPixel(37, 11, BLACK );
  screen.drawPixel(37, 12, BLACK );
  screen.drawPixel(37, 13, BLACK );
  screen.drawPixel(37, 14, BLACK );
  screen.drawPixel(37, 15, BLACK );
  screen.drawPixel(37, 16, BLACK );
  screen.drawPixel(37, 17, BLACK );
  screen.drawPixel(37, 18, BLACK );
  screen.drawPixel(37, 19, BLACK );
  screen.drawPixel(37, 20, BLACK );
  screen.drawPixel(37, 21, BLACK );
  screen.drawPixel(37, 22, BLACK );
  screen.drawPixel(37, 23, BLACK );
  screen.drawPixel(37, 24, BLACK );
  screen.drawPixel(37, 25, BLACK );
  screen.drawPixel(37, 26, BLACK );
  screen.drawPixel(37, 27, BLACK );
  screen.drawPixel(37, 28, BLACK );
  screen.drawPixel(37, 29, BLACK );
  screen.setCursor(0,40);
  screen.println("[Paulyphone]");
  screen.display();
  //delay(6000);
}

void drawRocket()
{
static unsigned char PROGMEM rocket[] =
{ 
B00000001, B00000000,
B00000011, B10000000,
B00000111, B11000000,
B00001111, B11100000,
B00011111, B11110000,
B00000000, B00000000,
B00001100, B01100000, };
int WIDTH=16; 
int HEIGHT=7; 
screen.drawBitmap(0, 0,  rocket, WIDTH,HEIGHT, 1);
}

void registerNet()
{
  mode = REGISTERING;
  screen.clearDisplay();
  screen.setTextColor(BLACK,WHITE);
  screen.setCursor(0,0);
  screen.setTextSize(1);
  screen.println("verbindet");

  
  screen.display();
  
  pinMode(4, OUTPUT);
  
  // restart the GSM module.
  // the library will attempt to start the module using pin 7, which is SCK
  // (and not connected to anything except the ISP header)
  pinMode(19, OUTPUT);
  digitalWrite(19, LOW);
  delay(12000);
  digitalWrite(19, HIGH);
  /*
  if(PINManager.checkPIN(mypin) == 0)
      {
        //auth = true;
        PINManager.setPINUsed(true);
        screen.println("pinok");
      }
  else
      {  
        Serial.println("Incorrect PIN. Remember that you have 3 opportunities.");
        screen.println("Incorrect PIN. Remember that you have 3 opportunities.");
        mode=ENTERSIMPIN;
      }
   */
    
    if (mode!=ENTERSIMPIN)
    {
      //screen.println("vor begin");
      while (gsmAccess.begin(mypin, true) != GSM_READY) {
        delay(1000);
        //todo mode=ENTERSIMPIN falls PIN wrong zurückgemeldet wird.
      }
       screen.clearDisplay();
      screen.println("connected.");
      screen.display();
  
      vcs.hangCall();
  
      delay(300);
  
      screen.println("caching...");
      screen.display();
  
      cachePhoneBook();
  
      screen.println("done.");
      screen.display();
      mode = HOME;
    }
    else
    {
      screen.println("pin wrong");
    }
}


void validateSimPin()
{

if (true)
{
  registerNet();
}
else
{
      
          
          

  // check if the SIM have pin lock
  while(!auth){
    int pin_query = PINManager.isPIN();
    screen.clearDisplay();
    screen.println("isPin");
    screen.println(pin_query);
    if(pin_query == 1)
    {
      // if SIM is locked, enter PIN code
      Serial.print("Enter PIN code: ");
      //user_input = readSerial();
      // check PIN code
      if(PINManager.checkPIN(mypin) == 0)
      {
        auth = true;
        PINManager.setPINUsed(true);
        registerNet();
      }
      else
      {  
        // if PIN code was incorrected
        Serial.println("Incorrect PIN. Remember that you have 3 opportunities.");
      }
    }
    else if(pin_query == -1)
    {
      // PIN code is locked, user must enter PUK code
      /*Serial.println("PIN locked. Enter PUK code: ");
      String puk = readSerial();
      Serial.print("Now, enter a new PIN code: ");
      user_input = readSerial();
      // check PUK code
      if(PINManager.checkPUK(puk, user_input) == 0)
      {
        auth = true;
        PINManager.setPINUsed(true);
        Serial.println(oktext);
        registerNet();
      }
      else
      {
        // if PUK o the new PIN are incorrect
        Serial.println("Incorrect PUK or invalid new PIN. Try again!.");
      }
      */
    }
    else if(pin_query == -2)
    {
      // the worst case, PIN and PUK are locked
      Serial.println("PIN & PUK locked. Use PIN2/PUK2 in a mobile phone.");
      screen.println("PIN & PUK locked. Use PIN2/PUK2 in a mobile phone.");
      while(true);
    }
    else
    {
      // SIM does not requires authetication 
      Serial.println("No pin necessary.");
      screen.println("No pin necessary.");
      auth = true;
    }
          
             
        }
     
     
      


}


}
