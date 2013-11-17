/*
WebserverHttpgetJsonres
- connect to ethernet in DHCP mode
- send IP on serial output
- wait for incoming http request with format http://aaa.bbb.ccc.ddd/droittle
- parse "int" commands from predefined set : loco,power,dir,f1,f2,f3,f4,f5,f6,f7,f8
- answer command status in json format
*/

// include
#include <SPI.h>
#include <Ethernet.h>

// globals
char cmd;
char _buf[50];
int ethok=0;
byte mac[] = { 0x00,0xAA,0xBB,0xCC,0xDE,0x02 };
IPAddress ip;
// command status
int tmp;  //generic
int i;
boolean found;
word loco,power,functions,flags;  //loco
word flags_x;  //
word locox[16]; //loco controlled
word countx[16]; //timeout

byte state, incomingbyte, checksum, expbyte, command_typ, command_len;
byte ah,al,pw,chk,stp,f,md;
byte param[15];

boolean isreqline = true;
String req = String();
String par = String();

// Ethernet server
EthernetServer server(80);

// the setup routine runs once when you press reset:
void setup() {
  // init command status
  tmp=-1; 
  loco=0; 
  power = 0;
  functions=0;
  flags=0;
  flags_x=0;
  state=0;
  for (i=0; i < 16; i++) {
    locox[i]=0;
    countx[i]=0;
  }

  // open serial communications and wait for port to open:
  Serial.begin(9600,SERIAL_8N1);
  while (!Serial) {
    ; // wait for serial port to connect
  }
  Serial.println("-- UART READY");
  
  // ethernet setup (using DHCP can be blocking for 60 seconds)
  ethok = Ethernet.begin(mac);
  if (ethok==0) Serial.println("ERROR : failed to configure Ethernet");
  else {
    ip = Ethernet.localIP();
    formatIP(ip);
    Serial.print("-- IP ADDRESS is ");
    Serial.println(_buf);
  }
}

// loop routine
void loop() {
  
  // check connected clients timeouts
  for (i=0; i < 16; i++) {
    if (locox[i]!=0) {
      if (countx[i]!=0) countx[i]--;
      else {
        ah=highByte(locox[i]);
        if (locox[i]>0x63) ah|=0xC000;
        al=lowByte(locox[i]);
        chk=0x92^ah^al;
        Serial.write(0x92); 
        Serial.write(ah);
        Serial.write(al);
        Serial.write(chk);        
        locox[i]=0;
      }
    }
  }  
  
  // serial data management
  if (Serial.available() > 0) {
    incomingbyte = Serial.read();
    fsm_serial();
  }  
 
  // web server
  if (ip[0]!=0) {
    EthernetClient client = server.available(); // listen to client connecting
    if (client) {
      //Serial.println("ETHERNET : new client http request");
      boolean currentLineIsBlank = true; // an http request ends with a blank line
      while (client.connected()) {
        if (client.available()) {
          // read char from client
          char c = client.read();
          // append to request string
          if ((isreqline)&&(req.length()<63)) req += c;
          // stop parsing after first line
          if (c=='\n') isreqline = false;
          
          // if you've gotten to the end of the line (received a newline character) and the line is blank,
          // the http request has ended, so you can send a reply
          if ((c=='\n') && currentLineIsBlank) {
            
            // if request does not contain "cantuina" keyword send 404
            if (req.indexOf("droittle")==-1) { 
              //send_404(client);
              //Serial.println("404 - Not Found"); 
             }
            // else apply command and send JSON response
            else {
              // trim request and logging url
              req = req.substring(req.indexOf("/"),req.lastIndexOf(" HTTP"));
              //Serial.println(req);
              // parsing request, update commands only if detected

              tmp = parseReq("lo");    //loco address 00aa.aaaa aaaa.aaaa
              if (tmp!=-1) loco = tmp;
  
              tmp = parseReq("pw");    //loco speed, 0000.0000 0sss.ssss
              if (tmp!=-1) power = tmp;

              tmp = parseReq("fu");    //function(s) state, from 000f.ffff ffff.ffff
              if (tmp!=-1) functions = tmp;

              tmp = parseReq("fl");            //0000.00 step2 step1    000 direction . 0 stop resume off
              if (tmp!=-1) flags = tmp;

              //brodcast check
              
              if ((flags & 0x0001)!=0){      //track power off request
                Serial.write(0x21); 
                Serial.write(0x80);
                Serial.write(0xA1);                    
              }
              if ((flags & 0x0002)!=0){      //resume normal operations request
                Serial.write(0x21); 
                Serial.write(0x81);
                Serial.write(0xA0);                    
              }
              if ((flags & 0x0004)!=0){      //loco emy stop request
                Serial.write(0x80); 
                Serial.write(0x80);                    
              }
              
              // loco management
              
              if (flags_x==0x0){
                
                stp=(flags & 0x0300) >> 8;
                
                found=false;                              //loco not found in the table
                for (i=0; i < 16; i++){                   //scan the table 
                  if (loco==locox[i]){                    //if found... 
                    found=true;                           //set the flag that we found it 
                    countx[i]=40000;                       //and update the timeout
                  }
                }
                if (!found) {                             //if, after scan, we not found           
                  found=false;                            //now "found" means "I found one free place in the table"  
                  for (i=0; i < 16; i++){                 //scan the table 
                    if ((locox[i]==0) && (!found)) {      //until we find one free place 
                    locox[i]=loco;                        //put the loco addr
                    countx[i]=40000;                       //and timeout 
                    found=true;  
                  }
                }
                }
                
                ah=highByte(loco);
                if (loco>99) ah |= 0xC0;
                al=lowByte(loco);
                switch (stp) {
                  case 0:
                    md=0x10;
                    if (power==0) pw=0; else pw=byte(power+1);
                  break;
                  case 1:
                    md=0x11;
                    if (power==0) pw=0; else {
                      pw=byte(power+3);
                      if ((pw & 0x01)!=0) pw|=0x20;
                      pw=pw >> 1;
                    }
                  break;
                  case 2:
                    md=0x12;
                    if (power==0) pw=0; else {
                      pw=byte(power+3);
                      if ((pw & 0x01)!=0) pw|=0x20;
                      pw=pw >> 1;
                    }
                  break;
                  case 3:
                    md=0x13;
                    if (power==0) pw=0; else pw=byte(power+1);
                  break;
                }
                if ((flags & 0x0010)!=0) pw|=0x80;        
                chk=0xE4^md^ah^al^pw;
                Serial.write(0xE4); 
                Serial.write(md);
                Serial.write(ah);
                Serial.write(al);
                Serial.write(pw);
                Serial.write(chk);
                
                f=highByte(functions);      //functions = X X X F0 . F4 F3 F2 F1     F8 F7 F6 F5 . F12 F11 F10 F9
                chk=0xE4^0x20^ah^al^f;
                Serial.write(0xE4); 
                Serial.write(0x20);
                Serial.write(ah);
                Serial.write(al);
                Serial.write(f);
                Serial.write(chk);
                
                f=(lowByte(functions) & 0xF0) >> 4;
                chk=0xE4^0x21^ah^al^f;
                Serial.write(0xE4); 
                Serial.write(0x21);
                Serial.write(ah);
                Serial.write(al);
                Serial.write(f);
                Serial.write(chk);

                f=(lowByte(functions) & 0x0F);
                chk=0xE4^0x22^ah^al^f;
                Serial.write(0xE4); 
                Serial.write(0x22);
                Serial.write(ah);
                Serial.write(al);
                Serial.write(f);
                Serial.write(chk);
                
              }


              // send JSON response
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json"); // JSON response type
              client.println("Connection: close"); // close connection after response
              client.println();
              // open JSON
              client.print("{");
              // loco
              client.print(",\n\"lo\":\""); client.print(loco); client.print("\"");
              client.print(",\n\"pw\":\""); client.print(power); client.print("\"");
              client.print(",\n\"fu\":\""); client.print(functions); client.print("\"");
              client.print(",\n\"fl\":\""); client.print(flags); client.print("\"");
//              client.print(",\n\"bc\":\""); client.print(flags_x); client.print("\"");
              client.print(",\n\"bc\":\""); client.print(flags_x); client.print("\"");
              // close json
              client.println("\n}");
            }

            // prepare for next request
            req = "";
            isreqline = true;
            // exit
            break;
          }
          if (c == '\n') {
            // you're starting a new line
            currentLineIsBlank = true;
          }
          else if (c != '\r') {
            // you've gotten a character on the current line
            currentLineIsBlank = false;
          }
        }
      }
      // give the web browser time to receive the data
      delay(1);
      // close the connection:
      client.stop();
    }
  }
}

int parseReq(String param) {
  par = "";
  int ind = req.indexOf(param);
  if (ind==-1) return -1;
  int ind2 = 1+req.indexOf("=",ind);
  int ind3 = req.indexOf("&",ind);
  if (ind3==-1) ind3 = req.length();
  par = req.substring(ind2,ind3);
  return par.toInt();
}

void parseReqs(String param) {
  par = "";
  int ind = req.indexOf(param);
  if (ind==-1) 
  {
    par="";
  }
  else
  {
  int ind2 = 1+req.indexOf("=",ind);
  int ind3 = req.indexOf("&",ind);
  if (ind3==-1) ind3 = req.length();
  par = req.substring(ind2,ind3);
  }
}

void formatIP(IPAddress ii) {
  sprintf(_buf,"%d.%d.%d.%d",ii[0],ii[1],ii[2],ii[3]);
}

/*
static void send_404(EthernetClient client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println();
  client.println("404 NOT FOUND");
}
*/

void fsm_serial() {
switch (state) {
    case 0:        //wait for a new header byte
//      Serial.write(0x10);Serial.write(0x10);
      checksum=incomingbyte;
      command_len=incomingbyte & 0x0F;
      expbyte=0;
      command_typ=(incomingbyte & 0xF0) >> 4;
      if (command_len!=0) state=1; else state=2; 
    break;
    case 1:
//      Serial.write(0x20);Serial.write(0x20);
      checksum=checksum^incomingbyte;
      param[expbyte]=incomingbyte;
      expbyte++;
      if (expbyte==command_len) state=2; else state=1;
    break;
    case 2:
//      Serial.write(0x30);Serial.write(0x30);
      if (checksum==incomingbyte){
          if (command_typ==0x06) {
            if ((command_len==1) && (param[0]==0x0)) flags_x|=0x01;   //track power off
            if ((command_len==1) && (param[0]==0x1)) flags_x=0x00;    //normal op resumed
            if ((command_len==1) && (param[0]==0x2)) flags_x|=0x04;   //service mode entry
            
        } 
          if (command_typ==0x08) {
            if ((command_len==1) && (param[0]==0x0)) flags_x|=0x02;   //emergency stop
            
          } 
          
        }
      state=0;
    break;
    
    default:
      state=0;
  }  
}

