# arduino-spotify

This project demonstrates an innovative ESP32 BLE Jukebox system that connects to a Bluefruit controller and a Wi-Fi hotspot to fetch and play songs. The ESP32 microcontroller communicates with a mobile app via BLE to receive commands for controlling the playback of songs fetched from an API.

## Code Explanation

### Main Components

1. **BLE Communication**:
   - The code sets up BLE communication with the Bluefruit app to receive control commands.

2. **Wi-Fi Connection**:
   - Connects to a specified Wi-Fi network to fetch song data from the API.

3. **Song Queue Management**:
   - Implements a stack structure to manage the song queue, allowing users to play, pause, skip, and replay songs.

4. **Melody Playback**:
   - Fetches song tempo and melody from the API and plays the song using a buzzer.

### Key Functions

- `void push(String song)`: Adds a new song to the queue.
- `String getCurrentSong()`: Retrieves the current song.
- `String getPreviousSong()`: Retrieves the previous song.
- `String getNextSong()`: Retrieves the next song.
- `String getSongName()`: Fetches a song name from the API.
- `String* getMelodyTempo(String response)`: Parses the JSON response to get melody and tempo.
- `void playMelody(String* melTemp)`: Plays the melody based on the provided tempo and notes.
- `void playSong()`: Plays the current song in the queue.
