"""回调处理器模块

处理Telegram内联按钮的回调。
"""

import logging
from typing import Callable
from telebot import types
import util


logger = logging.getLogger(__name__)


class CallbackHandler:
    """回调处理器基类"""
    
    def __init__(self, bot, config_manager, service, mqtt_publisher: Callable):
        """
        初始化回调处理器
        
        Args:
            bot: Telegram bot实例
            config_manager: 配置管理器
            service: 机器人服务实例
            mqtt_publisher: MQTT发布函数
        """
        self.bot = bot
        self.config = config_manager
        self.service = service
        self.mqtt_publisher = mqtt_publisher
        self.logger = logging.getLogger(self.__class__.__name__)


class TaskCallbackHandler(CallbackHandler):
    """任务相关回调处理器"""
    
    def handle_taskid(self, call) -> None:
        """
        处理任务ID终止回调
        
        Args:
            call: 回调查询对象
        """
        taskid = call.data[7:]  # 移除 "taskid_" 前缀
        call.message.text = f"terminate {taskid}"
        
        try:
            data = self.service.terminate(call.message)
            if data:
                self.mqtt_publisher(data)
                data['chat_id'] %= 100000
                self.bot.send_message(
                    call.message.chat.id,
                    f"{str(data)} is transmitted"
                )
        except Exception as e:
            self.logger.error(f"终止任务失败: {e}")
            self.bot.answer_callback_query(call.id, "操作失败")
            return
        
        self.bot.answer_callback_query(call.id)
        
        # 刷新任务列表
        call.message.text = "tasklist"
        data = self.service.tasklist(call.message)
        if data:
            self.mqtt_publisher(data)
    
    def handle_taskname(self, call) -> None:
        """
        处理任务名称终止回调
        
        Args:
            call: 回调查询对象
        """
        taskname = call.data[9:]  # 移除 "taskname_" 前缀
        call.message.text = f"terminatename {taskname}"
        
        try:
            data = self.service.terminatename(call.message)
            if data:
                self.mqtt_publisher(data)
                data['chat_id'] %= 100000
                self.bot.send_message(
                    call.message.chat.id,
                    f"{str(data)} is transmitted"
                )
        except Exception as e:
            self.logger.error(f"终止任务失败: {e}")
            self.bot.answer_callback_query(call.id, "操作失败")
            return
        
        self.bot.answer_callback_query(call.id)
        
        # 刷新任务列表
        call.message.text = "tasklist"
        data = self.service.tasklist(call.message)
        if data:
            self.mqtt_publisher(data)


class AliasCallbackHandler(CallbackHandler):
    """别名相关回调处理器"""
    
    def handle_exec(self, call) -> None:
        """
        处理别名执行回调
        
        Args:
            call: 回调查询对象
        """
        alias = call.data[10:]  # 移除 "alias_exc_" 前缀
        self._exec_alias(alias, call.message)
        self.bot.answer_callback_query(call.id)
    
    def handle_delete(self, call) -> None:
        """
        处理别名删除回调
        
        Args:
            call: 回调查询对象
        """
        alias = call.data[10:]  # 移除 "alias_del_" 前缀
        preferences = self.config.get_current_preferences()
        
        if alias in preferences:
            del preferences[alias]
            self.config.update_preferences(preferences)
        
        self.bot.answer_callback_query(call.id)
        self._show_delete_menu(call.message)
    
    def handle_add(self, call) -> None:
        """
        处理别名添加回调
        
        Args:
            call: 回调查询对象
        """
        msg = self.bot.send_message(
            call.message.chat.id,
            "first input alias name, then input commands. e.g.\n"
            "myalias\nmycommand arg1 arg2"
        )
        
        def add_command(message):
            lines = [i.strip() for i in message.text.split('\n') if i and i.strip()]
            if len(lines) < 2:
                self.bot.send_message(message.chat.id, "invalid format")
                return
            
            alias = lines[0]
            preferences = self.config.get_current_preferences()
            preferences[alias] = lines[1:]
            self.config.update_preferences(preferences)
            
            self.bot.answer_callback_query(call.id)
            self._show_main_menu(call.message, send=True)
        
        self.bot.register_next_step_handler(msg, add_command)
    
    def handle_menu_switch(self, call) -> None:
        """
        处理菜单切换回调
        
        Args:
            call: 回调查询对象
        """
        self.bot.answer_callback_query(call.id)
        
        if call.data == "alias_del":
            self._show_delete_menu(call.message)
        elif call.data == "alias_exc":
            self._show_exec_menu(call.message)
        elif call.data == "alias_cancel":
            self._show_main_menu(call.message)
    
    def _exec_alias(self, alias: str, message) -> None:
        """执行别名"""
        preferences = self.config.get_current_preferences()
        
        if alias not in preferences:
            self.bot.send_message(message.chat.id, f"alias {alias} not found")
            return
        
        for seq in preferences[alias]:
            cmd_parts = [i for i in seq.split(" ") if i]
            if len(cmd_parts) == 0:
                continue
            
            cmd = cmd_parts[0]
            message.text = seq
            
            try:
                data = util.dynamic_call(self.service, cmd, message)
                if data:
                    self.logger.info(f"命令执行成功: {cmd}, data={str(data)}")
                    self.mqtt_publisher(data)
                    data['chat_id'] %= 100000
                    self.bot.send_message(
                        message.chat.id,
                        f"{str(data)} is transmitted"
                    )
            except AttributeError:
                self.logger.error(f"命令不存在: {cmd}")
                self.bot.send_message(message.chat.id, f"command {cmd} not found")
    
    def _show_alias_list(self, message, markup, text: str, send: bool = False) -> None:
        """显示别名列表"""
        preferences = self.config.get_current_preferences()
        preference_msg = "\n".join([
            f"+{k}\n{chr(10).join('    ' + line for line in v)}"
            for k, v in preferences.items()
        ])
        
        full_text = f"alias list:\n{preference_msg}\n\n{text}"
        
        if send:
            self.bot.send_message(message.chat.id, full_text, reply_markup=markup)
        else:
            self.bot.edit_message_text(
                full_text,
                chat_id=message.chat.id,
                message_id=message.message_id,
                reply_markup=markup
            )
    
    def _show_delete_menu(self, message, send: bool = False) -> None:
        """显示删除菜单"""
        preferences = self.config.get_current_preferences()
        markup = types.InlineKeyboardMarkup()
        
        for alias in preferences:
            btn = types.InlineKeyboardButton(
                alias,
                callback_data=f"alias_del_{alias}"
            )
            markup.add(btn)
        
        markup.add(types.InlineKeyboardButton(
            "⬅ back to alias list",
            callback_data="alias_cancel"
        ))
        
        self._show_alias_list(message, markup, "which alias del", send)
    
    def _show_exec_menu(self, message, send: bool = False) -> None:
        """显示执行菜单"""
        preferences = self.config.get_current_preferences()
        markup = types.InlineKeyboardMarkup()
        
        for alias in preferences:
            btn = types.InlineKeyboardButton(
                alias,
                callback_data=f"alias_exc_{alias}"
            )
            markup.add(btn)
        
        markup.add(types.InlineKeyboardButton(
            "⬅ back to alias list",
            callback_data="alias_cancel"
        ))
        
        self._show_alias_list(message, markup, "which alias exc", send)
    
    def _show_main_menu(self, message, send: bool = False) -> None:
        """显示主菜单"""
        markup = types.InlineKeyboardMarkup()
        buttons = [
            types.InlineKeyboardButton("exc", callback_data="alias_exc"),
            types.InlineKeyboardButton("add", callback_data="alias_add"),
            types.InlineKeyboardButton("del", callback_data="alias_del")
        ]
        markup.add(*buttons)
        
        self._show_alias_list(message, markup, "which alias option", send)
    
    def show_alias_menu(self, message) -> None:
        """显示别名主菜单（公共方法）"""
        self.logger.info(f"显示别名菜单: user_id={message.from_user.id}")
        self._show_main_menu(message, send=True)
