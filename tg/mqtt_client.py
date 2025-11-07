"""MQTT客户端管理模块（重构版）

使用消息路由器简化MQTT客户端管理。
"""

import logging
import json
import uuid
from typing import Dict, Callable
from paho.mqtt import client as mqtt
from config import DeviceConfig
from handlers import MessageRouter


logger = logging.getLogger(__name__)


class MQTTClient:
    """单个MQTT客户端封装"""
    
    def __init__(self, device: DeviceConfig, message_router: MessageRouter):
        """
        初始化MQTT客户端
        
        Args:
            device: 设备配置
            message_router: 消息路由器
        """
        self.device = device
        self.message_router = message_router
        self.client: mqtt.Client = None
        self.logger = logging.getLogger(f"{__name__}.{device.name}")
        
        self._connect()
    
    def _on_connect(self, client, userdata, flags, rc):
        """
        MQTT连接回调
        
        Args:
            rc: 返回码
                0: 连接成功
                1: 协议版本不正确
                2: 客户端标识符无效
                3: 服务器不可用
                4: 用户名或密码不正确
                5: 未经授权
        """
        rc_messages = [
            "连接成功",
            "协议版本不正确",
            "客户端标识符无效",
            "服务器不可用",
            "用户名或密码不正确",
            "未经授权"
        ]
        
        if rc == 0:
            self.logger.info(f"MQTT连接状态: {rc_messages[rc]}")
            client.subscribe(self.device.ir_sub_topic)
        else:
            self.logger.error(f"MQTT连接失败: {rc_messages[rc]} (code: {rc})")
    
    def _on_message(self, client, userdata, msg):
        """
        MQTT消息接收回调
        
        Args:
            msg: 消息对象
        """
        try:
            message = json.loads(msg.payload.decode('utf-8'))
            self.logger.info(
                f"接收MQTT消息: topic='{msg.topic}', "
                f"qos={msg.qos}, payload='{str(message)}'"
            )
            
            # 使用消息路由器处理消息
            self.message_router.route(message)
            
        except json.JSONDecodeError as e:
            self.logger.error(f"JSON解析失败: {e}")
        except Exception as e:
            self.logger.error(f"处理MQTT消息时发生错误: {e}", exc_info=True)
    
    def _connect(self) -> None:
        """建立MQTT连接"""
        self.logger.info(
            f"初始化MQTT连接: host={self.device.ir_mqtthost}, "
            f"port={self.device.ir_mqttport}, user={self.device.ir_username}, "
            f"sub_topic={self.device.ir_sub_topic}, pub_topic={self.device.ir_pub_topic}"
        )
        
        # 创建客户端
        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION1,
            str(uuid.uuid4())
        )
        
        # 设置回调
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        
        # 设置认证
        self.client.username_pw_set(
            self.device.ir_username,
            self.device.ir_password
        )
        
        try:
            # 连接并启动循环
            self.client.connect(
                self.device.ir_mqtthost,
                self.device.ir_mqttport,
                60
            )
            self.client.loop_start()
            
            self.logger.info(
                f"MQTT客户端启动成功: {self.device.ir_mqtthost}:"
                f"{self.device.ir_mqttport}"
            )
        except Exception as e:
            self.logger.error(f"MQTT连接启动失败: {e}")
            raise
    
    def publish(self, topic: str, payload: str, qos: int = 0) -> None:
        """
        发布MQTT消息
        
        Args:
            topic: 主题
            payload: 消息内容
            qos: 服务质量等级
        """
        if self.client:
            self.client.publish(topic, payload, qos)
            self.logger.debug(f"发布MQTT消息: topic={topic}, payload={payload}")
    
    def disconnect(self) -> None:
        """断开MQTT连接"""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            self.logger.info("MQTT客户端已断开")


class MQTTClientManager:
    """MQTT客户端管理器"""
    
    def __init__(self, devices: Dict[str, DeviceConfig], bot):
        """
        初始化MQTT客户端管理器
        
        Args:
            devices: 设备配置字典
            bot: Telegram bot实例
        """
        self.devices = devices
        self.bot = bot
        self.logger = logging.getLogger(__name__)
        
        # 创建消息路由器
        self.message_router = MessageRouter(bot)
        
        # 创建所有设备的MQTT客户端
        self.clients: Dict[str, MQTTClient] = {}
        for name, device in devices.items():
            try:
                self.clients[name] = MQTTClient(device, self.message_router)
            except Exception as e:
                self.logger.error(f"初始化设备 {name} 的MQTT客户端失败: {e}")
    
    def get(self, device_name: str) -> MQTTClient:
        """
        获取指定设备的MQTT客户端
        
        Args:
            device_name: 设备名称
            
        Returns:
            MQTT客户端，如果不存在则返回None
        """
        client = self.clients.get(device_name)
        if client is None:
            self.logger.warning(f"未找到MQTT客户端: {device_name}")
        return client
    
    def publish_to_device(self, device_name: str, data: Dict) -> None:
        """
        向指定设备发布消息
        
        Args:
            device_name: 设备名称
            data: 消息数据
        """
        client = self.get(device_name)
        if client:
            device = self.devices[device_name]
            client.publish(device.ir_pub_topic, str(data), 0)
    
    def disconnect_all(self) -> None:
        """断开所有MQTT客户端"""
        for name, client in self.clients.items():
            try:
                client.disconnect()
            except Exception as e:
                self.logger.error(f"断开设备 {name} 的MQTT客户端失败: {e}")
