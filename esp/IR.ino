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

/* cron matcher */

// 东八区时差（秒）
#define TIMEZONE_OFFSET (8 * 3600)

// cron字段结构
typedef struct {
    int sec[60];     // 秒 0-59
    int min[60];     // 分钟 0-59
    int hour[24];    // 小时 0-23
    int day[32];     // 日 1-31
    int month[13];   // 月 1-12
    int wday[8];     // 星期 0-7 (0和7都表示周日)
} CronPattern;

// 解析数字范围，如 "1-5" 或 "*/2"
int parse_range(const char *field, int *array, int min_val, int max_val) {
    char *token, *saveptr;
    char field_copy[256];
    strcpy(field_copy, field);
    
    token = strtok_r(field_copy, ",", &saveptr);
    while (token != NULL) {
        // 处理 "*" 或 "*/n"
        if (token[0] == '*') {
            int step = 1;
            if (strlen(token) > 1 && token[1] == '/') {
                step = atoi(token + 2);
            }
            for (int i = min_val; i <= max_val; i += step) {
                array[i] = 1;
            }
        }
        // 处理范围 "a-b" 或 "a-b/c"
        else if (strchr(token, '-') != NULL) {
            char *dash = strchr(token, '-');
            *dash = '\0';
            int start = atoi(token);
            
            char *end_part = dash + 1;
            int end, step = 1;
            
            if (strchr(end_part, '/') != NULL) {
                char *slash = strchr(end_part, '/');
                *slash = '\0';
                end = atoi(end_part);
                step = atoi(slash + 1);
            } else {
                end = atoi(end_part);
            }
            
            for (int i = start; i <= end; i += step) {
                if (i >= min_val && i <= max_val) {
                    array[i] = 1;
                }
            }
        }
        // 处理单个数字
        else {
            int num = atoi(token);
            if (num >= min_val && num <= max_val) {
                array[num] = 1;
            }
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return 0;
}

// 解析cron表达式
int parse_cron(const char *cron_expr, CronPattern *pattern) {
    memset(pattern, 0, sizeof(CronPattern));
    char expr_copy[512];
    strcpy(expr_copy, cron_expr);
    
    char *fields[6];
    int field_count = 0;
    
    // 分割字段
    char *token = strtok(expr_copy, " \t");
    while (token != NULL && field_count < 6) {
        fields[field_count++] = token;
        token = strtok(NULL, " \t");
    }
    
    // 支持5字段和6字段两种格式
    if (field_count == 5) {
        // 5字段格式 (分 时 日 月 星期)
        pattern->sec[0] = 1; // 默认0秒执行
        parse_range(fields[0], pattern->min, 0, 59);     // 分钟
        parse_range(fields[1], pattern->hour, 0, 23);    // 小时
        parse_range(fields[2], pattern->day, 1, 31);     // 日
        parse_range(fields[3], pattern->month, 1, 12);   // 月
        parse_range(fields[4], pattern->wday, 0, 7);     // 星期
    } else if (field_count == 6) {
        // 6字段格式 (秒 分 时 日 月 星期)
        parse_range(fields[0], pattern->sec, 0, 59);     // 秒
        parse_range(fields[1], pattern->min, 0, 59);     // 分钟
        parse_range(fields[2], pattern->hour, 0, 23);    // 小时
        parse_range(fields[3], pattern->day, 1, 31);     // 日
        parse_range(fields[4], pattern->month, 1, 12);   // 月
        parse_range(fields[5], pattern->wday, 0, 7);     // 星期
    } else {
        return -1;
    }
    
    // 处理星期0和7都表示周日
    if (pattern->wday[0] || pattern->wday[7]) {
        pattern->wday[0] = pattern->wday[7] = 1;
    }
    
    return 0;
}

// 检查时间是否匹配cron模式
int match_cron(time_t timestamp, const CronPattern *pattern) {
    // 转换为东八区时间
    timestamp += TIMEZONE_OFFSET;
    struct tm *tm_info = gmtime(&timestamp);
    
    // 检查各字段是否匹配
    if (!pattern->sec[timestamp % 60]) return 0;        // 秒
    if (!pattern->min[tm_info->tm_min]) return 0;
    if (!pattern->hour[tm_info->tm_hour]) return 0;
    if (!pattern->day[tm_info->tm_mday]) return 0;
    if (!pattern->month[tm_info->tm_mon + 1]) return 0;  // tm_mon是0-11
    if (!pattern->wday[tm_info->tm_wday]) return 0;      // tm_wday是0-6
    
    return 1;
}
/* end cron matcher */

#define MQTT_PARAM_CNT 6

#define MQTTSERVER 0
#define MQTTPORT 1
#define MQTTUSER 2
#define MQTTPASSWORD 3
#define MQTTPUBTOPIC 4
#define MQTTSUBTOPIC 5

#define MQTT_PARAM_LEN 32
char param[MQTT_PARAM_CNT][MQTT_PARAM_LEN];
String mqtt_param_filename = "/mqtt_param";

// flash键 配网络
const uint16_t flashPin = 0; // GPIO0 D3

// AP
const char* ap_ssid="ESP8266_AP";
const char* ap_password="12345678";

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
#define COPY_N 16
#define MAX_SIGNAL_LEN 512
uint16_t copy_signal[COPY_N][MAX_SIGNAL_LEN];
uint16_t copy_length[COPY_N] = {0};
String copy_name[COPY_N];
int copy_cover = 0;
String comming_copy_name;
int copy_mode = 0;

// 同时执行的最大任务数
#define TASK_N 16
struct Task {
  int xid;
  uint64_t remain;
  uint64_t start;
  uint64_t freq;
  String cmd;
  String taskname;
  String cron;
  CronPattern cp;
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
DynamicJsonDocument doc(1536);
DynamicJsonDocument rdoc(1536);



void LED_flash(int n) { // 闪烁n次
  for (int i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

void save_mqtt_param() {
  File f = SPIFFS.open(mqtt_param_filename, "w");
  for (int i=0; i<MQTT_PARAM_CNT; i++) {
    f.write((char *)&param[i], MQTT_PARAM_LEN);
  }
  f.close();
}
void load_mqtt_param() {
  File f = SPIFFS.open(mqtt_param_filename, "r");
  for (int i=0; i<MQTT_PARAM_CNT; i++) {
    f.read((uint8_t *) &param[i], MQTT_PARAM_LEN);
  }
  f.close();
}

void connect_wifi(int init, int fail_rst) {
  load_mqtt_param();
  digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
  WiFiManager wifiManager;
  wifiManager.setConnectTimeout(30); // 连接wifi 30秒超时
  WiFiManagerParameter wmp[] = {
    WiFiManagerParameter("mqtt_server", "mqtt server", param[0], MQTT_PARAM_LEN-1),
    WiFiManagerParameter("mqtt_port", "mqtt port", param[1], MQTT_PARAM_LEN-1),
    WiFiManagerParameter("mqtt_user", "mqtt user", param[2], MQTT_PARAM_LEN-1),
    WiFiManagerParameter("mqtt_password", "mqtt password", param[3], MQTT_PARAM_LEN-1),
    WiFiManagerParameter("mqtt_pubtopic", "publish topic", param[4], MQTT_PARAM_LEN-1),
    WiFiManagerParameter("mqtt_subtopic", "subscribe topic", param[5], MQTT_PARAM_LEN-1)
  };
  for (int i=0; i<MQTT_PARAM_CNT; i++) {
    wifiManager.addParameter(&wmp[i]);
  }
  if (!(init?wifiManager.autoConnect(ap_ssid, ap_password):wifiManager.startConfigPortal(ap_ssid, ap_password))) { // 优先连接保存的WiFi配置， true如果成功连接到 WiFi（自动连接或通过配网完成）
    Serial.println("Failed to connect, restarting...");
    delay(3000);
    if (!fail_rst) return ;
    ESP.restart(); // 连接失败重启
  }
  LED_flash(5); //成功闪烁4下
  for (int i=0; i<MQTT_PARAM_CNT; i++) {
    Serial.println(String("web read param: ")+wmp[i].getValue());
    if (strlen(wmp[i].getValue())) strcpy(param[i], wmp[i].getValue());
    Serial.println(String("memery param: ")+param[i]);
  }
  save_mqtt_param();
}


uint8_t connect_mqtt(){
  if(WiFi.status()!=WL_CONNECTED) return -1;
  pc.setServer(param[MQTTSERVER], atoi(param[MQTTPORT]));
  if(!pc.connect(WiFi.macAddress().c_str(), param[MQTTUSER], param[MQTTPASSWORD])){     //以物理地址为ID去连接MQTT服务器
    Serial.println("connect MQTT fail");
    return -1;
  }
  pc.subscribe(param[MQTTSUBTOPIC]);    
  pc.setCallback(sub_msg_hander);                        //绑定订阅回调函数
  Serial.println("connect MQTT success");
  return 0;
}

// 定时函数执行耗时操作会崩溃，主循环loop中检测到标记变量则执行耗时操作
Ticker wf, mt, lp;
int tag_wifi=0, tag_mqtt=0, tag_loop = 0;
void itv_wifi() { tag_wifi++; }
void itv_mqtt() { tag_mqtt++; }
void itv_loop() { tag_loop++; }

// must after rdoc.clear()
void msg_pub_print(int code, const String& msg) {
  rdoc["chat_id"] = doc["chat_id"];
  rdoc["code"] = code;
  rdoc["message"] = msg;
  String result;
  serializeJson(rdoc, result);
  pc.publish(param[MQTTPUBTOPIC], result.c_str());
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
      rdoc["task"]["cron"] = tasklist[id].cron;
      rdoc["task"]["taskname"] = tasklist[id].taskname;
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
        rdoc["tasks"][j]["cron"] = tasklist[i].cron;
        rdoc["tasks"][j]["taskname"] = tasklist[i].taskname;
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

  if (cmd == "terminate") { // 支持非数字字符分割的taskid 2,4 1
    String cmds = doc["taskid"];
    int cmds_len = cmds.length();
    for (int i=0, j; (j=i)<cmds_len; i=j) {
      while (j<cmds_len && !('0' <= cmds.charAt(j) && cmds.charAt(j) <= '9') ) j++;
      i = j;
      int id = 0;
      while (j<cmds_len && ('0' <= cmds.charAt(j) && cmds.charAt(j) <= '9') ) id = id*10+cmds.charAt(j++)-'0';
      if (i<j && 0 <= id && id < TASK_N && tasklist[id].remain>0) {
        tasklist[id].remain = 0;
        msg_pub_print(200, "terminate task "+cmds.substring(i,j)+" ok");
      } else {
        msg_pub_print(400, "illegal task id");
      }  
    }
    
  }

  if (cmd == "terminatename") { // 支持非数字字符分割的taskid 2,4 1
    String cmds = doc["taskname"];
    int cmds_len = cmds.length();
    for (int i=0, j; (j=i)<cmds_len; i=j) {
      while (j<cmds_len && !(33 <= cmds.charAt(j) && cmds.charAt(j) <= 126) ) j++; // 跳过不可见字符
      i = j;
      while (j<cmds_len && (33 <= cmds.charAt(j) && cmds.charAt(j) <= 126) ) j++;
      if (i<j) {
        String taskname = cmds.substring(i,j);
        for (int id=0; id<TASK_N; id++) {
          if (tasklist[id].remain>0 && tasklist[id].taskname == taskname) {
            tasklist[id].remain = 0;
            msg_pub_print(200, "terminate task "+cmds.substring(i,j)+" ok");
          }
        }
      } else {
        msg_pub_print(400, "illegal taskname");
      }  
    }
    
  }

  if (cmd == "exec") {                            // exec cmd
    String cmds = doc["name"];
    Serial.println(cmds);
    int cmds_len = cmds.length();
    for (int i=0, t=0, j; (j=i)<cmds_len; t++, i=j+1) { // 以逗号分割命令
      while (j<cmds_len && cmds.charAt(j) != ',') j++;
      
      String name = cmds.substring(i,j);
      uint64_t start = doc["start"]; 
      start += t;// 指令间间隔一秒
      uint64_t freq = doc["freq"];
      uint64_t remain = doc["remain"];
      String cron = doc["cron"];
      String taskname = doc["taskname"];
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
        continue;
      }
      // find first not use task place
      int task_id = 0;
      while (task_id<TASK_N && tasklist[task_id].remain > 0) task_id++;
      if (task_id == TASK_N) {
        msg_pub_print(400, String("exec failure, tasklist is full, max is ")+TASK_N);
        continue;
      }
      // check cron expr
      if (cron != "" && parse_cron(cron.c_str(), &tasklist[task_id].cp) != 0) {
        msg_pub_print(400, "exec failure, cron expr ["+name+"] error!");
        continue;
      }
      
      tasklist[task_id].remain = remain;
      tasklist[task_id].freq = freq;
      tasklist[task_id].start = start;
      tasklist[task_id].cmd = name;
      tasklist[task_id].xid = xid;
      tasklist[task_id].cron = cron;
      tasklist[task_id].taskname = taskname;
      msg_pub_print(200, "add "+name+" to tasklist");

    }
    
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
  Serial.printf("MQTT subscribe data from topic: %s\r\n",topic);
  for(unsigned int i=0;i<length;++i){
    Serial.print((char)payload[i]);
  }
  Serial.println();
  payload[length] = 0;
  String Msg = String((char*)payload);
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

  // network
  connect_wifi(1, 1);

  Serial.printf("macAddress is %s\r\n",WiFi.macAddress().c_str());  
  connect_mqtt();  // 连接MQTT
  wf.attach(60, itv_wifi);
  mt.attach(30, itv_mqtt);
  lp.attach(0.5, itv_loop);


  
  // ntp
  timeClient.begin();
  timeClient.update();
  Serial.println(timeClient.getFormattedTime() + " " + timeClient.getEpochTime());
}

void loop() {
  // task slover
  for (int i=0; i<TASK_N; i++) {
    if (tasklist[i].remain <= 0) continue;
    if (tasklist[i].cron != "") { // 优先cron
      size_t u = timeClient.getEpochTime();
      if (tasklist[i].start == u) continue; // 一秒内不可重复执行
      if (match_cron(u, &tasklist[i].cp)) {
        exec_queue[eqsz++] = tasklist[i].xid;
        tasklist[i].start = u;
        if (--tasklist[i].remain == 0) tasklist[i].cron = ""; // 消除影响后续任务
      }
    } else if (tasklist[i].start <= timeClient.getEpochTime()) {
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
  for (int i=0; i<eqsz; i++) {
    int xid = exec_queue[i];
    irsend.sendRaw(copy_signal[xid], copy_length[xid], 38);
    rdoc.clear();
    msg_pub_print(200, "task:exec "+copy_name[xid]+" success");
    delay(800); // 同时执行的指令，间隔0.8s
  }
  eqsz = 0;

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
      LED_flash(2); // 闪烁2次 wifi 断联
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
    if(!(WiFi.status()==WL_CONNECTED && pc.connected())){
      LED_flash(1); // 闪烁1次 mqtt 断联
      Serial.println("mqtt disconnected, trying to reconnect...");
      connect_mqtt();                                  // 如果和MQTT服务器断开连接,那么重连
    }
    tag_mqtt = 0;
    timeClient.update();
    // Serial.println(timeClient.getFormattedTime()+" "+timeClient.getEpochTime());
  }
  if (tag_loop>=1) {
    pc.loop();
    tag_loop = 0;
  }
  if (digitalRead(flashPin) == LOW) {
    Serial.println("Flash button pressed!");
    connect_wifi(0,0);
  }
  delay(50);
}   