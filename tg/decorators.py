"""装饰器模块

提供权限检查、命令执行等功能的装饰器。
"""

import logging
import json
from functools import wraps
from typing import Callable, Dict, Any, Optional


logger = logging.getLogger(__name__)


class PermissionDecorator:
    """权限装饰器类"""
    
    def __init__(self, bot, db: Dict[str, Any], mqtt_publisher: Callable):
        """
        初始化权限装饰器
        
        Args:
            bot: Telegram bot实例
            db: 数据库字典
            mqtt_publisher: MQTT发布函数
        """
        self.bot = bot
        self.db = db
        self.mqtt_publisher = mqtt_publisher
    
    def require_auth(self, func: Callable) -> Callable:
        """
        需要权限验证的装饰器
        
        检查用户是否在授权列表中，并自动发布MQTT消息
        """
        @wraps(func)
        def wrapper(message, *args, **kwargs):
            # 清理消息内容，只保留ASCII可见字符和空格换行
            message.text = ''.join(
                filter(lambda char: 32 <= ord(char) <= 126 or char == '\n', message.text)
            )
            
            logger.debug(f"权限检查: user_id={message.from_user.id}, chat_id={message.chat.id}")
            
            # 权限验证
            if message.chat.id not in self.db["user"]:
                logger.warning(
                    f"未授权用户尝试访问: user_id={message.from_user.id}, "
                    f"chat_id={message.chat.id}"
                )
                self.bot.reply_to(message, "authentication required")
                return
            
            logger.info(
                f"用户权限验证通过: user_id={message.from_user.id}, "
                f"chat_id={message.chat.id}"
            )
            
            # 执行命令
            try:
                data = func(message, *args, **kwargs)
                
                # 如果返回数据，则发布到MQTT
                if data:
                    logger.info(f"发送MQTT消息: data={str(data)}")
                    self.mqtt_publisher(data)
                    
                    # 发送确认消息（隐藏完整chat_id）
                    data_copy = data.copy()
                    data_copy['chat_id'] %= 100000
                    self.bot.send_message(
                        message.chat.id,
                        f"{json.dumps(data_copy, ensure_ascii=False)} is transmitted"
                    )
            except ValueError as e:
                # 处理命令解析错误
                logger.warning(f"命令解析错误: {e}")
                self.bot.reply_to(message, str(e))
            except Exception as e:
                # 处理其他错误
                logger.error(f"命令执行失败: {e}", exc_info=True)
                self.bot.reply_to(message, f"命令执行失败: {str(e)}")
        
        return wrapper
    
    def no_auth_required(self, func: Callable) -> Callable:
        """
        不需要权限验证的装饰器
        
        用于公开命令如/start, /help, /auth等
        """
        @wraps(func)
        def wrapper(message, *args, **kwargs):
            try:
                return func(message, *args, **kwargs)
            except Exception as e:
                logger.error(f"命令执行失败: {e}", exc_info=True)
                self.bot.reply_to(message, f"命令执行失败")
        
        return wrapper


def create_permission_decorator(bot, db: Dict[str, Any], mqtt_publisher: Callable) -> PermissionDecorator:
    """
    创建权限装饰器实例的工厂函数
    
    Args:
        bot: Telegram bot实例
        db: 数据库字典
        mqtt_publisher: MQTT发布函数
        
    Returns:
        PermissionDecorator实例
    """
    return PermissionDecorator(bot, db, mqtt_publisher)
