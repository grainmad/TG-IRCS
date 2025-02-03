import telebot
import requests
import copy
import os
import json
import json
from datetime import datetime, timezone, timedelta
import re
from dotenv import load_dotenv
from paho.mqtt import client as mqtt
import uuid
import time

load_dotenv()
BOT_TOKEN = os.getenv('IR_BOT_TOKEN')
SUB_TOPIC = os.getenv('IR_SUB_TOPIC')
PUB_TOPIC = os.getenv('IR_PUB_TOPIC')
USERNAME = os.getenv('IR_USERNAME')
PASSWORD = os.getenv('IR_PASSWORD')
ADMIN_CHAT_ID = int(os.getenv('IR_ADMIN_CHAT_ID'))
MQTTHOST = os.getenv('IR_MQTTHOST')
MQTTPORT = int(os.getenv('IR_MQTTPORT'))

bot = telebot.TeleBot(BOT_TOKEN)

print("env:", BOT_TOKEN, SUB_TOPIC, PUB_TOPIC, USERNAME, PASSWORD, ADMIN_CHAT_ID, MQTTHOST, MQTTPORT)

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
    client.subscribe(SUB_TOPIC)




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

def mqtt_connect():
    """连接MQTT服务器"""
    mqttClient = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, str(uuid.uuid4()))
    mqttClient.on_connect = on_connect  # 返回连接状态的回调函数
    mqttClient.on_message = on_message 
    mqttClient.username_pw_set(USERNAME, PASSWORD)
    mqttClient.connect(MQTTHOST, MQTTPORT, 60)
    mqttClient.loop_start()  # 启用线程连接

    return mqttClient


mqttClient = mqtt_connect()


user = {ADMIN_CHAT_ID}



def Permissions(func):
    def wrapper(*args, **kwargs):
        message = args[0]
        print(message)
        if message.chat.id not in user:
            bot.reply_to(message, f"authentication required")
            return 
        data = func(*args, **kwargs)  # 调用被装饰的函数，并传递所有参数
        if data:
            if isinstance(data, list):
                for i in data:
                    mqttClient.publish(PUB_TOPIC, str(i), 0)
                bot.reply_to(message, f"{str(data)} is transmitted")
            else:
                mqttClient.publish(PUB_TOPIC, str(data), 0)
                bot.reply_to(message, f"{str(data)} is transmitted")
    return wrapper

@bot.message_handler(commands=['copy'])
@Permissions
def bot_copy(message):
    args = message.text.split(' ')[1:]
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
    

@bot.message_handler(commands=['exec'])
@Permissions
def bot_exec(message):
    args = message.text.split(' ')[1:]
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
            sc = int(time.time())
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
            return sc+num
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
    cmds = data["name"].split(",")
    datalist = []
    for it in cmds:
        data["name"] = it
        datalist.append(copy.copy(data))
        data["start"]+=1 # 确保执行顺序
    return datalist
    
@bot.message_handler(commands=['terminate'])
@Permissions
def bot_exec(message):
    args = message.text.split(' ')[1:]
    data = {"cmd":"terminate", "taskid":-1, "chat_id":message.chat.id}
    
    if 0<len(args) and args[0] : 
        data["taskid"] = args[0]
    else:
        bot.reply_to(message, "not set taskid", parse_mode="Markdown")
        return 
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
    args = message.text.split(' ')[1:]
    if 0<len(args) and args[0] : 
        data["id"] = args[0]
    else:
        bot.reply_to(message, "not set taskid", parse_mode="Markdown")
        return 
    return data

@bot.message_handler(commands=['adduser'])
def bot_adduser(message):
    print(message)
    if message.chat.id != ADMIN_CHAT_ID:
        bot.reply_to(message, f"only administrators can operate")
        return 
    for i in message.text.split(' ')[1:]:
        user.add(int(i))
    bot.reply_to(message, f"user list:\n {str(user)}")


@bot.message_handler(commands=['auth'])
def bot_request(message):
    print(message)
    bot.send_message(ADMIN_CHAT_ID, f"{message.from_user.first_name} {message.from_user.last_name} request chat id: {message.chat.id}")
    bot.reply_to(message, f"your application has been submitted to the administrator")


@bot.message_handler(commands=['start'])
def bot_start(message):
    bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICVGYZDg7Fg7hZ96S_Wp9t8O26xxxVAAITAwAC2SNkIbQZSopsDmMTNAQ", reply_to_message_id=message.id)


help="""
`/exec name [start] [freq] [remain]`
- `name`  
    - 执行命令，单片机校验不存在则执行失败
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
    
`/terminate id`
- 终止编号为id的任务

`/copy name [old]`
- `name`  
    - 学习红外命令的名称  
    - 若`old`未指定
        - `name`已存在则更新`name`
        - `name`已存在则从剩余容量中分配，容量已满（默认 5）则轮转覆盖。
- `old`  
    - 替换旧命令，单片机校验旧命令不存在则执行失败  
    - 默认 `""`

`/tasklist`  
- 当前所有任务信息

`/taskidlist`  
- 当前所有任务id

`/task id`  
- 获取编号为id的任务信息

`/cmdlist`  
- 当前学习的命令

`/adduser`  
- 添加可使用机器人指令的用户

`/auth`  
- 向管理员认证，申请使用指令

"""

@bot.message_handler(commands=['help'])
def bot_help(message):
    bot.send_sticker(chat_id=message.chat.id, sticker="CAACAgQAAxkBAAICWGYZDmNki3c5DiCYg9impkXVKXP9AAILAwAC2SNkIZ-71pEOj1BjNAQ", reply_to_message_id=message.id)
    bot.send_message(message.chat.id, help, parse_mode="Markdown")

bot.infinity_polling()


"""
bootfather Edit Commands

exec - 执行红外命令
terminate - 终止任务
copy - 学习红外命令
tasklist - 当前所有任务信息
taskidlist - 当前所有任务id
task - 获取编号为id的任务信息
cmdlist - 当前学习的命令
adduser - 添加可使用机器人指令的用户
auth - 向管理员认证，申请使用指令
start - 开始
help - 帮助
"""
