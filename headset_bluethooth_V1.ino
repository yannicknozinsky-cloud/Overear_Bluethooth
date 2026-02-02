#include <BluetoothA2DPSink.h>
#include <ESP32Encoder.h>
#include <SPI.h>
#include <TFT_eSPI.h>


#define state_unconnected 1
#define state_paused 2
#define state_start 3
int status = state_start;
//==================================================
//  74HC595 LED OUTPUT (Beat-Visualizer)
//==================================================
#define latchPin 15
#define clockPin 21
#define PinRED 2 //red
#define PinBLUE 32
#define PinGREEN 33


//==================================================
//  DISPLAY
//==================================================
TFT_eSPI tft = TFT_eSPI();
byte center_x = 120;
byte center_y = 120;

//==================================================
//  AUDIO WAVE PROCESSING
//==================================================
const int WAVE_WIDTH = 240;
int16_t waveform[WAVE_WIDTH];
int waveIndex = 0;
float hue = 0.0;          // 0–360° über den Farbkreis 
float hueStep = 1.1;      // wie schnell die Farbe wandert (kleiner = langsamer)
#define MaxVolume 90
#define StartVolume 20

//==================================================
//  MAX98357A
//==================================================
#define BCLK 26
#define LRCLK 25
#define DATA 22  

//==================================================
//  ROTARY ENCODER
//==================================================
#define Encoder_A 19
#define Encoder_B 17
#define ENC_SW    27

BluetoothA2DPSink a2dp_sink;
ESP32Encoder encoder;
volatile bool buttonPressed = false;
bool paused = false;
bool connected = false;

//==================================================
// MULTICORE
//==================================================
TaskHandle_t AudioCore;
TaskHandle_t UICore;


//==================================================
// BUTTON INT
//==================================================
void IRAM_ATTR handleButton(){
  buttonPressed = true;
}

//==================================================
// Colours
//==================================================
const byte COLOUR_OUT[3][8]  PROGMEM = {{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0}};
const byte COLOUR_RED[3][8]  PROGMEM = {{1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0}};
const byte COLOUR_GREEN[3][8]  PROGMEM = {{0,0,0,0,0,0,0,0},{1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0}};
const byte COLOUR_BLUE[3][8]  PROGMEM = {{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,1,1,1,1,1,1,1}};
const byte COLOUR_PURPLE[3][8]  PROGMEM = {{1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0},{1,1,1,1,1,1,1,1}};
const byte COLOUR_YELLOW[3][8]  PROGMEM = {{1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,1},{0,0,0,0,0,0,0,0}};
const byte COLOUR_CYAN[3][8]  PROGMEM = {{0,0,0,0,0,0,0,0},{1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,1}};
const byte COLOUR_WHITE[3][8]  PROGMEM = {{1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,1}};

byte LED_matrix[3][8];

//==================================================
//  AUDIO CALLBACK → Füllt Waveform Buffer
//==================================================
void audio_data_callback(const uint8_t *data, uint32_t len) {
  for (uint32_t i = 0; i < len; i += 2) {

    // 16-bit signed Sample zusammensetzen
    int16_t sample = (int16_t)(data[i] | (data[i + 1] << 8));

    // 🔉 Lautstärke halbieren
    sample = sample / 2;

    // Optional: für Anzeige skalieren
    int mapped = map(sample, -32768, 32767, -100, 100);

    waveform[waveIndex] = mapped;
    waveIndex = (waveIndex + 1) % WAVE_WIDTH;
  }
}

void setOneBit(bool r, bool g, bool b) {
  digitalWrite(latchPin, LOW);
  if(r){
    digitalWrite(PinRED, HIGH);
  }else{
    digitalWrite(PinRED, LOW);
  }
  if(g){
    digitalWrite(PinGREEN, HIGH);
  }else{
    digitalWrite(PinGREEN, LOW);
  }
  if(b){
    shiftOut(PinBLUE, clockPin, MSBFIRST, 1);
  }else{
    shiftOut(PinBLUE, clockPin, MSBFIRST, 0);
  }
  digitalWrite(latchPin, HIGH);
}


void setLED(byte matrix[3][8], int waitingtime, int brightness){
  
  for (int col = 0; col < brightness; col++) {
       digitalWrite(latchPin, LOW);
      if(matrix[0][col] == 0){
        digitalWrite(PinRED, LOW);
      }else{
         digitalWrite(PinRED, HIGH);
      }
      if(matrix[1][col] == 0){
        digitalWrite(PinGREEN, LOW);
      }else{
        digitalWrite(PinGREEN, HIGH);
      }
      if(matrix[2][col] == 0){
        shiftOut(PinBLUE, clockPin, MSBFIRST, 0);
      }else{
        shiftOut(PinBLUE, clockPin, MSBFIRST, 1);
      }
      digitalWrite(latchPin, HIGH); 
      delay(waitingtime);       
  } 
  digitalWrite(latchPin, LOW);
  digitalWrite(PinRED, LOW);
  digitalWrite(PinGREEN, LOW);
  shiftOut(PinBLUE, clockPin, MSBFIRST, 0);
  digitalWrite(latchPin, HIGH); 
  delay(waitingtime); 
}




void start_parallel(){
  // LED-Timer
    unsigned long ledLast = 0;
    int ledStep = 0;

    // TFT-Timer
    unsigned long tftLast = 0;
    int tftR = 10;

    int totalLEDSteps = 27; // 8 ROT + 8 GRÜN + 8 BLAU
    int ledDelay = 40;      // ms pro Schritt (anpassbar für gleiche Dauer wie TFT)

    tft.fillScreen(TFT_BLACK);
    int cx = center_x, cy = center_y;

    while(ledStep < totalLEDSteps || tftR < 120) {
        unsigned long now = millis();

        // -------- LED Update --------
        if(ledStep < totalLEDSteps && now - ledLast >= ledDelay) {
            ledLast = now;

            if(ledStep < 9) setOneBit(1, 0, 0);    // ROT
            else if(ledStep < 18) setOneBit(0, 1, 0); // GRÜN
            else setOneBit(0, 0, 1);               // BLAU

            ledStep++;
        }

        // -------- TFT Update --------
        if(tftR < 120 && now - tftLast >= 5) {
            tftLast = now;

            uint16_t col = tft.color565(
                127 + 127 * sin(tftR * 0.05),
                127 + 127 * sin(tftR * 0.08),
                127 + 127 * sin(tftR * 0.11)
            );
            tft.drawCircle(cx, cy, tftR, col);
            tftR++;
        }
    }
    memcpy_P(LED_matrix, COLOUR_PURPLE, sizeof(LED_matrix));
    setLED(LED_matrix,0,8);
    // TFT Text Animation
    for(int s = 1; s <= 3; s++) {
        tft.fillCircle(cx, cy, 70, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setFreeFont(&FreeSansBold24pt7b);
        tft.setTextSize(s);
        tft.setTextColor(TFT_PURPLE);
        tft.drawString("YN", cx, cy);
        delay(70);
    }

    tft.setTextColor(TFT_PURPLE);
    tft.drawString("YN", cx, cy);
    delay(400);
}



//==================================================
//  CORE 0 → AUDIO + BLUETOOTH
//==================================================
void AudioTask(void *){
  
  i2s_pin_config_t pins = {
    .bck_io_num = BCLK,
    .ws_io_num  = LRCLK,
    .data_out_num = DATA,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  a2dp_sink.set_pin_config(pins);
  a2dp_sink.set_bits_per_sample(16);
  a2dp_sink.set_channels(I2S_CHANNEL_STEREO);
  a2dp_sink.set_stream_reader(audio_data_callback);
  a2dp_sink.start("YN Headset");

  while(true) delay(50);
}



//==================================================
//  CORE 1 → VISUALIZER + UI + LED BEAT
//==================================================
void UItask(void *){
  
  int lastVol = StartVolume;
  tft.fillScreen(TFT_BLACK);

  while(true){

    //=================== VOLUME DREHREGLER ==================
    int newVol = encoder.getCount();
    newVol = constrain(newVol,0,MaxVolume);

    if(newVol!=lastVol){
      lastVol=newVol;
      a2dp_sink.set_volume(newVol);

      tft.setCursor(center_x-13,center_y-80,2);
      tft.setTextSize(2);
      tft.setTextColor(TFT_RED);
      tft.printf("%d",newVol);
    }


    //=================== PLAY/PAUSE BUTTON ==================
    if(buttonPressed){
      buttonPressed=false;
      if(paused){
        pressed_pause();
      }
      paused=!paused;
      paused? a2dp_sink.pause():a2dp_sink.play();
      
    }


    //=================== STATUS → NICHT VERBUNDEN ==================
    if(!a2dp_sink.is_connected()){
      if(status!=state_unconnected){
        status=state_unconnected;
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(3);
        tft.setTextColor(TFT_PURPLE);
        tft.drawString("YN",center_x,center_y);
        tft.fillCircle(center_x, center_y+110, 6, TFT_RED); // Status rot
        memcpy_P(LED_matrix, COLOUR_RED, sizeof(LED_matrix));
        setLED(LED_matrix,0,8);
        delay(200);
      }
      continue;
    }


    //=================== STATUS → VERBUNDEN aber PAUSE ==================
    if(paused){
      if(status != state_paused){
        status=state_paused;
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_PURPLE);
        tft.setTextSize(2);
        tft.drawString("| |",center_x-10,center_y-20);
        tft.fillCircle(center_x, center_y+110, 6, TFT_GREEN);
        memcpy_P(LED_matrix, COLOUR_GREEN, sizeof(LED_matrix));
        setLED(LED_matrix,0,8);
        delay(200);
      }
      continue;
    }


    // ------------------ Waveform ------------------
tft.fillRect(0, 0, WAVE_WIDTH, 210, TFT_BLACK);  // Hintergrund für Motion-Blur/Fade

for(int x=1; x<WAVE_WIDTH; x++){
    int pos1 = (waveIndex+x-1) % WAVE_WIDTH;
    int pos2 = (waveIndex+x) % WAVE_WIDTH;

    float y1_raw = waveform[pos1];
    float y2_raw = waveform[pos2];

    // ---- Lineare Verstärkung + Clipping ----
   auto amplify = [](float val) -> float {
    float scaled = val * 2.0;      // alles doppelt so stark
    if(scaled > 100.0) return 100.0;   // Maximalwert oben begrenzen
    if(scaled < -100.0) return -100.0;
    return scaled;
};


    y1_raw = amplify(y1_raw);
    y2_raw = amplify(y2_raw);

    int y1 = center_y + y1_raw * 1.8;  // Multiplikator für vertikales Ausschwingen
    int y2 = center_y + y2_raw * 1.8;

    // ---- Farbe (Regenbogen langsam) ----
    uint8_t r, g, b;
    HSVtoRGB(hue, 1.0, 1.0, r, g, b);

    tft.drawLine(x-1, y1, x, y2, tft.color565(r, g, b));
}

  hue += hueStep;
  if(hue >= 360) hue = 0;

    //=================== LED BEAT ===================
    int bass=0;
    for(int i=0; i<120; i++)                 // mehr Samples → viel präziser
        bass += abs(waveform[(waveIndex+i)%WAVE_WIDTH]);

    bass /= 120;
    bass = bass * 300;                        // Verstärkung damit LED sicher reagieren

    int level = map(bass,0,800,0,8);
    level = constrain(level,0,8);
    bool red = random(0, 2);
    bool green = random(0, 2);
    bool blue = random(0, 2);


    memcpy_P(LED_matrix, COLOUR_OUT, sizeof(LED_matrix));
    for(int i=0;i<level;i++){
      if(red){LED_matrix[0][i] = 1;}
      if(green){LED_matrix[1][i] = 1;}
      if(blue){LED_matrix[2][i] = 1;}
    }
    setLED(LED_matrix,0,8);
    tft.fillCircle(center_x, center_y+110, 6, TFT_GREEN);
    delay(1);
  }
}

void pressed_pause(){
   status=state_paused;
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_PURPLE);
        tft.setTextSize(2);
        tft.drawString("| |",center_x-10,center_y-20);
        tft.fillCircle(center_x, center_y+110, 6, TFT_GREEN);
        
}


//==================================================
//  SETUP
//==================================================
void setup(){
  pinMode(latchPin,OUTPUT);
  pinMode(clockPin,OUTPUT);
  pinMode(PinRED,OUTPUT);
  pinMode(PinGREEN,OUTPUT);
  pinMode(PinBLUE,OUTPUT);

  tft.init();
  delay(500);
  start_parallel();

  ESP32Encoder::useInternalWeakPullResistors=puType::up;
  encoder.attachHalfQuad(Encoder_A,Encoder_B);
  encoder.setCount(StartVolume);

  pinMode(ENC_SW,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENC_SW),handleButton,FALLING);

  xTaskCreatePinnedToCore(AudioTask,"AudioCore",4096,NULL,2,&AudioCore,0);
  xTaskCreatePinnedToCore(UItask,"UICore",6000,NULL,1,&UICore,1);
}

// ---------- HSV → RGB Umrechner für perfekten Regenbogen ----------
void HSVtoRGB(float H, float S, float V, uint8_t &r, uint8_t &g, uint8_t &b){
    float C = V * S;
    float X = C * (1 - fabs(fmod(H / 60.0, 2) - 1));
    float m = V - C;

    float R,G,B;
    if(H < 60){ R=C;G=X;B=0; }
    else if(H <120){ R=X;G=C;B=0; }
    else if(H <180){ R=0;G=C;B=X; }
    else if(H <240){ R=0;G=X;B=C; }
    else if(H <300){ R=X;G=0;B=C; }
    else{ R=C;G=0;B=X; }

    r = (R+m)*255;
    g = (G+m)*255;
    b = (B+m)*255;
}


void loop(){}
