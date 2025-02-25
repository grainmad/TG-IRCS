import telebot
import requests
import copy
import os
import json
import json
from datetime import datetime, timezone, timedelta
import re
from paho.mqtt import client as mqtt
import uuid
import time

import json

def save_dict(name, dc):
    with open(name, 'w') as f:
        json.dump(dc, f, indent=4)

def load_dict(name):
    try:
        with open(name, "r") as f:
            dc = json.load(f)
        return dc
    except FileNotFoundError:
        print(f"文件 '{name}' 不存在。")
        return {}

env = load_dict("env.json")
devices = {i["name"] : i for i in env["device"]}

db = load_dict("db.json")


if "device" not in db:
    db["device"] = env["device"][0]

if"user" not in db:
    db["user"] = [env["ir_admin_chat_id"]]

if"preference" not in db:
    db["preference"] = {}

save_dict("db.json", db)

bot = telebot.TeleBot(env["ir_bot_token"])

# print("env:", BOT_TOKEN, SUB_TOPIC, PUB_TOPIC, USERNAME, PASSWORD, env["ir_admin_chat_id"], MQTTHOST, MQTTPORT)





def on_message(client, userdata, msg):  
    rsp = json.loads(msg.payload.decode('utf-8'))
    print(f"Received message '{str(rsp)}' on topic '{msg.topic}' with QoS {msg.qos}")

    def unix_timestamp_to_datetime(timestamp, timezone_hour=8):
        dt = datetime.fromtimestamp(timestamp, tz=timezone(timedelta(hours=timezone_hour)))
        return dt.strftime("%Y-%m-%d/%H:%M:%S")
    def seconds_to_hms(seconds):
        m, s = divmod(seconds, 60)
        h, m = divmod(m, 60)
        d, h = divmod(h, 24)
        return f"{d}d{h:02d}h{m:02d}m{s:02d}s"
    rt = rsp["message"]+"\n"
    if rsp["code"] == 200:
        if "task" in rsp:
            it = rsp["task"]
            rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\n执行时间: {unix_timestamp_to_datetime(it["start"])}\n周期: {seconds_to_hms(it["freq"])}\n剩余次数:{it["remain"]}\n'
        if "tasks" in rsp:
            for it in rsp["tasks"]:
                rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\n执行时间: {unix_timestamp_to_datetime(it["start"])}\n周期: {seconds_to_hms(it["freq"])}\n剩余次数:{it["remain"]}\n'
        if "cmds" in rsp:
            for it in rsp["cmds"]:
                rt += f"  {it}\n"
        if "taskids" in rsp:
            for it in rsp["taskids"]:
                rt += f"  {str(it)}\n"
    bot.send_message(rsp["chat_id"], rt)

def mqtt_connect(mqtthost, mqttport, username, password, sub_topic, pub_topic):
    print(f"mqtt_connect:\n mqtthost {mqtthost}\n mqttport {mqttport}\n username {username}\n password {password}\n sub_topic {sub_topic}\n pub_topic {pub_topic}")
    """连接MQTT服务器"""
    mqttClient = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, str(uuid.uuid4()))
    def on_connect(client, userdata, flags, rc):
        """
        一旦连接成功, 回调此方法
        rc的值表示成功与否：
            0:连接成功
            1:连接被拒绝-协议版本不正确
            2:连接被拒绝-客户端标识符无效
            3:连接被拒绝-服务器不可用
            4:连接被拒绝-用户名或密码不正确
            5:连接被拒绝-未经授权
            6-255:当前未使用。
        """
        rc_status = ["连接成功", "协议版本不正确", "客户端标识符无效", "服务器不可用", "用户名或密码不正确", "未经授权"]
        print("connect：", rc_status[rc])
        client.subscribe(sub_topic)  # 订阅消息
    mqttClient.on_connect = on_connect  # 返回连接状态的回调函数
    mqttClient.on_message = on_message 
    mqttClient.username_pw_set(username, password)
    mqttClient.connect(mqtthost, mqttport, 60)
    mqttClient.loop_start()  # 启用线程连接

    return mqttClient


mqttClients = {}

for key, val in devices.items():
    mqttClients[key] = mqtt_connect(val["ir_mqtthost"], val["ir_mqttport"], val["ir_username"], val["ir_password"], val["ir_sub_topic"], val["ir_pub_topic"])



def Permissions(func):
    def wrapper(*args, **kwargs):
        # 只保留ascii可见字符和空格换行
        args[0].text =  ''.join(filter(lambda char: 32 <= ord(char) <= 126 or char == '\n', args[0].text))
        print(args[0])
        message = args[0]
        if message.chat.id not in db["user"]:
            bot.reply_to(message, f"authentication required")
            return 
        data = func(*args, **kwargs)  # 调用被装饰的函数，并传递所有参数
        if data:
            mqttClients[db["device"]["name"]].publish(db["device"]["ir_pub_topic"], str(data), 0)
            bot.reply_to(message, f"{str(data)} is transmitted")
    return wrapper

@bot.message_handler(commands=['copy'])
@Permissions
def bot_copy(message):
    args = [i for i in message.text.split(' ')[1:] if i]
    data = {"cmd":"copy", "name":"", "old":"", "chat_id":message.chat.id}

    if 0<len(args):
        if re.match(r'^[a-zA-Z0-9_-]+$', args[0]):
            data["name"] = args[0]
        else:
            # bot.reply_to(message, "name format error", parse_mode="Markdown") 
            # 仅支持字母、数字、下划线、中划线
            bot.reply_to(message, "name format error, only supports letters, numbers, underscores, and hyphens", parse_mode="Markdown")
            return 
    else:
        bot.reply_to(message, "not set name", parse_mode="Markdown")
        return 

    if 1<len(args) and args[1] : 
        data["old"] = args[1]
    return data
    

def exec(message, args):
    data = {"cmd":"exec", "name":"", "start":0, "freq":0, "remain":1,"chat_id":message.chat.id}
    
    if 0<len(args) and args[0] : 
        data["name"] = args[0]
    else:
        bot.reply_to(message, "not set name", parse_mode="Markdown")
        return 

    def parse_start(ts):
        # 时间戳
        if ts.isdigit():
            return int(ts)
        # ?d?h?m?s
        if all(ch in "dhms0123456789" for ch in ts):
            cur = int(time.time())
            num = 0
            for i in ts:
                if i.isdigit():
                    num = num*10+int(i)
                else:
                    if i == 'd': cur += num*24*60*60
                    elif i == 'h': cur += num*60*60
                    elif i == 'm': cur += num*60
                    else: cur += num
                    num = 0
            return cur
        # "%Y-%m-%d/%H:%M:%S"
        try:
            time_format = "%Y-%m-%d/%H:%M:%S" # 定义时间格式
            dt = datetime.strptime(ts, time_format) # 解析时间字符串
            tz = timezone(timedelta(hours=8)) # 定义东八区时区（UTC+8）
            dt = dt.replace(tzinfo=tz) # 设置时区
            timestamp = dt.timestamp() # 获取时间戳
            return int(timestamp)
        except ValueError as e:
            return 0
    def parse_freq(cy):
        # ?d?h?m?s
        if all(ch in "dhms0123456789" for ch in cy):
            sc = 0
            num = 0
            for i in cy:
                if i.isdigit():
                    num = num*10+int(i)
                else:
                    if i == 'd': sc += num*24*60*60
                    elif i == 'h': sc += num*60*60
                    elif i == 'm': sc += num*60
                    else: sc += num
                    num = 0
            return sc
        return 0
    def parse_remain(rm):
        if rm.isdigit():
            return int(rm)
        return 1
    if 1<len(args) and args[1] : 
        data["start"] = parse_start(args[1])
    if 2<len(args) and args[2] : 
        data["freq"] = parse_freq(args[2])
    if 3<len(args) and args[3] : 
        data["remain"] = parse_remain(args[3])
    
    return data

@bot.message_handler(commands=['exec'])
@Permissions
def bot_exec(message):
    return exec(message, [i for i in message.text.split(' ')[1:] if i])
    
@bot.message_handler(commands=['terminate'])
@Permissions
def bot_terminate(message):
    data = {"cmd":"terminate", "taskid":",".join(message.text.split(' ')[1:]), "chat_id":message.chat.id}
    return data    


@bot.message_handler(commands=['cmdlist'])
@Permissions
def bot_clist(message):
    data = {"cmd":"cmdlist","chat_id":message.chat.id}
    return data


@bot.message_handler(commands=['taskidlist'])
@Permissions
def bot_tilist(message):
    data = {"cmd":"taskidlist","chat_id":message.chat.id}
    return data


@bot.message_handler(commands=['tasklist'])
@Permissions
def bot_tlist(message):
    data = {"cmd":"tasklist","chat_id":message.chat.id}
    return data

@bot.message_handler(commands=['task'])
@Permissions
def bot_tlist(message):
    data = {"cmd":"task", "id":-1, "chat_id":message.chat.id}
    args = [i for i in message.text.split(' ')[1:] if i]
    if 0<len(args) and args[0] : 
        data["id"] = args[0]
    else:
        bot.reply_to(message, "not set taskid", parse_mode="Markdown")
        return 
    return data



@bot.message_handler(commands=['preference'])
@Permissions
def bot_preference(message):
    lines = [i for i in message.text.split('\n') if i]
    # 添加删除别名
    adding = ""
    for line in lines[1:]: 
        line = line.strip()
        if line[0] == "+": #不存在空行
            adding = line[1:]
            db["preference"][adding] = []
            continue
        if line[0] == "-":
            adding = ""
            if line[1:] in db["preference"]: del db["preference"][line[1:]]
            continue
        if adding:
            db["preference"][adding].append(line)
    save_dict("db.json", db)

    # 返回别名列表
    preference_msg = "\n".join([ f"{k}\n    {"\n    ".join(v)}" for k,v in db["preference"].items()])
    bot.reply_to(message, f"alias list:\n{preference_msg}")

    # 执行别名
    args = [i for i in lines[0].split(" ")[1:] if i]
    for alias in args:
        bot.send_message(message.chat.id, f"executing {alias} ...")
        if alias in db["preference"]:
            for cmd in db["preference"][alias]:
                data = exec(message, [i for i in cmd.split(" ") if i])
                if data:
                    mqttClients[db["device"]["name"]].publish(db["device"]["ir_pub_topic"], str(data), 0)
                    bot.send_message(message.chat.id, f"{str(data)} is transmitted")

@bot.message_handler(commands=['device'])
@Permissions
def bot_device(message):
    args = [i for i in message.text.split(' ')[1:] if i]
    if 0<len(args) and args[0] in devices:
        db["device"] = devices[args[0]]
        bot.reply_to(message, f"device switch to {args[0]}")    
        save_dict("db.json", db)
    devices_msg = "\n".join([("+ " if k == db["device"]["name"] else "- ")+k for k in devices])
    bot.reply_to(message, f"device list:\n{devices_msg}")    
    return 

@bot.message_handler(commands=['usermod'])
@Permissions
def bot_usermod(message):
    if message.chat.id != env["ir_admin_chat_id"]:
        bot.reply_to(message, f"only administrators can operate")
        return 
    candicates = " ".join(message.text.split(' ')[1:])
    i = 0
    while i < len(candicates):
        while i<len(candicates) and not candicates[i].isdecimal():
            i += 1
        if i == len(candicates): break
        # 找到了第一个数字
        uid, sub = 0, 0
        if i>0 and candicates[i-1] == '-':
            sub = 1
        
        while i < len(candicates) and candicates[i].isdecimal():
            uid = uid*10 + int(candicates[i])
            i += 1
        if sub:
            if uid in db["user"] and uid != env["ir_admin_chat_id"]: db["user"].remove(uid) # 非管理员用户
        else:
            if uid not in db["user"]: db["user"].append(uid)

    save_dict("db.json", db)
    bot.reply_to(message, f"user list:\n {str(db["user"])}")


@bot.message_handler(commands=['auth'])
def bot_request(message):
    print(message)
    bot.send_message(env["ir_admin_chat_id"], f"{message.from_user.first_name} {message.from_user.last_name} request chat id: {message.chat.id}")
    bot.reply_to(message, f"your application has been submitted to the administrator")


@bot.message_handler(commands=['start'])
def bot_start(message):
    bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICVGYZDg7Fg7hZ96S_Wp9t8O26xxxVAAITAwAC2SNkIbQZSopsDmMTNAQ", reply_to_message_id=message.id)


help="""


# 机器人指令

## 学习红外指令  
执行`copy`命令后，单片机将亮灯一段时间，这段时间内存储收到的最后一条红外指令。

`/copy name [old]`
- `name`  
    - 学习红外指令的名称，非空，且只能包含数字、字母、下划线、减号
    - 若`old`未指定
        - `name`已存在则更新`name`
        - `name`已存在则从剩余容量中分配，容量已满则轮转覆盖。
- `old`  
    - 替换旧命令，单片机校验旧命令不存在则执行失败  
    - 默认 `""`

## 查看学习到的红外指令  
可以看到保存的指令名称

`/cmdlist`  

## 执行红外指令
可定时定期定量执行多条红外命令

`/exec name [start] [freq] [remain]`
- `name`  
    - 执行命令，单片机校验不存在则执行失败
    - 多条命令以逗号分割，然后命令会顺序执行
- `start`  
    - 指定执行开始时间，格式：
        - [Unix 时间戳](https://zh.wikipedia.org/wiki/UNIX%E6%97%B6%E9%97%B4)  
        - 指定日期`%Y-%m-%d/%H:%M:%S`  e.g. `2025-01-08/12:30:00`  
        - 延时时间`?d?h?m?s` e.g. `1d2h3m4s` 1天2时3分4秒后执行  
    - 小于当前时间则立即执行并更新为当前时间  
    - 默认 `0`
- `freq`  
    - 执行频率，格式：
        - 非负整数，单位秒  
        - 模式`?d?h?m?s` e.g. `1d2h3m4s` 每隔1天2时3分4秒执行
    - 默认 `0`
- `remain`  
    - 剩余执行次数  
    - 默认 `1`
    

## 任务队列信息
对于延期执行的命令，将在队列中存储

`/tasklist`  
- 当前所有任务信息，包括任务id，下次执行的时间，执行周期，剩余执行次数，当剩余执行次数为0时结束任务。

## 终止任务
可通过任务的编号终止任务队列中的某些任务

`/terminate id [...]`  
- 终止数字编号为id的任务，支持多个id，以任意非数字字符分割

## 管理和执行别名
有时需要多条命令才能达到想要的效果，命令又多又长。可以用较短的别名来组织管理。

`/preference`
- 偏好设置，可管理和执行别名，通过别名可以批量执行exec类型指令。
  ``` text
  /preference [name]
  [+addname]
  cmd
  ...
  [-delname]
  ```
    - `name` 执行别名，多个以空格分割，必须与`/preference`同一行
    - `addname` 添加别名，必须以`+`开头标识，接下来的行将识别为`exec`命令的参数。
    - `delname` 删除别名，必须以`-`开头标识
    - 支持多别名删除与添加
- 使用示例：
  ``` text
  /preference alias1
  +alias1
  cmd1
  cmd2
  -alias2
  ```
  添加别名`alias1`，`alias1`包含了`cmd1`和`cmd2`两条指令。  
  删除别名`alias2`，如果存在的话。  
  返回当前的别名列表。  
  最后执行别名alias1的命令序列。

## 设备列表
有多个单片机设备，可以展示这些设备，或者切换到某台设备。被选中的设备名称会以`+`开头标识。

`/device [name]`
- 展示设备列表
- `name`  指定切换设备名称

## 添加新用户
其他用户可能想要使用机器人，只需提供该用户的id即可为用户授权，管理员可能想要踢出某些用户。

`/usermod [[+]id] [-id]`  
- 添加或删除用户的数字id，多个id用非数字字符分割
- 纯数字或`+`开头为添加用户
- `-`开头为删除用户

## 申请使用机器人
其他用户执行此指令，管理员可为其授权。

`/auth`  
- 向管理员认证，申请使用指令，管理员会收到用户数字id

"""

@bot.message_handler(commands=['help'])
def bot_help(message):
    bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICWGYZDmNki3c5DiCYg9impkXVKXP9AAILAwAC2SNkIZ-71pEOj1BjNAQ", reply_to_message_id=message.id)
    bot.send_message(message.chat.id, help, parse_mode="Markdown")

bot.infinity_polling()


"""
bootfather Edit Commands

start - 开始
help - 帮助
copy - 学习红外命令
cmdlist - 学习到的命令
exec - 执行红外命令
tasklist - 当前任务队列信息
terminate - 终止任务
preference - 别名管理或执行
device - 展示或切换设备列表
usermod - 添加删除的用户
auth - 向管理员认证，申请使用指令
"""
