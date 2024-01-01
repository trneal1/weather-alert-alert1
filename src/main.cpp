#include <Arduino.h>

#include <sntp.h>

#include <EEPROM.h>

#include <Wifi.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>

#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/FreeRTOSConfig.h>

#include <HTTPClient.h>
#define ARDUINOJSON_COMMENTS_ENABLE 1
#include <ArduinoJSON.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <WROVER_KIT_LCD.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

void task1(void *);
void task2(void *);
static xSemaphoreHandle lock;

xTaskHandle htask1, htask2;

const char *ssid = "TRNNET-2G";
const char *password = "ripcord1";
const char *hostname = "ESP_WALERT";

WiFiUDP udp;

const char areas[100] = "NCC183/NCC063/NCC069/SCZ056/MDC027";   // MDC027
const char *codes[] = {"NC W", "NC D", "NC F", "SC G", "MD H"}; // MD H

unsigned long connects = 0;
unsigned long badhttp = 0;

byte reboots;
byte restarts;

long int times1[5][3];

struct SpiRamAllocator
{
   void *allocate(size_t size)
   {
      return ps_malloc(size);
   }
   void deallocate(void *pointer)
   {
      free(pointer);
   }
};
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

WROVER_KIT_LCD tft;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -5 * 3600);

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// WebServer server(80);

char *content[5];
char *desc;

//////////////////////////////////////////////////////////////////////////////////

#define _TASK_WDT_IDS
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include <TaskScheduler.h>
#include <WiFiUDP.h>
#include <string.h>
#include <arduino.h>

#define ledpin 13
#define numtasks 8
#define maxcolors 32
#define port 4100
#define commandlen 128

void parse_command();
void check_udp();
void t1_callback();

WiFiUDP Udp;
WiFiUDP Udp1;
WiFiServer Tcp(port);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(numtasks, ledpin, NEO_GRB + NEO_KHZ800);
Scheduler runner;

Task *tasks[numtasks];

unsigned int numcolors[numtasks], currcolor[numtasks];
unsigned long colors[numtasks][maxcolors];
unsigned long steps[numtasks][maxcolors], currstep[numtasks];
unsigned char first[numtasks];
char command[commandlen + 1];
///////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////
void check_udp()
{

   int packetsize = Udp.parsePacket();
   if (packetsize)
   {

      memset(command, 0, commandlen + 1);
      Udp.read(command, commandlen);

      Serial.print("UDP: ");
      Serial.println(command);

      parse_command();
   }
}

void check_tcp()
{

   static WiFiClient client;
   static long unsigned noDataCnt;

   if (!client)
   {
      noDataCnt = 0;
      client = Tcp.available();
      if (client)
      {
         Serial.print("Connect:   ");
         Serial.println(client.remoteIP());
      }
   }
   else
   {
      int test = client.available();
      // Serial.print(client.remoteIP());
      // Serial.print("    ");
      // Serial.println(noDataCnt);
      if (test > 0)
      {
         noDataCnt = 0;
         memset(command, 0, commandlen + 1);
         client.readBytesUntil('\n', command, commandlen);

         Serial.print("TCP: ");
         Serial.println(command);
         parse_command();
      }
      else
      {
         noDataCnt++;
         // if(noDataCnt>5012)
         // client.stop(0);
      }
   }
}

void parse_command()
{

   unsigned ledId, pause;
   char *ptr, *ptr1;
   char *sav, *sav1;

   ptr = strtok_r(command, " ", &sav);
   ledId = atoi(ptr);
   (*tasks[ledId]).disable();

   currcolor[ledId] = 0;
   currstep[ledId] = 0;
   ptr = strtok_r(NULL, " ", &sav);

   if (strcmp(ptr, "+"))
   {
      numcolors[ledId] = 0;
      pause = atoi(ptr);
      (*tasks[ledId]).setInterval(pause);
   }

   while (ptr != NULL)
   {
      ptr = strtok_r(NULL, " ", &sav);
      if (ptr != NULL)
      {

         ptr = strupr(ptr);
         if (strchr(ptr, 'X') != NULL)
         {
            ptr1 = strtok_r(ptr, "X", &sav1);
            steps[ledId][numcolors[ledId]] = atol(ptr1);
            ptr1 = strtok_r(NULL, "X", &sav1);
         }
         else
         {
            steps[ledId][numcolors[ledId]] = 1;
            ptr1 = ptr;
         }
         colors[ledId][numcolors[ledId]] = strtol(ptr1, NULL, 16);
         numcolors[ledId]++;
      }
   }
   strip.setPixelColor(ledId, colors[ledId][currcolor[ledId]]);
   strip.show();
   first[ledId] = 1;
   (*tasks[ledId]).enable();
}

void t1_callback()
{
   int ledId;

   Task &taskRef = runner.currentTask();
   ledId = taskRef.getId();

   if (steps[ledId][currcolor[ledId]] != 0 and not first[ledId])
   {
      if (currstep[ledId] == steps[ledId][currcolor[ledId]] - 1)
      {

         if (currcolor[ledId] < numcolors[ledId] - 1)
            currcolor[ledId]++;
         else
            currcolor[ledId] = 0;

         currstep[ledId] = 0;
         strip.setPixelColor(ledId, colors[ledId][currcolor[ledId]]);
         strip.show();
      }
      else
         currstep[ledId]++;
   }
   first[ledId] = 0;
}
//////////////////////////////////////////////////////////////////////////////////////////

void tft_init()
{
   tft.fillScreen(WROVER_BLACK);
   tft.setRotation(1);
   tft.setCursor(0, 0);
   tft.setTextColor(WROVER_YELLOW);
   tft.setTextSize(2);
}

void connect()
{
   int attempts = 0;
   connects++;
   tft_init();
   WiFi.mode(WIFI_STA);
   WiFi.setSleep(false);

   WiFi.hostname(hostname);
   WiFi.disconnect();
   WiFi.begin(ssid, password);
   while (WiFi.status() != WL_CONNECTED)
   {
      if (attempts > 15 || WiFi.status() == 4)
      {
         tft_init();
         tft.println("Restarting");

         restarts = restarts + 1;
         EEPROM.write(1, restarts);
         EEPROM.commit();

         ESP.restart();
      }

      tft.printf("connecting, %d %ld %d\n\r", WiFi.status(), connects, attempts);
      attempts++;
      delay(2000);
   }
   tft_init();
   tft.println("Connected!");
   tft.println(WiFi.localIP());
   tft.println(WiFi.SSID());
   delay(2000);
}

void setup1()
{

   lock = xSemaphoreCreateMutex();

   xTaskCreatePinnedToCore(
       task1,   /* Task function. */
       "task1", /* String with name of task. */
       10000,   /* Stack size in bytes. */
       NULL,    /* Parameter passed as input of the task */
       1,       /* Priority of the task. */
       &htask1, /* Task handle. */
       1);      /* Core */

   xTaskCreatePinnedToCore(
       task2,   /* Task function. */
       "task2", /* String with name of task. */
       10000,   /* Stack size in bytes. */
       NULL,    /* Parameter passed as input of the task */
       1,       /* Priority of the task. */
       &htask2, /* Task handle. */
       0);      /* Core */
}

void setup2()
{

   for (int i = 0; i <= numtasks - 1; i++)
   {
      numcolors[i] = 1;
      currcolor[i] = 0;
      currstep[i] = 0;
      first[i] = 0;
      for (int j = 0; j <= maxcolors - 1; j++)
      {
         colors[i][j] = 0;
         steps[i][j] = 1;
      }
   }

   runner.init();
   for (int i = 0; i <= numtasks - 1; i++)
   {
      tasks[i] = new Task(500, TASK_FOREVER, &t1_callback);
      (*tasks[i]).setId(i);

      runner.addTask((*tasks[i]));
      (*tasks[i]).enable();
   }
   strip.begin();

   Udp.begin(port);
   Tcp.begin();
}

void setup(void)
{

   // content = (char **)ps_calloc(5, sizeof(char *));
   for (int i = 0; i <= 4; i++)
   {
      content[i] = (char *)ps_calloc(100000, sizeof(char));
   }

   desc = (char *)ps_calloc(100000, sizeof(char));

   String http_filename;

   EEPROM.begin(2);
   // EEPROM.write(0,0);
   // EEPROM.write(1,0);
   // EEPROM.commit();

   reboots = EEPROM.read(0) + 1;
   restarts = EEPROM.read(1);
   EEPROM.write(0, reboots);
   EEPROM.commit();

   tft.begin();
   psramInit();
   Serial.begin(9600);
   connect();

   timeClient.begin();
   timeClient.update();

   ArduinoOTA.begin();

   server.on("/area", HTTP_GET, [](AsyncWebServerRequest *request)
             {
       
        AsyncWebParameter* n = request->getParam("num");
        request->send(200,"text/plain",String(content[n->value().toInt()])); });

   server.on("/desc", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(200, "text/plain", desc); });

   server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request)
             {
   
//xSemaphoreTake(lock, portMAX_DELAY);
          AsyncResponseStream *response = request->beginResponseStream("text/plain");
          response->printf("Time: %ld\r",millis()/1000);
          response->printf("Connects: %d\r",connects);
                response->printf("Bad HTTP: %d\r",badhttp);
          response->printf("Reboots: %d\r",reboots);
          response->printf("Restarts: %d\r\r",restarts);
          response->println("\t\tsize\tload\tproc\r");

          for (int i=0;i<=4;i++)
             response->printf("Area[%d]:\t%ld\t%lu\t%lu\r", i, strlen(content[i]), (times1[i][1]-times1[i][0]),(times1[i][2]-times1[i][1]));

response->println();
 response->printf("Desc: %ld\r\r\r",strlen(desc));

  response->printf("Stack task1: %d\r",uxTaskGetStackHighWaterMark(htask1));
  response->printf("Stack task2: %d\r",uxTaskGetStackHighWaterMark(htask2));
 request->send(response); 
 //xSemaphoreGive(lock);
 });

   server.on("/flush", HTTP_GET, [](AsyncWebServerRequest *request)
             {
       EEPROM.write(0,0);
       EEPROM.write(1,0);
       EEPROM.commit();

       reboots=1;
       restarts=0;
       connects=1;
       badhttp=0;

      request->send(200,"text/html","Statistics flushed"); });

   server.begin();

   setup1();
   setup2();
}

void task2(void *parameter)
{

   for (;;)
   {
      ////////////////////////////////////////////////////////////////
      runner.execute();
      check_udp();
      check_tcp();
      ////////////////////////////////////////////////////////////////
      vTaskDelay(100);
   }
}

void loop(void)
{
   timeClient.update();
   ArduinoOTA.handle();
   vTaskDelay(5000);
}

void task1(void *parameter)
{
#define eos(s) ((s) + strlen(s))

   WiFiClientSecure client;
   client.setInsecure();

   SpiRamJsonDocument doc(1000000);

   for (;;)
   {

      HTTPClient http;
      int httpcode;
      char areasbuff[100];
      char mergebuff[100];

      char *area;
      char localcode[10];

      int textcolor, textcolorh;
      const char *tempbuf;

      DeserializationError error;

      int areanum = 0;
      int oldarea = 999;
      int localnum = 0;

      xSemaphoreTake(lock, portMAX_DELAY);

      if (WiFi.status() != WL_CONNECTED)
         connect();

      memset(desc, 0, 100000);

      tft_init();
      tft.print("Weather Alerts   ");
      tft.println(timeClient.getFormattedTime());

      strcpy(areasbuff, areas);
      area = strtok(areasbuff, "/");

      while (area != NULL)
      {

         sprintf(eos(desc), "######################################\n");
         sprintf(eos(desc), "%s\n", area);
         sprintf(eos(desc), "######\n");
         strcpy(mergebuff, "https://api.weather.gov/alerts/active?zone=");
         strcat(mergebuff, area);

         http.useHTTP10(true);
         http.begin(client, mergebuff);
         http.setTimeout(60000);

         times1[areanum][0] = millis();

         httpcode = http.GET();

         if (httpcode != 200)
         {
            badhttp++;

            tft.setTextSize(1);
            tft.setTextColor(WROVER_ORANGE);
            tft.print(area);
            tft.print(": ");
            tft.print(F("Http get error:"));
            tft.println(httpcode);
            http.end();
            client.stop();

            areanum++;
            area = strtok(NULL, "/");
            continue;
         }

         memset(content[areanum], 0, 100000);
         http.getStream().readBytes(content[areanum], 100000);
         tempbuf = content[areanum];

         times1[areanum][1] = millis();
         error = deserializeJson(doc, tempbuf);
         times1[areanum][2] = millis();

         if (error)
         {
            // tft_init();
            tft.setTextSize(1);
            tft.setTextColor(WROVER_ORANGE);
            tft.print(area);
            tft.print(": ");
            tft.print(F("deserializeJson() failed: "));
            tft.println(error.f_str());

            http.end();
            client.stop();

            areanum++;
            area = strtok(NULL, "/");
            continue;
         }

         http.end();
         client.flush();
         client.stop();

         tft.setTextSize(1);
         JsonArray jarray = doc["features"];
         for (int i = 0; i < jarray.size(); i++)
         {

            if (oldarea != areanum && oldarea != 999)
               tft.println("");

            oldarea = areanum;

            strcpy(localcode, "002000");
            textcolor = WROVER_GREEN;

            if (strstr(jarray[i]["properties"]["event"], "Watch") != NULL)
            {
               strcpy(localcode, "202000");
               textcolor = WROVER_YELLOW;
            }
            else if (strstr(jarray[i]["properties"]["event"], "Warning") != NULL)
            {
               if (strstr(jarray[i]["properties"]["event"], "Tornado") != NULL)
               {
                  strcpy(localcode, "FFFFFF");
                  textcolor = WROVER_WHITE;
               }
               else
               {
                  strcpy(localcode, "200000");
                  textcolor = WROVER_RED;
               }
            }
            else if (strstr(jarray[i]["properties"]["event"], "Statement") != NULL)
            {
               strcpy(localcode, "000020");
               textcolor = WROVER_BLUE; // blue
            }

            /*
                        int hour = timeClient.getHours();
                        if ((hour >= 21 || hour < 6) && strcmp(localcode, "FFFFFF") != 0)
                        {
                           strcpy(localcode, "000000");
                        }
            */
            if (areanum == 0)
            {
               udp.beginPacket("192.168.1.2", 4100);
               udp.print(localnum);
               udp.print(" 10000 8x");
               udp.print(localcode);
               udp.print(" 0x000000\n");
               udp.endPacket();

               vTaskDelay(500);

               localnum++;
            }

            if (areanum == 0)
               textcolorh = WROVER_MAGENTA;
            else
               textcolorh = WROVER_GREEN;

            tft.setTextColor(textcolorh);
            tft.setTextSize(1);
            tft.print(codes[areanum]);
            tft.print("  ");

            tft.setTextColor(textcolor);
            tft.setTextSize(1);

            if (jarray[i]["properties"]["event"].as<const char *>() != NULL)
            {
               sprintf(eos(desc), "%s\n", jarray[i]["properties"]["event"].as<const char *>());
               tft.print(jarray[i]["properties"]["event"].as<const char *>());
               tft.print("  ");
            }

            if (jarray[i]["properties"]["parameters"]["NWSheadline"][0].as<const char *>() != NULL)
               sprintf(eos(desc), "%s\n", doc["features"][i]["properties"]["parameters"]["NWSheadline"][0].as<const char *>());

            if (jarray[i]["properties"]["description"].as<const char *>() != NULL)
               sprintf(eos(desc), "%s\n\n", jarray[i]["properties"]["description"].as<const char *>());

            if (jarray[i]["properties"]["effective"].as<const char *>() != NULL)
               sprintf(eos(desc), "%s\n", jarray[i]["properties"]["effective"].as<const char *>());

            if (jarray[i]["properties"]["expires"].as<const char *>() != NULL)
            {
               sprintf(eos(desc), "%s\n", jarray[i]["properties"]["expires"].as<const char *>());
               memset(mergebuff, 0, 100);
               strncpy(mergebuff, jarray[i]["properties"]["expires"].as<const char *>(), 16);
               tft.println(mergebuff);
            }
            sprintf(eos(desc), "%s\n", "------------------------------------------------------------------------------");
         }
         areanum++;
         area = strtok(NULL, "/");
      }

      sprintf(eos(desc), "$$$$$\n\r");

      tft.setTextColor(WROVER_GREEN);
      tft.setTextSize(1);
      tft.printf("$$ t=%ld c=%ld h=%ld b=%d r=%d", millis() / 1000, connects, badhttp, reboots, restarts);

      xSemaphoreGive(lock);
      vTaskDelay(60000);
   }
}