"""MQTT消息处理器模块

处理从MQTT接收到的各种类型的消息。
"""

import logging
from typing import Dict, Any, List
from telebot import types
import util


logger = logging.getLogger(__name__)


class MessageHandler:
    """MQTT消息处理器基类"""
    
    def __init__(self, bot):
        """
        初始化消息处理器
        
        Args:
            bot: Telegram bot实例
        """
        self.bot = bot
        self.logger = logging.getLogger(self.__class__.__name__)
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理消息
        
        Args:
            message: 消息字典
        """
        raise NotImplementedError


class TaskMessageHandler(MessageHandler):
    """单个任务消息处理器"""
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理单个任务的消息
        
        Args:
            message: 包含task和message字段的消息字典
        """
        task = message.get("task", {})
        msg_text = message.get("message", "")
        chat_id = message.get("chat_id")
        
        if not task or not chat_id:
            self.logger.warning(f"任务消息缺少必要字段: {message}")
            return
        
        # 构建任务信息文本
        text = f"{msg_text}\n"
        
        if task.get("cron"):
            text += (
                f'\n任务号: {task.get("taskid")}\n'
                f'任务名: {task.get("taskname")}\n'
                f'指令: {task.get("cmd")}\n'
                f'指令编号: {task.get("xid")}\n'
                f'cron: {task.get("cron")}\n'
                f'剩余次数: {task.get("remain")}\n'
            )
        else:
            start_time = util.unix_timestamp_to_datetime(task.get("start", 0))
            freq = util.seconds_to_hms(task.get("freq", 0))
            text += (
                f'\n任务号: {task.get("taskid")}\n'
                f'任务名: {task.get("taskname")}\n'
                f'指令: {task.get("cmd")}\n'
                f'指令编号: {task.get("xid")}\n'
                f'执行时间: {start_time}\n'
                f'周期: {freq}\n'
                f'剩余次数: {task.get("remain")}\n'
            )
        
        self.bot.send_message(chat_id, text)


class TasksMessageHandler(MessageHandler):
    """任务列表消息处理器"""
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理任务列表消息
        
        Args:
            message: 包含tasks和message字段的消息字典
        """
        tasks = message.get("tasks", [])
        msg_text = message.get("message", "")
        chat_id = message.get("chat_id")
        
        if chat_id is None:
            self.logger.warning(f"任务列表消息缺少chat_id: {message}")
            return
        
        text = f"{msg_text}\n"
        task_names = set()
        task_ids = set()
        
        # 构建任务列表文本
        for task in tasks:
            if task.get("cron"):
                text += (
                    f'\n任务号: {task.get("taskid")}\n'
                    f'任务名: {task.get("taskname")}\n'
                    f'指令: {task.get("cmd")}\n'
                    f'指令编号: {task.get("xid")}\n'
                    f'cron: {task.get("cron")}\n'
                    f'剩余次数: {task.get("remain")}\n'
                )
            else:
                start_time = util.unix_timestamp_to_datetime(task.get("start", 0))
                freq = util.seconds_to_hms(task.get("freq", 0))
                text += (
                    f'\n任务号: {task.get("taskid")}\n'
                    f'任务名: {task.get("taskname")}\n'
                    f'指令: {task.get("cmd")}\n'
                    f'指令编号: {task.get("xid")}\n'
                    f'执行时间: {start_time}\n'
                    f'周期: {freq}\n'
                    f'剩余次数: {task.get("remain")}\n'
                )
            
            # 收集任务名和任务ID用于创建按钮
            if task.get("taskname"):
                task_names.add(task.get("taskname"))
            if task.get("taskid") is not None:
                task_ids.add(task.get("taskid"))
        
        # 创建内联键盘
        markup = types.InlineKeyboardMarkup()
        
        for name in task_names:
            btn = types.InlineKeyboardButton(
                f"{name} (name)",
                callback_data=f"taskname_{name}"
            )
            markup.add(btn)
        
        for task_id in task_ids:
            btn = types.InlineKeyboardButton(
                f"{task_id} (id)",
                callback_data=f"taskid_{task_id}"
            )
            markup.add(btn)
        
        if tasks:
            text += "\nterminate by following buttons"
        
        self.bot.send_message(chat_id, text, reply_markup=markup)


class CommandListHandler(MessageHandler):
    """命令列表消息处理器"""
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理命令列表消息
        
        Args:
            message: 包含cmds和message字段的消息字典
        """
        cmds = message.get("cmds", [])
        msg_text = message.get("message", "")
        chat_id = message.get("chat_id")
        
        if chat_id is None:
            self.logger.warning(f"命令列表消息缺少chat_id: {message}")
            return
        
        text = f"{msg_text}\n"
        for cmd in cmds:
            text += f"  {cmd}\n"
        
        self.bot.send_message(chat_id, text)


class TaskIdListHandler(MessageHandler):
    """任务ID列表消息处理器"""
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理任务ID列表消息
        
        Args:
            message: 包含taskids和message字段的消息字典
        """
        taskids = message.get("taskids", [])
        msg_text = message.get("message", "")
        chat_id = message.get("chat_id")
        
        if chat_id is None:
            self.logger.warning(f"任务ID列表消息缺少chat_id: {message}")
            return
        
        text = f"{msg_text}\n"
        for taskid in taskids:
            text += f"  {str(taskid)}\n"
        
        self.bot.send_message(chat_id, text)


class SimpleMessageHandler(MessageHandler):
    """简单消息处理器（仅包含message字段）"""
    
    def handle(self, message: Dict[str, Any]) -> None:
        """
        处理简单消息
        
        Args:
            message: 包含message和chat_id字段的消息字典
        """
        msg_text = message.get("message", "")
        chat_id = message.get("chat_id")
        
        if chat_id is None:
            self.logger.warning(f"简单消息缺少chat_id: {message}")
            return
        
        self.bot.send_message(chat_id, msg_text)


class MessageRouter:
    """消息路由器，根据消息类型分发到对应的处理器"""
    
    def __init__(self, bot):
        """
        初始化消息路由器
        
        Args:
            bot: Telegram bot实例
        """
        self.bot = bot
        self.logger = logging.getLogger(__name__)
        
        # 初始化各类处理器
        self.handlers = {
            "task": TaskMessageHandler(bot),
            "tasks": TasksMessageHandler(bot),
            "cmds": CommandListHandler(bot),
            "taskids": TaskIdListHandler(bot),
            "simple": SimpleMessageHandler(bot)
        }
    
    def route(self, message: Dict[str, Any]) -> None:
        """
        路由消息到对应的处理器
        
        Args:
            message: MQTT消息字典
        """
        try:
            # 根据消息包含的字段判断消息类型
            if "task" in message:
                self.handlers["task"].handle(message)
            elif "tasks" in message:
                self.handlers["tasks"].handle(message)
            elif "cmds" in message:
                self.handlers["cmds"].handle(message)
            elif "taskids" in message:
                self.handlers["taskids"].handle(message)
            else:
                self.handlers["simple"].handle(message)
        except Exception as e:
            self.logger.error(f"处理MQTT消息时发生错误: {e}", exc_info=True)
