#include <ESP8266WiFi.h>
// PUBSUB 库中的默认有效负载 256 Bytes修改头文件PubSubClient.h #define MQTT_MAX_PACKET_SIZE 1536
#include <PubSubClient.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>  //红外头文件
#include <IRrecv.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>
#include <Ticker.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h> 
#include <WiFiManager.h>


const char* MQTTSERVER="xxx"; //设置MQTT服务器IP地址
const int MQTTPORT=xxx;  //设置MQTT服务器端口
const char* MQTTUSER="xxx";  //设置MQTT用户名
const char* MQTTPW="xxx";  //设置MQTT密码
const char* PUBTOPIC="xxx"; //设置发布主题
const char* SUBTOPIC="xxx";  //设置订阅主题




// 定义红外接收的管脚
const uint16_t kRecvPin = 14; // GPIO14 D5
const uint32_t kBaudRate = 115200;
const uint16_t kCaptureBufferSize = 1024;

#if DECODE_AC
const uint8_t kTimeout = 50;
#else   // DECODE_AC
const uint8_t kTimeout = 15;
#endif  // DECODE_AC
const uint8_t kTolerancePercentage = kTolerance; 
#define LEGACY_TIMING_INFO false
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;  // Somewhere to store the results

//定义红外发射的管脚
const uint16_t kIrLed = 12;  // 设置kIrLed为GPIO12，D6脚
IRsend irsend(kIrLed);  // 将kIrLed设置发送信息

// 持久化红外指令数组到闪存文件系统
String copy_base_filename = "/copy_signal";
#define COPY_N 8
uint16_t copy_signal[COPY_N][256];
uint16_t copy_length[COPY_N] = {0};
String copy_name[COPY_N];
int copy_cover = 0;
String comming_copy_name;
int copy_mode = 0;

// 同时执行的最大任务数
#define TASK_N 8
struct Task {
  int xid;
  uint64_t remain;
  uint64_t start;
  uint64_t freq;
  String cmd;
} tasklist[TASK_N];
// 执行队列
int exec_queue[TASK_N];
int eqsz = 0;

// 定期同步网络时间，断网则用本机时间
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

WiFiClient wc;
PubSubClient pc(wc);
// 收发json消息
JsonDocument doc;
DynamicJsonDocument rdoc(1536);



uint8_t connect_mqtt(){
  if(WiFi.status()!=WL_CONNECTED) return -1;
  pc.setServer(MQTTSERVER, MQTTPORT);
  if(!pc.connect(WiFi.macAddress().c_str(), MQTTUSER, MQTTPW)){     //以物理地址为ID去连接MQTT服务器
    Serial.println("connect MQTT fail");
    return -1;
  }
  pc.subscribe(SUBTOPIC);    
  pc.setCallback(sub_msg_hander);                        //绑定订阅回调函数
  Serial.println("connect MQTT success");
  return 0;
}

// 定时函数执行耗时操作会崩溃，主循环loop中检测到标记变量则执行耗时操作
Ticker wf, mt;
int tag_wifi=0, tag_mqtt=0;
void itv_wifi() { tag_wifi++; }
void itv_mqtt() { tag_mqtt++; }

// must after rdoc.clear()
void msg_pub_print(int code, const String& msg) {
  rdoc["chat_id"] = doc["chat_id"];
  rdoc["code"] = code;
  rdoc["message"] = msg;
  String result;
  serializeJson(rdoc, result);
  pc.publish(PUBTOPIC, result.c_str());
  Serial.println(result);
}

 
void solve_msg(String Msg) {
  doc.clear();
  rdoc.clear();
  deserializeJson(doc, Msg);
  String cmd = doc["cmd"];

  if (cmd == "taskidlist") {
    for (int i=0,j=0; i<TASK_N; i++) {
      if (tasklist[i].remain>0) {
        rdoc["taskids"][j] = i;
        j++;
      }
    }
    msg_pub_print(200, "get taskidlist ok");
  }

  if (cmd == "task") {
    int id = doc["id"];
    if (0 <= id && id < TASK_N && tasklist[id].remain>0) {
      rdoc["task"]["remain"] = tasklist[id].remain;
      rdoc["task"]["start"] = tasklist[id].start;
      rdoc["task"]["freq"] = tasklist[id].freq;
      rdoc["task"]["cmd"] = tasklist[id].cmd;
      rdoc["task"]["xid"] = tasklist[id].xid;
      rdoc["task"]["taskid"] = id;
      msg_pub_print(200, "get task ok");
    } else {
      msg_pub_print(400, "illegal task id");
    }
  }

  if (cmd == "tasklist") {
    for (int i=0,j=0; i<TASK_N; i++) {
      if (tasklist[i].remain>0) {
        rdoc["tasks"][j]["remain"] = tasklist[i].remain;
        rdoc["tasks"][j]["start"] = tasklist[i].start;
        rdoc["tasks"][j]["freq"] = tasklist[i].freq;
        rdoc["tasks"][j]["cmd"] = tasklist[i].cmd;
        rdoc["tasks"][j]["xid"] = tasklist[i].xid;
        rdoc["tasks"][j]["taskid"] = i;
        j++;
      }
    }
    msg_pub_print(200, "get tasklist ok");
  }

  if (cmd == "cmdlist") {
    // get no null
    for (int i=0,j=0; i<COPY_N; i++) {
      if (copy_name[i] != "") {
        rdoc["cmds"][j++] = copy_name[i];
      }
    }
    msg_pub_print(200, "get cmdlist ok");
  }

  if (cmd == "terminate") {
    int id = doc["taskid"];
    if (0 <= id && id < TASK_N && tasklist[id].remain>0) {
      tasklist[id].remain = 0;
      msg_pub_print(200, "terminate task ok");
    } else {
      msg_pub_print(400, "illegal task id");
    }
  }

  if (cmd == "exec") {                            // exec cmd
    String name = doc["name"];
    uint64_t start = doc["start"];
    uint64_t freq = doc["freq"];
    uint64_t remain = doc["remain"];
    // Serial.println(name+" "+start+" "+freq);
    int xid = 0;
    for (; xid<COPY_N; xid++) {
      if (name == copy_name[xid]) {
        break;
      }
    }
    // not exist name 
    if (xid == COPY_N) {
      msg_pub_print(400, "exec failure, cmd name ["+name+"] not exist!");
      return ;
    }
    // find first not use task place
    int task_id = 0;
    while (task_id<TASK_N && tasklist[task_id].remain > 0) task_id++;
    if (task_id == TASK_N) {
      msg_pub_print(400, String("exec failure, tasklist is full, max is ")+TASK_N);
      return ;
    }
    tasklist[task_id].remain = remain;
    tasklist[task_id].freq = freq;
    tasklist[task_id].start = start;
    tasklist[task_id].cmd = name;
    tasklist[task_id].xid = xid;
    msg_pub_print(200, "add "+name+" to tasklist");
  }

  if (cmd == "copy") {                   // copy cmd 
    String name = doc["name"];
    String old = doc["old"];
    Serial.println(name+" "+old);
    // check old exist
    int i = 0;
    if (old != "") {
      for (; i<COPY_N; i++) {
        if (old == copy_name[i]) {
          copy_cover = i;
          break;
        }
      }
      // not exist
      if (i == COPY_N) {
        msg_pub_print(400, "copy failure: old name ["+old+"] not exist!");
        return ;
      }
    } else { // no old
      // check update
      for (i=0; i<COPY_N; i++) {
        if (name == copy_name[i]) {
          copy_cover = i;
          break;
        }
      }
      // if no updae then try find not allocate
      if (i == COPY_N)
      for (i=0; i<COPY_N && copy_length[copy_cover] != 0; i++) {
        copy_cover = (copy_cover+1)%COPY_N;
      }
    }
      
    comming_copy_name = name;
    copy_mode = 1;
    msg_pub_print(200, "copy start ok");
  }
}

void sub_msg_hander(char* topic,byte* payload,unsigned int length){
  Serial.printf("MQTT subscribe data from topic: %s\r\n",topic);    //输出调试信息,得知是哪个主题发来的消息
  for(unsigned int i=0;i<length;++i){             //读出信息里的每个字节
    Serial.print((char)payload[i]);             //以文本形式读取就这样,以16进制读取的话就把(char)删掉
  }
  Serial.println();
  char msg[128];
  if (length<128) {
    for (int i=0; i<length; i++) {
      msg[i] = (char)payload[i];
    }
    msg[length] = '\0';
  }
  String Msg = String(msg);
  solve_msg(Msg);
}

// 持久化学到的红外信号
void save_copy() {
  String prename = copy_name[copy_cover];
  copy_name[copy_cover] = comming_copy_name;
  File f = SPIFFS.open(copy_base_filename+copy_cover, "w");
  f.write((char *)&copy_length[copy_cover], 2);
  f.write((char *) copy_signal[copy_cover], copy_length[copy_cover]*2);
  f.write(copy_name[copy_cover].c_str());
  f.close();
  copy_cover = (copy_cover+1)%COPY_N;
  
  
  rdoc.clear();
  msg_pub_print(200, "copy ["+comming_copy_name+"] success, repalce [" + prename + "]");
}


// 解析收到的红外数组
void record_copy(String& sc) {
  int p = 0;
  uint16_t *ls = copy_signal[copy_cover], *ll = &copy_length[copy_cover];
  *ll = 0;
  while (p<sc.length() && sc.charAt(p) != '{') p++;
  uint16_t num = 0;
  for (;p<sc.length(); p++) {
    char ch = sc.charAt(p);
    if ('0'<=ch && ch<='9') {
      num = num*10 + ch-'0';
    } else if (num != 0) {
      ls[(*ll)++] = num;
      num = 0;
    }
    if (ch == '}') break;
  }

  Serial.printf("%s recored to copy_signal[%d] size: %d \r\n", copy_name[copy_cover].c_str(), copy_cover, *ll);
  for (int i=0; i<*ll; i++) {
    Serial.printf("%d ", ls[i]);
  }
  Serial.println("");
}


void setup() {
  // led bultin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // serial
  Serial.begin(kBaudRate, SERIAL_8N1, SERIAL_TX_ONLY);
  while (!Serial)  // Wait for the serial connection to be establised.
  delay(50);
  Serial.println();
  
  // irsend 
  irsend.begin();
  
  // irrecv
  irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.
  irrecv.enableIRIn();  // Start the receiver
  
  WiFiManager wifiManager;
  if (!wifiManager.autoConnect("ESP8266_AP", "12345678")) { // 闪存中默认配置如果无法连接，则打开AP输入配置持久化后重启
    Serial.println("Failed to connect, restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.printf("macAddress is %s\r\n",WiFi.macAddress().c_str());  
  connect_mqtt();  // 连接MQTT
  wf.attach(30, itv_wifi);
  mt.attach(5, itv_mqtt);

  // file system
  SPIFFS.begin();
  
  // load copyed IR
  for (int i=0; i<COPY_N; i++) {
    File f = SPIFFS.open(copy_base_filename+i, "r");
    if (f) {
      Serial.println(copy_base_filename + i + " content:");
      
      // load from disk
      f.read((uint8_t *) &copy_length[i], 2);
      f.read((uint8_t *) copy_signal[i], copy_length[i]*2);
      copy_name[i] = f.readString();
      f.close();

      // print
      Serial.println(copy_length[i]);
      for (int t=0; t<copy_length[i]; t++)
        Serial.printf("%d ", copy_signal[i][t]);
      Serial.println("");  
      Serial.println(copy_name[i]);
    }
  }
  
  // ntp
  timeClient.begin();
  timeClient.update();
  Serial.println(timeClient.getFormattedTime() + " " + timeClient.getEpochTime());
}

void loop() {
  // task slover
  for (int i=0; i<TASK_N; i++) {
    if (tasklist[i].remain > 0 && tasklist[i].start <= timeClient.getEpochTime()) {
      exec_queue[eqsz++] = tasklist[i].xid;
      uint64_t remain = tasklist[i].remain, freq = tasklist[i].freq;
      if (tasklist[i].start+freq<timeClient.getEpochTime()) 
        tasklist[i].start = timeClient.getEpochTime();
      if (remain>1) {
          tasklist[i].start += freq;
      }
      tasklist[i].remain--;
    }
  }
  // exec task
  while (eqsz>0) {
    int xid = exec_queue[--eqsz];
    irsend.sendRaw(copy_signal[xid], copy_length[xid], 38);
    rdoc.clear();
    msg_pub_print(200, "task:exec "+copy_name[xid]+" success");
  }
  // copy cmd
  if (copy_mode) {
    irrecv.resume();
    digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
    // print all signal in 10s
    uint64_t ts = millis();
    Serial.printf("current ts:%d\r\n", ts);
    uint8_t is_copy = 0;
    while (ts+10000>millis()) { // 10s内可学习
      if (irrecv.decode(&results)) {
        is_copy = 1;
        // Display the basic output of what we found.
        Serial.print(resultToHumanReadableBasic(&results));
        // Display any extra A/C info if we have it.
        String description = IRAcUtils::resultAcToString(&results);
        if (description.length()) Serial.println(D_STR_MESGDESC ": " + description);
        String sc = resultToSourceCode(&results);
        Serial.println(sc);
        record_copy(sc);
        irrecv.resume();
      }
      yield(); // 喂狗
    }
    if (is_copy)
      save_copy();
    Serial.printf("current ts:%d\r\n", millis());
    digitalWrite(LED_BUILTIN, HIGH);
    copy_mode = 0;
  }

  // check wifi
  if (tag_wifi>=1) {
    // Serial.print("itv check WiFi: ");
    if(WiFi.status()!=WL_CONNECTED){
      Serial.println("WiFi disconnected, trying to reconnect...");
      WiFi.reconnect();
    } else {
      // Serial.println("WiFi connected");
    }
    tag_wifi = 0;
  }
  // check mqtt
  if (tag_mqtt>=1) {
    // Serial.print("itv check mqtt: ");
    if(WiFi.status()==WL_CONNECTED && pc.connected()){ 
        // Serial.println("mqtt connected");
        pc.loop();                                      // 发送心跳信息
    }else{
      Serial.println("mqtt disconnected, trying to reconnect...");
      connect_mqtt();                                  // 如果和MQTT服务器断开连接,那么重连
    }
    tag_mqtt = 0;
    timeClient.update();
    // Serial.println(timeClient.getFormattedTime()+" "+timeClient.getEpochTime());
  }
}   