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
#include <ESP8266WebServer.h>
#include <DNSServer.h>

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

#define CONFIG_CNT 9

#define WIFI_SSID 0
#define WIFI_PASSWORD 1
#define MQTT_SERVER 2
#define MQTT_PORT 3
#define MQTT_USER 4
#define MQTT_PASSWORD 5
#define MQTT_PUBTOPIC 6
#define MQTT_SUBTOPIC 7
#define ADMIN_UID 8

#define CONFIG_LEN 20
char config[CONFIG_CNT][CONFIG_LEN];
String config_filename = "/svrConfig";
uint64_t admin_user = 0;


// 配网相关
ESP8266WebServer webServer(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool config_mode = false;
bool need_restart_wifi = false;  // 标志位，用于延迟处理WiFi重连

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
#define COPY_N 12
#define MAX_SIGNAL_LEN 512
uint16_t copy_signal[COPY_N][MAX_SIGNAL_LEN];
uint16_t copy_length[COPY_N] = {0};
String copy_name[COPY_N];
int copy_cover = 0;
String comming_copy_name;
int copy_mode = 0;

// 同时执行的最大任务数
#define TASK_N 12
struct Task {
  int xid;
  uint64_t remain;
  uint64_t start;
  uint64_t freq;
  uint64_t uid;
  String cmd;
  String taskname;
  String cron;
  CronPattern cp;
} tasklist[TASK_N];
// 执行队列
#define EXEC_Q_N 16
struct Task* exec_queue[EXEC_Q_N];
int eqsz = 0;

// 定期同步网络时间，断网则用本机时间
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

WiFiClient wc;
PubSubClient pc(wc);
// 收发json消息
StaticJsonDocument<1536> doc, rdoc;

size_t last_check;

void LED_flash(int n) { // 闪烁n次
  for (int i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

void save_config() {
  File f = SPIFFS.open(config_filename, "w");
  for (int i=0; i<CONFIG_CNT; i++) {
    f.write((char *)&config[i], CONFIG_LEN);
  }
  f.close();
}
void load_config() {
  File f = SPIFFS.open(config_filename, "r");
  for (int i=0; i<CONFIG_CNT; i++) {
    f.read((uint8_t *) &config[i], CONFIG_LEN);
  }
  f.close();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>WiFi配置</title>";
  html += "<style>body{font-family:Arial;margin:40px;background:#f0f0f0}";
  html += ".container{background:white;padding:20px;border-radius:10px;max-width:500px;margin:0 auto}";
  html += "input{width:100%;padding:10px;margin:5px 0;border:1px solid #ddd;border-radius:5px}";
  html += "button{background:#007cba;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer}";
  html += "button:hover{background:#005a87}</style></head><body>";
  html += "<div class='container'><h2>WiFi & MQTT 配置</h2>";
  html += "<form action='/save' method='post'>";
  html += "<h3>WiFi设置</h3>";
  html += "SSID: <input type='text' name='ssid' value='" + String(config[WIFI_SSID]) + "'><br>";
  html += "密码: <input type='password' name='password' value='" + String(config[WIFI_PASSWORD]) + "'><br>";
  html += "<h3>MQTT设置</h3>";
  html += "MQTT服务器: <input type='text' name='mqtt_server' value='" + String(config[MQTT_SERVER]) + "'><br>";
  html += "MQTT端口: <input type='text' name='mqtt_port' value='" + String(config[MQTT_PORT]) + "'><br>";
  html += "MQTT用户名: <input type='text' name='mqtt_user' value='" + String(config[MQTT_USER]) + "'><br>";
  html += "MQTT密码: <input type='password' name='mqtt_password' value='" + String(config[MQTT_PASSWORD]) + "'><br>";
  html += "发布主题: <input type='text' name='mqtt_pubtopic' value='" + String(config[MQTT_PUBTOPIC]) + "'><br>";
  html += "订阅主题: <input type='text' name='mqtt_subtopic' value='" + String(config[MQTT_SUBTOPIC]) + "'><br>";
  html += "管理员用户: <input type='text' name='mqtt_adminuser' value='" + String(config[ADMIN_UID]) + "'><br>";
  html += "<button type='submit'>保存配置</button>";
  html += "</form></div></body></html>";
  webServer.send(200, "text/html", html);
}

void handleSave() {
  if (webServer.hasArg("ssid")) {
    strcpy(config[WIFI_SSID], webServer.arg("ssid").c_str());
  }
  if (webServer.hasArg("password")) {
    strcpy(config[WIFI_PASSWORD], webServer.arg("password").c_str());
  }
  if (webServer.hasArg("mqtt_server")) {
    strcpy(config[MQTT_SERVER], webServer.arg("mqtt_server").c_str());
  }
  if (webServer.hasArg("mqtt_port")) {
    strcpy(config[MQTT_PORT], webServer.arg("mqtt_port").c_str());
  }
  if (webServer.hasArg("mqtt_user")) {
    strcpy(config[MQTT_USER], webServer.arg("mqtt_user").c_str());
  }
  if (webServer.hasArg("mqtt_password")) {
    strcpy(config[MQTT_PASSWORD], webServer.arg("mqtt_password").c_str());
  }
  if (webServer.hasArg("mqtt_pubtopic")) {
    strcpy(config[MQTT_PUBTOPIC], webServer.arg("mqtt_pubtopic").c_str());
  }
  if (webServer.hasArg("mqtt_subtopic")) {
    strcpy(config[MQTT_SUBTOPIC], webServer.arg("mqtt_subtopic").c_str());
  }
  if (webServer.hasArg("mqtt_adminuser")) {
    strcpy(config[ADMIN_UID], webServer.arg("mqtt_adminuser").c_str());
    admin_user = atoll(config[ADMIN_UID]);
  }
  save_config();
  
  webServer.send(200, "text/html", 
    "<!DOCTYPE html><html><head><meta charset='UTF-8'></head><body>"
    "<div style='text-align:center;margin-top:50px;font-family:Arial'>"
    "<h2>配置已保存!</h2><p>正在尝试连接WiFi...</p></div></body></html>");
  
  // 设置标志位，在主循环中处理WiFi重连
  need_restart_wifi = true;
}

void handleNotFound() {
  // DNS劫持，所有请求都重定向到配置页面
  webServer.sendHeader("Location", "http://192.168.4.1", true);
  webServer.send(302, "text/plain", "");
}

void start_config_mode() {
  digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
  config_mode = true;
  Serial.println("Starting configuration mode...");
  
  // 停止STA模式
  WiFi.mode(WIFI_OFF);
  delay(1000);
  
  // 启动AP模式
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  delay(2000);
  
  IPAddress apIP(192, 168, 4, 1);
  IPAddress netMsk(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  
  // 启动DNS服务器进行劫持
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // 启动Web服务器
  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  
  Serial.println("Configuration mode started");
  Serial.println("Connect to WiFi: " + String(ap_ssid));
  Serial.println("Open browser and visit any website");
  
}

void stop_config_mode() {
  if (config_mode) {
    digitalWrite(LED_BUILTIN, HIGH);
    config_mode = false;
    webServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("Configuration mode stopped");
  }
}

bool connect_wifi_sta() {
  if (strlen(config[WIFI_SSID]) == 0) {
    Serial.println("No WiFi SSID configured");
    return false;
  }
  
  Serial.println("Connecting to WiFi: " + String(config[WIFI_SSID]));
  WiFi.mode(WIFI_STA);
  WiFi.begin(config[WIFI_SSID], config[WIFI_PASSWORD]);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { // 30秒超时
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected successfully");
    Serial.println("IP address: " + WiFi.localIP().toString());
    LED_flash(5); // 成功闪烁5次
    return true;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
    return false;
  }
}

uint8_t connect_mqtt(){
  if(WiFi.status()!=WL_CONNECTED) return -1;
  pc.setServer(config[MQTT_SERVER], atoi(config[MQTT_PORT]));
  if(!pc.connect(WiFi.macAddress().c_str(), config[MQTT_USER], config[MQTT_PASSWORD])){     //以物理地址为ID去连接MQTT服务器
    Serial.println("connect MQTT fail");
    return -1;
  }
  pc.subscribe(config[MQTT_SUBTOPIC]);    
  pc.setCallback(sub_msg_hander);                        //绑定订阅回调函数
  Serial.println("connect MQTT success");
  return 0;
}

void init_connect_wifi() {
  load_config();
  if (strlen(config[ADMIN_UID])) {
    admin_user = atoll(config[ADMIN_UID]);
  }
  
  // 尝试连接WiFi
  if (connect_wifi_sta()) {
    connect_mqtt();
    LED_flash(10);
    return; // 连接成功
  }
  
  // 连接失败，进入配置模式
  start_config_mode();
  
}
// 定时函数执行耗时操作会崩溃，主循环loop中检测到标记变量则执行耗时操作
Ticker wf, mt, lp;
int tag_wifi=0, tag_mqtt=0, tag_loop = 0;
void itv_wifi() { tag_wifi++; }
void itv_mqtt() { tag_mqtt++; }
void itv_loop() { tag_loop++; }


void msg_pub_print(int code, uint64_t uid, const String& msg, int reset) {
  if (reset) rdoc.clear();
  rdoc["chat_id"] = uid;
  rdoc["code"] = code;
  rdoc["message"] = msg;
  String result;
  serializeJson(rdoc, result);
  pc.publish(config[MQTT_PUBTOPIC], result.c_str());
  Serial.println(result);
}

 
void solve_msg(String Msg) {
  doc.clear();
  rdoc.clear();
  deserializeJson(doc, Msg);
  String cmd = doc["cmd"];
  uint64_t uid = doc["chat_id"];

  if (cmd == "taskidlist") {
    for (int i=0,j=0; i<TASK_N; i++) {
      if (tasklist[i].remain>0) {
        rdoc["taskids"][j] = i;
        j++;
      }
    }
    msg_pub_print(200, uid, "get taskidlist ok", false);
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
      msg_pub_print(200, uid, "get task ok", false);
    } else {
      msg_pub_print(400, uid, "illegal task id", false);
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
    msg_pub_print(200, uid, "get tasklist ok", false);
  }

  if (cmd == "cmdlist") {
    // get no null
    for (int i=0,j=0; i<COPY_N; i++) {
      if (copy_name[i] != "") {
        rdoc["cmds"][j++] = copy_name[i];
      }
    }
    msg_pub_print(200, uid, "get cmdlist ok", false);
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
        msg_pub_print(200, uid, "terminate task "+cmds.substring(i,j)+" ok", false);
      } else {
        msg_pub_print(400, uid, "illegal task id", false);
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
            msg_pub_print(200, uid, "terminate task "+cmds.substring(i,j)+" ok", false);
          }
        }
      } else {
        msg_pub_print(400, uid, "illegal taskname", false);
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
        msg_pub_print(400, uid, "exec failure, cmd name ["+name+"] not exist!", false);
        continue;
      }
      // find first not use task place
      int task_id = 0;
      while (task_id<TASK_N && tasklist[task_id].remain > 0) task_id++;
      if (task_id == TASK_N) {
        msg_pub_print(400, uid, String("exec failure, tasklist is full, max is ")+TASK_N, false);
        continue;
      }
      // check cron expr
      if (cron != "" && parse_cron(cron.c_str(), &tasklist[task_id].cp) != 0) {
        msg_pub_print(400, uid, "exec failure, cron expr ["+name+"] error!", false);
        continue;
      }
      
      tasklist[task_id].remain = remain;
      tasklist[task_id].freq = freq;
      tasklist[task_id].start = start;
      tasklist[task_id].cmd = name;
      tasklist[task_id].xid = xid;
      tasklist[task_id].uid = uid;
      tasklist[task_id].cron = cron;
      tasklist[task_id].taskname = taskname;
      msg_pub_print(200, uid, "add "+name+" to tasklist", false);

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
        msg_pub_print(400, uid, "copy failure: old name ["+old+"] not exist!", false);
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
    msg_pub_print(200, uid, "copy start ok", false);
  }
}

void sub_msg_hander(char* topic, byte* payload,unsigned int length){
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
  
  
  // can not send mqtt to other user after received copy mqtt
  msg_pub_print(200, rdoc["chat_id"], "copy ["+comming_copy_name+"] success, repalce [" + prename + "]", false);
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
  Serial.printf("Free Heap: %d\n", ESP.getFreeHeap()); // 检查内存
  // irsend 
  irsend.begin();
  
  // irrecv
  irrecv.setTolerance(kTolerancePercentage);  // Override the default tolerance.
  irrecv.enableIRIn();  // Start the receiver

  // if (SPIFFS.format()) {
  //   Serial.println("SPIFFS 格式化成功");
  // } else {
  //   Serial.println("SPIFFS 格式化失败");
  // }
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
  init_connect_wifi();
  Serial.printf("macAddress is %s\r\n",WiFi.macAddress().c_str());  
  connect_mqtt();  // 连接MQTT
  wf.attach(60, itv_wifi);
  mt.attach(30, itv_mqtt);
  lp.attach(0.5, itv_loop);


  
  // ntp
  timeClient.begin();
  timeClient.update();
  Serial.println(timeClient.getFormattedTime() + " " + timeClient.getEpochTime());
  last_check = timeClient.getEpochTime();
}

void loop() {
  // task slover
  size_t cur_check = timeClient.getEpochTime();
  // 没有联网，时间是开机时间
  if (last_check < 1000000000 && 1000000000 < cur_check) { // 第一次开机没有成功连上wifi，当前已连接
    last_check = cur_check;
  }
  if (cur_check>last_check+1) {
    if (admin_user && last_check > 1000000000)
      msg_pub_print(400, admin_user, String("task backlog time slice [")+last_check+","+cur_check+"] total "+(cur_check-last_check)+" seconds", true);
  }
  for (;last_check<cur_check; last_check++) {
    for (int i=0; i<TASK_N; i++) {
      if (eqsz == EXEC_Q_N) break;
      if (tasklist[i].remain <= 0) continue;
      if (tasklist[i].cron != "") { // 优先cron
        if (tasklist[i].start == last_check) continue; // 一秒内不可重复执行
        if (match_cron(last_check, &tasklist[i].cp)) {
          exec_queue[eqsz++] = &tasklist[i];
          tasklist[i].start = last_check;
          if (--tasklist[i].remain == 0) tasklist[i].cron = ""; // 消除影响后续任务
        }
      } else if (tasklist[i].start <= last_check) {
        exec_queue[eqsz++] = &tasklist[i];
        tasklist[i].start += tasklist[i].freq;
        tasklist[i].remain--;
      }
    }
    if (eqsz == EXEC_Q_N) break;
  }
  
  // exec task 
  for (int i=0; i<eqsz; i++) {
    int xid = exec_queue[i]->xid;
    irsend.sendRaw(copy_signal[xid], copy_length[xid], 38);
    msg_pub_print(200, exec_queue[i]->uid, "task:exec "+copy_name[xid]+" success", true);
    delay(800); // 同时执行的指令，间隔0.8s
  }
  eqsz = 0;

  
  // 处理配置模式
  if (config_mode) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    
    // 处理延迟的WiFi重连
    if (need_restart_wifi) {
      need_restart_wifi = false;
      delay(1000); // 确保HTTP响应发送完成
      stop_config_mode();
      
      // 尝试连接WiFi
      if (!connect_wifi_sta()) {
        // 连接失败，重新开启配置服务
        Serial.println("WiFi connection failed after config save, restarting config mode...");
        delay(1000);
        start_config_mode();
      } else {
        // 连接成功，尝试连接MQTT
        Serial.println("WiFi connected successfully after config save");
        connect_mqtt();
      }
    }
  } else {
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
  }
  
  if (digitalRead(flashPin) == LOW) {
    Serial.println("Flash button pressed!");
    stop_config_mode(); // 确保先停止当前配置模式
    delay(100);
    start_config_mode(); // 进入配置模式
    while (digitalRead(flashPin) == LOW) { // 等待按键释放
      delay(50);
    }
  }
  delay(50);
}