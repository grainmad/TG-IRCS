#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>  //红外头文件
#include <IRrecv.h>
#include <IRac.h>
#include <IRtext.h>
#include <IRutils.h>
#include <Ticker.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPAsyncTCP.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>

// dns
DNSServer dnsServer;
// web 
ESP8266WebServer server(80);

// 创建WebSocket服务器对象，监听端口81
WebSocketsServer webSocket = WebSocketsServer(81);

// HTML页面内容
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>红外控制面板</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            padding: 20px;
        }
        .blk {
            display: block;
            margin: 10px 0;
        }
        .hidden {
            display: none;
        }
        .dly {
            width: 40px;
            margin-right: 5px;
        }
        ul {
            list-style-type: none;  /* 去掉默认的列表项标记 */
            padding: 0;  /* 去掉默认的内边距 */
        }
        li {
            display: flex;  /* 使用 flexbox 布局 */
            justify-content: space-between;  /* 左右两边分布 */
            align-items: center;  /* 垂直居中对齐 */
            padding: 10px;
            border: 1px solid #ccc;
            margin-bottom: 5px;
            word-wrap: break-word;  /* 让文字换行 */
        }
        .text {
            flex: 1;  /* 文字部分占据可用空间 */
            margin-right: 10px;  /* 文字和按钮之间的间距 */
        }
        .buttons {
            display: flex;  /* 使按钮横向排列 */
            gap: 5px;  /* 按钮之间的间距 */
        }
    </style>
</head>
<body>
    <h1>websocket消息</h1>
    <div id="messages"></div>
    <hr>

    <h1>执行红外指令</h1>
    <input class="blk" type="text" id="execname" placeholder="执行命令">
    
    <label>
        <input type="radio" name="option" value="datetime" onclick="toggleInputs()" checked> 执行日期
    </label>
    <label>
        <input type="radio" name="option" value="delaytime" onclick="toggleInputs()"> 延时时间
    </label>

    <!-- Date input for option A -->
    <div id="dateInput" class="blk">
        <label for="date">选择执行时间:</label>
        <input type="datetime-local" id="selectDateTime">
    </div>

    <!-- Number input for option B -->
    <div id="numberInput" class="blk hidden">
        <label for="number">选择延时时间:</label>
        <input type="number" id="delayd" min="0" class="dly"><span>天</span>
        <input type="number" id="delayh" min="0" class="dly"><span>时</span>
        <input type="number" id="delaym" min="0" class="dly"><span>分</span>
        <input type="number" id="delays" min="0" class="dly"><span>秒</span>
        
    </div>

    <div id="numberInput2" class="blk">
        <label for="number">执行周期:</label>
        <input type="number" id="freqd" min="0" class="dly"><span>天</span>
        <input type="number" id="freqh" min="0" class="dly"><span>时</span>
        <input type="number" id="freqm" min="0" class="dly"><span>分</span>
        <input type="number" id="freqs" min="0" class="dly"><span>秒</span>
    </div>
    <div id="numberInput3" class="blk">
        <label for="number">剩余执行次数:</label> <input type="number" id="remain" min="1" value="1" style="width: 40px; margin-right: 5px;"><span>次</span>
    </div>
    <button class="blk" onclick="execstart()">执行命令</button>
    <hr>

    <h1>任务队列</h1>
    <div>
        <button class="blk" onclick="tasklist()">刷新</button>
        <ul id="taskqueue">
            <!-- <li>
                <div class="text">This is a list item with some long text that will wrap around as needed to fit within the available space.</div>
                <div class="buttons">
                    <button>终止任务</button>
                </div>
            </li>
            <li>
                <div class="text">Another item, this time with shorter text.</div>
                <div class="buttons">
                    <button>终止任务</button>
                </div>
            </li>
            <li>
                <div class="text">Here is a third item with a medium amount of text.</div>
                <div class="buttons">
                    <button>终止任务</button>
                </div>
            </li> -->
        </ul>
    </div>
    <hr>

    <h1>复制红外命令</h1>
    <input class="blk" type="text" id="copyname" placeholder="新命令名称">
    <input class="blk" type="text" id="oldname" placeholder="替换的旧命令名称（可选）">
    <button class="blk" onclick="copystart()">开始复制</button>
    <hr>
    <h1>红外命令</h1>
    <div>
        <button class="blk" onclick="cmdlist()">刷新</button>
        <ul id="ircmd">
            <!-- <li>
                <div class="text">This is a list item with some long text that will wrap around as needed to fit within the available space.</div>
                
            </li> -->
        </ul>
    </div>
    <hr>
    <button class="blk" onclick="updatetime()">校准时间</button>
    <script>
        function copystart() {
            var copyname = document.getElementById('copyname').value;
            var oldname = document.getElementById('oldname').value;
            const msg = {cmd: "copy", name: copyname, old: oldname, chat_id: parseInt(Math.floor(new Date().getTime()/1000))}
            sendMessage(JSON.stringify(msg))
            cmdlist();
        }
        function execstart() {
            var execname = document.getElementById('execname').value;
            const selectedOption = document.querySelector('input[name="option"]:checked').value;
            var delay;
            if (selectedOption === 'datetime') {
                delay = document.getElementById('selectDateTime').value;
                if (delay != "") {
                    delay += ":00"
                    delay = Math.floor(new Date(delay).getTime()/1000);
                } else {
                    delay = 0;
                }
            } else if (selectedOption === 'delaytime') {
                delay = parseInt(Math.floor(new Date().getTime()/1000));
                
                
                delay += parseInt(document.getElementById('delayd').value*86400) || 0;
                delay += parseInt(document.getElementById('delayh').value*3600) || 0;
                delay += parseInt(document.getElementById('delaym').value*60) || 0;
                delay += parseInt(document.getElementById('delays').value) || 0;
                
            }
            var fq = 0;
            fq += parseInt(document.getElementById('freqd').value*86400) || 0;
            fq += parseInt(document.getElementById('freqh').value*3600) || 0;
            fq += parseInt(document.getElementById('freqm').value*60) || 0;
            fq += parseInt(document.getElementById('freqs').value) || 0;
            if (fq == 0) fq = 1

            var rm = parseInt(document.getElementById('remain').value) || 1;
            
            const msg = {'cmd': 'exec', 'name': execname, 'start': delay, 'freq': fq, 'remain': rm, 'chat_id': parseInt(Math.floor(new Date().getTime()/1000))} 
            sendMessage(JSON.stringify(msg))
            tasklist();
        }
        function tasklist() {
            const msg = {'cmd': 'tasklist', 'chat_id': parseInt(Math.floor(new Date().getTime()/1000))} 
            sendMessage(JSON.stringify(msg))
        }
        function cmdlist() {
            const msg = {'cmd': 'cmdlist', 'chat_id': parseInt(Math.floor(new Date().getTime()/1000))} 
            sendMessage(JSON.stringify(msg))
        }
        function terminate(id) {
            console.log("terminate "+id);
            const msg = {'cmd': 'terminate', 'taskid':id, 'chat_id': parseInt(Math.floor(new Date().getTime()/1000))} 
            sendMessage(JSON.stringify(msg))
            tasklist();
        }
        function updatetime(id) {
            const msg = {'cmd': 'updatetime', 'chat_id': parseInt(Math.floor(new Date().getTime()/1000))} 
            sendMessage(JSON.stringify(msg))
        }
        function toggleInputs() {
            const dateInput = document.getElementById('dateInput');
            const numberInput = document.getElementById('numberInput');
            const selectedOption = document.querySelector('input[name="option"]:checked').value;

            // Hide both inputs initially
            dateInput.classList.add('hidden');
            numberInput.classList.add('hidden');

            // Show the corresponding input based on selection
            if (selectedOption === 'datetime') {
                dateInput.classList.remove('hidden');
            } else if (selectedOption === 'delaytime') {
                numberInput.classList.remove('hidden');
            }
        }
        function formatTimestamp(unixTimestamp) {
            // 将 Unix 时间戳转换为 JavaScript 的 Date 对象
            const date = new Date(unixTimestamp * 1000);

            // 获取年、月、日、时、分、秒
            const year = date.getFullYear();
            const month = String(date.getMonth() + 1).padStart(2, '0'); // 月份从 0 开始，补零
            const day = String(date.getDate()).padStart(2, '0'); // 日期补零
            const hours = String(date.getHours()).padStart(2, '0'); // 小时补零
            const minutes = String(date.getMinutes()).padStart(2, '0'); // 分钟补零
            const seconds = String(date.getSeconds()).padStart(2, '0'); // 秒补零

            // 格式化输出
            return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
        }
        function formatSeconds(seconds) {
            const days = Math.floor(seconds / (3600 * 24)); // 计算天数
            seconds %= 3600 * 24; // 剩余秒数

            const hours = Math.floor(seconds / 3600); // 计算小时数
            seconds %= 3600; // 剩余秒数

            const minutes = Math.floor(seconds / 60); // 计算分钟数
            seconds %= 60; // 剩余秒数

            // 拼接结果
            let result = '';
            if (days > 0) result += `${days}d`;
            if (hours > 0) result += `${hours}h`;
            if (minutes > 0) result += `${minutes}m`;
            if (seconds > 0) result += `${seconds}s`;

            return result || '0s'; // 如果结果为 0，返回 '0s'
        }
        function addcmd(cmd) {
            var execname = document.getElementById('execname').value;
            document.getElementById('execname').value += (execname != "" && execname.slice(-1) != "," ? ",":"")+cmd

        }
        function addmessage(message) {
            let messages = document.getElementById('messages');
            // 获取'messages'元素中的所有p标签
            let pTags = messages.querySelectorAll('p');

            // 如果p标签的数量大于或等于5个
            if (pTags.length >= 6) {
            // 删除第一个p标签
            pTags[0].remove();
            }
            document.getElementById('messages').innerHTML += message;
        }
        const socket = new WebSocket('ws://192.168.4.1:81/');
        socket.onmessage = function(event) {
            console.log('Message from server: ' + event.data);
            addmessage("<p>Server: " + event.data + "</p>")
            let data = JSON.parse(event.data)
            // cmdlist {"cmds":["109-on","109-off","hr-on","hr-off","lon","loff","hron","hroff"],"chat_id":0,"code":200,"message":"get cmdlist ok"}
            // tasklist {"tasks":[{"remain":1,"start":1740905400,"freq":0,"cmd":"lon","xid":4,"taskid":0}],"chat_id":0,"code":200,"message":"get tasklist ok"}
            if (data.message == "get cmdlist ok") {
                const cmdList = document.getElementById("ircmd");
                // 清空现有的列表项
                cmdList.innerHTML = "";
                data.cmds.forEach(cmd => {
                    const li = document.createElement("li");
                    li.innerHTML = `<div class="text">${cmd}</div><div class="buttons"><button onclick="addcmd('${cmd}')">添加指令</button>`;
                    cmdList.appendChild(li);
                });
            }
            if (data.message == "get tasklist ok") {
                const taskList = document.getElementById("taskqueue")
                // 清空现有的列表项
                taskList.innerHTML = "";
                if (data.tasks !== undefined) {
                    data.tasks.forEach(task => {
                        const li = document.createElement("li");
                        li.innerHTML = `<div class="text">任务号：${task.taskid}<br>指令：${task.cmd}<br>指令编号：${task.xid}<br>执行时间：${formatTimestamp(task.start)}<br>周期：${formatSeconds(task.freq)}<br>剩余次数：${task.remain}<br></div><div class="buttons"><button onclick="terminate(${task.taskid})">终止任务</button></div>`;
                        taskList.appendChild(li);
                    });
                }
            }
            if (data.message.includes("success")) {
                if (data.message.includes("task")) tasklist();
                if (data.message.startsWith("copy")) cmdlist();
            }
            
        };
        socket.onopen = function() {
            console.log('WebSocket is connected.');
            addmessage("<p>Connected to WebSocket server.</p>")
            updatetime();
            tasklist();
            cmdlist();
        };
        socket.onerror = function(error) {
            console.error('WebSocket Error: ', error);
        };
        socket.onclose = function() {
            console.log('WebSocket connection closed.');
            addmessage("<p>Disconnected from WebSocket server.</p>")
        };
        function sendMessage(message) {
            if (message && socket.readyState === WebSocket.OPEN) {
                socket.send(message);
                addmessage("<p>You: " + message + "</p>")
            } else {
                // alert('WebSocket is not open.');
            }
        }

    </script>
</body>
</html>
)rawliteral";


// AP
const char* ap_ssid="ESP8266_panel";
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
} tasklist[TASK_N];
// 执行队列
int exec_queue[TASK_N];
int eqsz = 0;

DynamicJsonDocument doc(1536);
DynamicJsonDocument rdoc(1536);


void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] WebSocket Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] WebSocket Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
    }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] WebSocket Received text: %s\n", num, payload);
      // 处理收到的命令
      payload[length] = 0;
      String Msg = String((char*)payload);
      solve_msg(Msg, num);
      break;
  }
}

void startServers() {
  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }); // 404 处理
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
}

void stopServers() {
  webSocket.close();  // 关闭 WebSocket 服务器
  server.close();     // 关闭 Web 服务器
}

void LED_flash(int n) { // 闪烁n次
  for (int i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, LOW); // 亮内置LED
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
  }
}



// 定时函数执行耗时操作会崩溃，主循环loop中检测到标记变量则执行耗时操作
Ticker wt;
uint64_t webtime=0;
void itv_wt() { webtime++; }

// must after rdoc.clear()
void msg_pub_print(int code, const String& msg, int num) {
  rdoc["chat_id"] = doc["chat_id"];
  rdoc["code"] = code;
  rdoc["message"] = msg;
  String result;
  serializeJson(rdoc, result);
  if (num>=0) webSocket.sendTXT(num, result.c_str());
  else {
    int connectedCount = webSocket.connectedClients();
    // 遍历所有客户端
    for (int i = 0; i < connectedCount; i++) {
      webSocket.sendTXT(i, result.c_str());
    }
  }
  Serial.println(result);
}

 
void solve_msg(String Msg, int num) {
  doc.clear();
  rdoc.clear();
  deserializeJson(doc, Msg);
  String cmd = doc["cmd"];
  if (cmd == "updatetime") {
    webtime = doc["chat_id"];
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
    msg_pub_print(200, "get tasklist ok", num);
  }

  if (cmd == "cmdlist") {
    // get no null
    for (int i=0,j=0; i<COPY_N; i++) {
      if (copy_name[i] != "") {
        rdoc["cmds"][j++] = copy_name[i];
      }
    }
    msg_pub_print(200, "get cmdlist ok", num);
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
        msg_pub_print(200, "terminate task "+cmds.substring(i,j)+" ok", num);
      } else {
        msg_pub_print(400, "illegal task id", num);
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
      // Serial.println(name+" "+start+" "+freq);
      int xid = 0;
      for (; xid<COPY_N; xid++) {
        if (name == copy_name[xid]) {
          break;
        }
      }
      // not exist name 
      if (xid == COPY_N) {
        msg_pub_print(400, "exec failure, cmd name ["+name+"] not exist!", num);
        continue;
      }
      // find first not use task place
      int task_id = 0;
      while (task_id<TASK_N && tasklist[task_id].remain > 0) task_id++;
      if (task_id == TASK_N) {
        msg_pub_print(400, String("exec failure, tasklist is full, max is ")+TASK_N, num);
        continue;
      }
      tasklist[task_id].remain = remain;
      tasklist[task_id].freq = freq;
      tasklist[task_id].start = start;
      tasklist[task_id].cmd = name;
      tasklist[task_id].xid = xid;
      msg_pub_print(200, "add "+name+" to tasklist", num);

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
        msg_pub_print(400, "copy failure: old name ["+old+"] not exist!", num);
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
    msg_pub_print(200, "copy start ok", num);
  }
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
  msg_pub_print(200, "copy ["+comming_copy_name+"] success, repalce [" + prename + "]", -1);
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
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.printf("macAddress is %s\r\n",WiFi.macAddress().c_str());  

  // dns
  dnsServer.start(53, "*", WiFi.softAPIP());

  // web
  startServers();
  
  // time
  wt.attach(1, itv_wt);
}


void loop() {
  // task slover
  for (int i=0; i<TASK_N; i++) {
    if (tasklist[i].remain > 0 && tasklist[i].start <= webtime) {
      exec_queue[eqsz++] = tasklist[i].xid;
      uint64_t remain = tasklist[i].remain, freq = tasklist[i].freq;
      if (tasklist[i].start+freq<webtime) 
        tasklist[i].start = webtime;
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
    msg_pub_print(200, "task:exec "+copy_name[xid]+" success", -1);
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
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();
  
}   