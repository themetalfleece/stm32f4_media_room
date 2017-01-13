

// READ ME !!!!!!!!!!!!!!!!
// change ssid and password below to your router
// change IP ( 192.168.1.123 ) to IP of ESP when connected
// give http://192.168.1.123/update  για Firmware update over the air. select firmware.bin file to download
// give http://192.168.1.123/reboot  to reboot
// give http://192.168.1.123/set?LED=ON  to turn ON led
// give http://192.168.1.123/set?LED=OFF  to turn OFF led
// give http://192.168.1.123 for demo html table

#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <Hash.h>

#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>

ESP8266HTTPUpdateServer httpUpdater;
ESP8266WebServer servertcp(80);
WebSocketsServer webSocket=WebSocketsServer(88);
String JSONtxt;

const char* ssid = "Azoth";
const char* password = "";

char Buffer[256];
String SongTitle;
int i = 0;

int ledPin = 2; // GPIO2


static const char PROGMEM INDEX_HTML[] = R"rawliteral(

  <!DOCTYPE html>
  <html>
  <head>
      <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js"></script>
      <style>
          body {
              background-image: url("http://templatemints.com/rttheme13/images/background_images/abstract_background7.jpg");
              background-repeat: repeat;
          }
          .button {
              position: relative;
              background-color: #4CAF50;
              border: none;
              font-size: 16px;
              color: #FFFFFF;
              padding: 10px;
              width: 150px;
              text-align: center;
              -webkit-transition-duration: 0.4s;
              transition-duration: 0.4s;
              text-decoration: none;
              overflow: hidden;
              cursor: pointer;
          }
          .button:after {
              content: "";
              background: #f1f1f1;
              display: block;
              position: absolute;
              padding-top: 300%;
              padding-left: 350%;
              margin-left: -20px !important;
              margin-top: -120%;
              opacity: 0;
              transition: all 0.8s
          }
          .button:active:after {
              padding: 0;
              margin: 0;
              opacity: 1;
              transition: 0s
          }
          .col-centered {
              position: relative;
              left:0%;
              margin: 0 auto;
              text-align: center;
              width: 50%;
          }
          .center {
              margin: 2% auto;
          }
      </style>
      <script>
          $(document).ready( function() {
              InitWebSocket();
              function InitWebSocket(){
                  websock=new WebSocket('ws://'+window.location.hostname+':88/');
                  websock.onmessage=function(evt){
                      JSONobj=JSON.parse(evt.data);
                      $('#song_title').html(JSONobj.song_title);
                  }
              };
          });
      </script>
  </head>
  <body>
      <div class="col-centered">
          <h2 id="song_title" class="center">Loading Song Title ...</h2>
          <a href="previous_song"><button class="center button">Previous Song</button></a>
          &nbsp;<a href="play_pause"><button class="center button">Play/Pause</button></a>` `
          &nbsp;<a href="next_song"><button class="center button">Next Song</button></a>
      </div>
  </body>
  </html>

)rawliteral";

void handlePreviousSong() {
  handleRoot();

  SerialPrint("1");
  delay(1000);
}

void handlePlayPause() {
  handleRoot();

  SerialPrint("2");
  delay(1000);
}

void handleNextSong() {
  handleRoot();

  SerialPrint("3");
  delay(1000);
}

void handleReboot() {
  servertcp.send(200, "text/html", "<h1>REBOOT</h1>");
  delay(1000);
  ESP.restart();
}

void handleRoot() {
  servertcp.send(200, "text/html", INDEX_HTML);
}


void setup() {
  Serial.begin(9600);
  delay(10);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Connect to WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

    // config static IP
  IPAddress ip(192, 168, 1, 200); // where xx is the desired IP Address
  IPAddress gateway(192, 168, 1, 254); // set gateway to match your network
  Serial.print(F("Setting static ip to : "));
  Serial.println(ip);
  IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network
  WiFi.config(ip, gateway, subnet);


  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");


  httpUpdater.setup(&servertcp);

  // Start the server
  Serial.println("Server started");

  servertcp.on("/", handleRoot);
  servertcp.on("/reboot", handleReboot);
  servertcp.on("/previous_song", handlePreviousSong);
  servertcp.on("/play_pause", handlePlayPause);
  servertcp.on("/next_song", handleNextSong);
  servertcp.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  webSocket.loop();
  servertcp.handleClient();
  yield();

  while (Serial.available()){
    char c = Serial.read();  //gets one byte from serial buffer
    Buffer[i] = c; //read byte into buffer
    i++; //increment buffer by 1 for next byte
    if (c == '|'){
      Buffer[i]='\0';
      Serial.println(Buffer);
      i = 0; //reset to get new packet
      SongTitle=String(Buffer);
      SongTitle.remove(SongTitle.length()-1, 1);

      JSONtxt="{\"song_title\":\""+SongTitle+"\"}";
      webSocket.broadcastTXT(JSONtxt);
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t wslength){
  String payloadString=(const char *)payload;
  Serial.println("payload: '"+payloadString+"', channel: "+(String)num);
}

void  SerialPrint(char *format,...){
  char buff[256];
  va_list args;
  va_start (args,format);
  vsnprintf(buff,sizeof(buff),format,args);

  va_end (args);
  buff[sizeof(buff)/sizeof(buff[0])-1]='\0';
  Serial.print(buff);
}

// String newwan="192.168.5.1";
// SerialPrint("New WAN IP  : %s\n" , newwan.c_str() );
