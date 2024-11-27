#include <Arduino_JSON.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>

volatile bool stopPlayback = false;
volatile bool endSong = false;
volatile bool previousSong = false;
volatile bool nextSong = false;
volatile bool less5Notes = false;

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
String rxString = "";
std::string rxValue;// rxValue gathers input data

// UART service UUID data
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define REST = 0

//Hotspot Auth
const char* ssid = "enter-your-hotspot-name";
const char* password = "enter-your-hotspot-password";
WiFiClientSecure client;

//API related info
int HTTP_PORT = 443;  // HTTPS port
String HTTP_METHOD = "GET";  // or POST
const char* HOST_NAME = "iotjukebox.onrender.com";
String PATH_NAME = "/song";

//Song queue info
String task = "";
const int stackSize = 15; 
String songStack[stackSize];
int top = -1;
int currentIndex = -1; 

int buzzer = 21; // pin for buzzer

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

/*Recognizes button presses from the Bluefruit controller, interupts used to changes flags for pausing, playing, prev and next*/
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue().c_str();
    if (rxValue.length() > 0) {
      Serial.println(" ");
      Serial.print("Received data: ");
      
      rxString = ""; // Reset rxString
      for (int i = 0; i < rxValue.length(); i++) {
        Serial.print(rxValue[i]);
        rxString = rxString + rxValue[i]; // build string from received data
      }
      
      task = rxString;
      Serial.println("Task set to: " + String(task)); // Debugging statement

      if (task == "!B11:") { // pause/play
        stopPlayback = !stopPlayback;
      } 
      
      else if (task == "!B714") { // prev
        previousSong = true;
        endSong = true;
      }

      else if (task == "!B813"){
        nextSong = true;
        endSong = true;
      }
      
      delay(10);
      Serial.println("StopPlayback state: " + String(stopPlayback)); // Debugging statement
    }
  }
};

/*QUEUE FUNTIONS*/

/*pushes new song onto queue*/
void push(String song) {
  if (!isFull()) {
    songStack[++top] = song;
  } else {
    Serial.println("Stack is full. Cannot push song.");
  }
}

String getCurrentSong() {
  return songStack[currentIndex];
}

String getPreviousSong() {
  if (currentIndex > 0) {
    return songStack[currentIndex - 1];
  } else {
    Serial.println("No previous song.");
    return "";
  }
}

String getNextSong() {
  if (currentIndex < top) {
    return songStack[currentIndex + 1];
  } else {
    Serial.println("No next song.");
    return "";
  }
}

bool isEmpty() {
  return top == -1;
}

bool isFull() {
  return top == stackSize - 1;
}

/*Fetches song name from API*/
String getSongName(){
  while (!client.connected()) {
    if (client.connect(HOST_NAME, HTTP_PORT)) {
      Serial.println("Connected to server for get song name.");
    } else {
      Serial.println("Connecting to server for get song name...");
      delay(1000); // Wait for a second before retrying
    }
  }
  
  client.println(HTTP_METHOD + " " + PATH_NAME + " HTTP/1.1");
  client.println("Host: " + String(HOST_NAME));
  client.println("Connection: close");
  client.println();  // end HTTP header
  delay(1000);

  // Read the response from the server
  String response = "";
  while (client.connected() || client.available()) {
    if (client.available()) {
      response += client.readString();
    }
  }
  client.stop();
  return response;
}

/*Gets song melody and tempo*/
String* getMelodyTempo(String response) {
  // Parse the JSON response
  int jsonStart = response.indexOf('{');
  int jsonEnd = response.lastIndexOf('}') + 1;
  String jsonResponse = response.substring(jsonStart, jsonEnd);

  // Parse the JSON response
  JSONVar doc = JSON.parse(jsonResponse);

  // Check for errors in parsing
  if (JSON.typeof(doc) == "undefined") {
    Serial.println("Parsing input failed!");
    static String errorResult[2] = {"Tempo: N/A", "Melody: N/A"};
    return errorResult;
  }

  // Extract values from the JSON document
  String tempo = (const char*) doc["tempo"];
  String melody = JSON.stringify(doc["melody"]);

  static String result[2];
  result[0] = tempo;
  result[1] = melody;

  return result;
}

/*Plays the song and handles play/pausing*/
void playMelody(String* melTemp) {
  // Extract and trim the tempo string
  melTemp[0].trim();
  int tempo = melTemp[0].toInt();

  // Extract and parse the melody string
  String melodyStr = melTemp[1];
  melodyStr.replace("[", ""); // Remove brackets
  melodyStr.replace("]", ""); // Remove brackets
  melodyStr.replace("\"", ""); // Remove quotes

  // Split the melody string into individual note strings
  int noteCount = 0;
  String noteStrings[100]; // Assuming a maximum of 100 notes
  int startIndex = 0;
  int endIndex = melodyStr.indexOf(',');
  
  while (endIndex != -1) {
    noteStrings[noteCount++] = melodyStr.substring(startIndex, endIndex);
    startIndex = endIndex + 1;
    endIndex = melodyStr.indexOf(',', startIndex);
  }
  noteStrings[noteCount++] = melodyStr.substring(startIndex); // Add the last note

  // Convert note strings to integers
  int melody[noteCount];
  for (int i = 0; i < noteCount; i++) {
    melody[i] = noteStrings[i].toInt();
  }

  // Calculate the duration of a whole note in ms (60s/tempo)*4 beats
  int wholenote = (60000 * 4) / tempo;
  
  int divider = 0, noteDuration = 0;

  // Iterate over the notes of the melody
  for (int thisNote = 0; thisNote < noteCount; thisNote += 2) {
    if(endSong){
      if (thisNote < 5){
        less5Notes = true;
      }
      Serial.println("Ending song...");
      return;}
    // Check if playback should pause/play
    while (stopPlayback) {
      Serial.println("Playback paused, waiting to resume...");
      delay(10); // Short delay to prevent busy-waiting
    }

    // Calculate the duration of each note
    divider = melody[thisNote + 1];
    if (divider == 0) {
      noteDuration = wholenote; // Default duration if divider is zero
    } else if (divider > 0) {
      noteDuration = wholenote / divider;
    } else if (divider < 0) {
      noteDuration = wholenote / abs(divider);
      noteDuration *= 1.5; // Increases the duration by half for dotted notes
    }

    // Play the note for 90% of the duration, leaving 10% as a pause
    tone(buzzer, melody[thisNote], noteDuration * 0.9);

    // Wait for the specified duration before playing the next note
    delay(noteDuration);
    
    // Stop the waveform generation before the next note
    noTone(buzzer);
  }
}

/*Play current song being pointed to in the queue*/
void playSong(){
  if (!isEmpty()) {
    String currentSong = getCurrentSong();
    Serial.println("Now playing: " + currentSong);
    String* melTemp = getMelodyTempo(currentSong);
    playMelody(melTemp);
  }
}

/*Setup wifi and connect to bluefruit, and push first song to song queue*/
void setup() { 
  Serial.begin(115200);
  
  BLEDevice::init("Sophia's ESP32 UART"); // give the BLE device a name
  
  BLEServer *pServer = BLEDevice::createServer(); // create BLE server
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY);                    
  pCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE);
  pCharacteristic->setCallbacks(new MyCallbacks());
  
  pService->start(); // start the service

  pServer->getAdvertising()->start(); // start advertising
  Serial.println("Waiting a client connection to notify...");
  Serial.println(" ");
  delay(1000); 

  
  // Connect to wifi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  client.setInsecure();  

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("Connected to WiFi");

  //Set up music queue
  push(getSongName());
  currentIndex ++;

}

/*Updates current pointer in the song playlist, allowing prev and next, and for playing beginning of song if more than 5 notes played*/
void loop() {
  if (deviceConnected) {
    if (previousSong && less5Notes) {
      Serial.print("prev entered, less than 5 notes played go back a song");
      if (currentIndex > 0) { 
        currentIndex -= 1;
      }
      Serial.print("Updated curr: ");
      Serial.println(currentIndex);
      previousSong = false;
      endSong = false;
      less5Notes = false;
    } else if (previousSong && !less5Notes) {
      Serial.print("prev entered, over 5 notes play back to beginning of the song");
      Serial.print("kept curr: ");
      Serial.println(currentIndex);
      previousSong = false;
      endSong = false;
    }else if (nextSong) {
      Serial.print("next entered");
      if (currentIndex == top) {
        Serial.print("no next, getting new");
        push(getSongName());
        currentIndex +=1;
      }
      else{
        currentIndex +=1;
        Serial.print("going next");
        }
      nextSong = false;
      endSong = false;

    } else {
      playSong();
      if(!endSong)
      {
        push(getSongName());
        currentIndex++;
      }
    }
  }
}
