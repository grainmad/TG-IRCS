from paho.mqtt import client as mqtt
import uuid
import json
from datetime import datetime, timezone, timedelta


class MutiMqttClients:
    def __init__(self, devices, bot):
        self.devices = devices
        self.bot = bot
        self.clients = {key : self.mqtt_connect(val["ir_mqtthost"], val["ir_mqttport"], val["ir_username"], val["ir_password"], val["ir_sub_topic"], val["ir_pub_topic"]) for key, val in devices.items()}
    
    def mqtt_connect(self, mqtthost, mqttport, username, password, sub_topic, pub_topic):
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
                    if it["cron"]:
                        rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\ncron: {it["cron"]}\n剩余次数:{it["remain"]}\n'
                    else:
                        rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\n执行时间: {unix_timestamp_to_datetime(it["start"])}\n周期: {seconds_to_hms(it["freq"])}\n剩余次数:{it["remain"]}\n'
                if "tasks" in rsp:
                    for it in rsp["tasks"]:
                        if it["cron"]:
                            rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\ncron: {it["cron"]}\n剩余次数:{it["remain"]}\n'
                        else:
                            rt += f'\n任务号: {it["taskid"]}\n指令: {it["cmd"]}\n指令编号: {it["xid"]}\n执行时间: {unix_timestamp_to_datetime(it["start"])}\n周期: {seconds_to_hms(it["freq"])}\n剩余次数:{it["remain"]}\n'
                if "cmds" in rsp:
                    for it in rsp["cmds"]:
                        rt += f"  {it}\n"
                if "taskids" in rsp:
                    for it in rsp["taskids"]:
                        rt += f"  {str(it)}\n"
            self.bot.send_message(rsp["chat_id"], rt)

        mqttClient.on_connect = on_connect  # 返回连接状态的回调函数
        mqttClient.on_message = on_message 
        mqttClient.username_pw_set(username, password)
        mqttClient.connect(mqtthost, mqttport, 60)
        mqttClient.loop_start()  # 启用线程连接

        return mqttClient

    def get(self, client_name):
        return self.clients[client_name] if client_name in self.clients else None
            