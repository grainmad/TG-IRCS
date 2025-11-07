"""机器人服务模块（重构版）

提供简化的服务类，使用解析器模块处理命令。
"""

import logging
from typing import Dict, Any, Optional
import util
import parsers
from config import ConfigManager


logger = logging.getLogger(__name__)


class BotService:
    """机器人服务类"""
    
    def __init__(self, bot, config_manager: ConfigManager):
        """
        初始化机器人服务
        
        Args:
            bot: Telegram bot实例
            config_manager: 配置管理器实例
        """
        self.bot = bot
        self.config = config_manager
        self.help_text: Optional[str] = None
        self.logger = logging.getLogger(__name__)
    
    def copy(self, message) -> Dict[str, Any]:
        """
        处理copy命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行copy命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.CopyCommandParser.parse(message.text, message.chat.id)
    
    def exec(self, message) -> Dict[str, Any]:
        """
        处理exec命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行exec命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.ExecCommandParser.parse(message.text, message.chat.id)
    
    def terminate(self, message) -> Dict[str, Any]:
        """
        处理terminate命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行terminate命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.SimpleCommandParser.parse_with_args(
            "terminate", message.text, message.chat.id, "taskid"
        )
    
    def terminatename(self, message) -> Dict[str, Any]:
        """
        处理terminatename命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行terminatename命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.SimpleCommandParser.parse_with_args(
            "terminatename", message.text, message.chat.id, "taskname"
        )
    
    def cmdlist(self, message) -> Dict[str, Any]:
        """
        处理cmdlist命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行cmdlist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.SimpleCommandParser.parse_no_args("cmdlist", message.chat.id)
    
    def taskidlist(self, message) -> Dict[str, Any]:
        """
        处理taskidlist命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行taskidlist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.SimpleCommandParser.parse_no_args("taskidlist", message.chat.id)
    
    def tasklist(self, message) -> Dict[str, Any]:
        """
        处理tasklist命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行tasklist命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.SimpleCommandParser.parse_no_args("tasklist", message.chat.id)
    
    def task(self, message) -> Dict[str, Any]:
        """
        处理task命令
        
        Args:
            message: Telegram消息对象
            
        Returns:
            命令数据字典
        """
        self.logger.info(f"执行task命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        return parsers.TaskCommandParser.parse(message.text, message.chat.id)
    
    def device(self, message) -> None:
        """
        处理device命令
        
        Args:
            message: Telegram消息对象
        """
        self.logger.info(f"执行device命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        
        args = [i for i in message.text.split(' ')[1:] if i]
        
        # 如果有参数，尝试切换设备
        if len(args) > 0 and args[0] in self.config.devices:
            if self.config.set_current_device(args[0]):
                self.bot.reply_to(message, f"device switch to {args[0]}")
        
        # 显示设备列表
        current_device = self.config.get_current_device()
        devices_msg = "\n".join([
            ("+ " if k == current_device.name else "- ") + k
            for k in self.config.devices.keys()
        ])
        self.bot.reply_to(message, f"device list:\n{devices_msg}")
        
        self.logger.info(f"device命令执行成功，当前设备: {current_device.name}")
    
    def usermod(self, message) -> None:
        """
        处理usermod命令（管理员专用）
        
        Args:
            message: Telegram消息对象
        """
        self.logger.info(f"执行usermod命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        
        # 检查管理员权限
        if message.chat.id != self.config.env.ir_admin_chat_id:
            self.bot.reply_to(message, "only administrators can operate")
            return
        
        # 解析用户ID
        candidates = " ".join(message.text.split(' ')[1:])
        i = 0
        
        while i < len(candidates):
            # 跳过非数字字符
            while i < len(candidates) and not candidates[i].isdecimal():
                i += 1
            
            if i == len(candidates):
                break
            
            # 检查是否为删除操作
            is_remove = i > 0 and candidates[i-1] == '-'
            
            # 提取用户ID
            uid = 0
            while i < len(candidates) and candidates[i].isdecimal():
                uid = uid * 10 + int(candidates[i])
                i += 1
            
            # 执行添加或删除操作
            if is_remove:
                self.config.remove_user(uid)
            else:
                self.config.add_user(uid)
        
        # 显示用户列表
        self.bot.reply_to(message, f"user list:\n{str(self.config.db.user)}")
        self.logger.info("usermod命令执行成功: 用户权限已修改")
    
    def auth(self, message) -> None:
        """
        处理auth命令（用户请求授权）
        
        Args:
            message: Telegram消息对象
        """
        self.logger.info(f"执行auth命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        
        # 通知管理员
        self.bot.send_message(
            self.config.env.ir_admin_chat_id,
            f"{message.from_user.first_name} {message.from_user.last_name} "
            f"request chat id: {message.chat.id}"
        )
        
        # 回复用户
        self.bot.reply_to(message, "your application has been submitted to the administrator")
        
        if self.config.is_user_authorized(message.chat.id):
            self.logger.info(f"用户认证成功: user_id={message.from_user.id}")
        else:
            self.logger.warning(f"用户认证失败: user_id={message.from_user.id}")
    
    def start(self, message) -> None:
        """
        处理start命令
        
        Args:
            message: Telegram消息对象
        """
        self.logger.info(f"执行start命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        
        self.bot.send_sticker(
            chat_id=message.chat.id,
            sticker="CAACAgQAAxkBAAICVGYZDg7Fg7hZ96S_Wp9t8O26xxxVAAITAwAC2SNkIbQZSopsDmMTNAQ",
            reply_to_message_id=message.id
        )
        
        self.logger.debug("start命令执行完成")
    
    def help(self, message) -> None:
        """
        处理help命令
        
        Args:
            message: Telegram消息对象
        """
        self.logger.info(f"执行help命令: user_id={message.from_user.id}, chat_id={message.chat.id}")
        
        self.bot.send_sticker(
            chat_id=message.chat.id,
            sticker="CAACAgQAAxkBAAICWGYZDmNki3c5DiCYg9impkXVKXP9AAILAwAC2SNkIZ-71pEOj1BjNAQ",
            reply_to_message_id=message.id
        )
        
        # 懒加载帮助文本
        if not self.help_text:
            self.help_text = util.load_help()
        
        self.bot.send_message(message.chat.id, self.help_text, parse_mode="Markdown")
        self.logger.debug("help命令执行完成")
