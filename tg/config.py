"""配置管理模块（重构版）

使用dataclass提供类型安全的配置管理。
"""

import logging
from dataclasses import dataclass, field
from typing import Dict, List, Any
import util


logger = logging.getLogger(__name__)


@dataclass
class DeviceConfig:
    """设备配置"""
    name: str
    ir_mqtthost: str
    ir_mqttport: int
    ir_username: str
    ir_password: str
    ir_sub_topic: str
    ir_pub_topic: str
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'DeviceConfig':
        """从字典创建设备配置"""
        return cls(
            name=data.get("name", ""),
            ir_mqtthost=data.get("ir_mqtthost", ""),
            ir_mqttport=data.get("ir_mqttport", 1883),
            ir_username=data.get("ir_username", ""),
            ir_password=data.get("ir_password", ""),
            ir_sub_topic=data.get("ir_sub_topic", ""),
            ir_pub_topic=data.get("ir_pub_topic", "")
        )
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            "name": self.name,
            "ir_mqtthost": self.ir_mqtthost,
            "ir_mqttport": self.ir_mqttport,
            "ir_username": self.ir_username,
            "ir_password": self.ir_password,
            "ir_sub_topic": self.ir_sub_topic,
            "ir_pub_topic": self.ir_pub_topic
        }


@dataclass
class EnvConfig:
    """环境配置"""
    ir_bot_token: str
    ir_admin_chat_id: int
    devices: List[DeviceConfig] = field(default_factory=list)
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'EnvConfig':
        """从字典创建环境配置"""
        devices = [DeviceConfig.from_dict(d) for d in data.get("device", [])]
        return cls(
            ir_bot_token=data.get("ir_bot_token", ""),
            ir_admin_chat_id=data.get("ir_admin_chat_id", 0),
            devices=devices
        )


@dataclass
class DatabaseConfig:
    """数据库配置"""
    device: DeviceConfig
    user: List[int] = field(default_factory=list)
    preference: Dict[str, Dict[str, List[str]]] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典用于保存"""
        return {
            "device": self.device.to_dict(),
            "user": self.user,
            "preference": self.preference
        }


class ConfigManager:
    """配置管理器"""
    
    def __init__(self, env_file: str = util.ENVFILE, db_file: str = util.DBFILE):
        """
        初始化配置管理器
        
        Args:
            env_file: 环境配置文件路径
            db_file: 数据库文件路径
        """
        self.env_file = env_file
        self.db_file = db_file
        self.env: EnvConfig = None
        self.db: DatabaseConfig = None
        self.devices: Dict[str, DeviceConfig] = {}
        
        self._load()
    
    def _load(self) -> None:
        """加载配置"""
        # 加载环境配置
        env_data = util.load_dict(self.env_file)
        self.env = EnvConfig.from_dict(env_data)
        
        # 构建设备字典
        self.devices = {device.name: device for device in self.env.devices}
        
        # 加载数据库配置
        db_data = util.load_dict(self.db_file)
        
        # 设置默认值
        if "device" not in db_data and self.env.devices:
            device_dict = self.env.devices[0].to_dict()
        else:
            device_dict = db_data.get("device", {})
        
        current_device = DeviceConfig.from_dict(device_dict)
        
        if "user" not in db_data:
            users = [self.env.ir_admin_chat_id]
        else:
            users = db_data.get("user", [])
        
        if "preference" not in db_data:
            preferences = {}
        else:
            preferences = db_data.get("preference", {})
        
        # 确保当前设备在偏好设置中存在
        if current_device.name not in preferences:
            preferences[current_device.name] = {}
        
        self.db = DatabaseConfig(
            device=current_device,
            user=users,
            preference=preferences
        )
        
        # 保存初始化后的配置
        self.save_db()
        
        logger.info(f"配置加载完成: 设备数={len(self.devices)}, 用户数={len(self.db.user)}")
    
    def save_db(self) -> None:
        """保存数据库配置"""
        util.save_dict(self.db_file, self.db.to_dict())
        logger.debug("数据库配置已保存")
    
    def get_current_device(self) -> DeviceConfig:
        """获取当前设备配置"""
        return self.db.device
    
    def set_current_device(self, device_name: str) -> bool:
        """
        设置当前设备
        
        Args:
            device_name: 设备名称
            
        Returns:
            是否成功设置
        """
        if device_name not in self.devices:
            logger.warning(f"设备不存在: {device_name}")
            return False
        
        self.db.device = self.devices[device_name]
        
        # 确保设备在偏好设置中存在
        if device_name not in self.db.preference:
            self.db.preference[device_name] = {}
        
        self.save_db()
        logger.info(f"切换设备: {device_name}")
        return True
    
    def get_current_preferences(self) -> Dict[str, List[str]]:
        """获取当前设备的偏好设置"""
        return self.db.preference.get(self.db.device.name, {})
    
    def update_preferences(self, preferences: Dict[str, List[str]]) -> None:
        """
        更新当前设备的偏好设置
        
        Args:
            preferences: 新的偏好设置
        """
        self.db.preference[self.db.device.name] = preferences
        self.save_db()
        logger.info(f"偏好设置已更新: {self.db.device.name}")
    
    def is_user_authorized(self, chat_id: int) -> bool:
        """
        检查用户是否已授权
        
        Args:
            chat_id: 聊天ID
            
        Returns:
            是否已授权
        """
        return chat_id in self.db.user
    
    def add_user(self, chat_id: int) -> None:
        """
        添加授权用户
        
        Args:
            chat_id: 聊天ID
        """
        if chat_id not in self.db.user:
            self.db.user.append(chat_id)
            self.save_db()
            logger.info(f"添加授权用户: {chat_id}")
    
    def remove_user(self, chat_id: int) -> None:
        """
        移除授权用户
        
        Args:
            chat_id: 聊天ID
        """
        if chat_id in self.db.user and chat_id != self.env.ir_admin_chat_id:
            self.db.user.remove(chat_id)
            self.save_db()
            logger.info(f"移除授权用户: {chat_id}")


def load_config(env_file: str = util.ENVFILE, db_file: str = util.DBFILE) -> ConfigManager:
    """
    加载配置的便捷函数
    
    Args:
        env_file: 环境配置文件路径
        db_file: 数据库文件路径
        
    Returns:
        ConfigManager实例
    """
    return ConfigManager(env_file, db_file)
