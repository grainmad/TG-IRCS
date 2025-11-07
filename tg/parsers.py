"""命令参数解析器模块

提供各种机器人命令的参数解析功能，将用户输入转换为结构化数据。
"""

import re
import logging
from datetime import datetime, timezone, timedelta
from typing import Dict, Any, Optional


logger = logging.getLogger(__name__)


class CommandParser:
    """命令解析器基类"""
    
    @staticmethod
    def validate_name(name: str) -> bool:
        """验证名称格式（仅支持字母、数字、下划线、中划线）"""
        return bool(re.match(r'^[a-zA-Z0-9_-]+$', name))


class CopyCommandParser(CommandParser):
    """copy命令解析器"""
    
    @staticmethod
    def parse(text: str, chat_id: int) -> Dict[str, Any]:
        """
        解析copy命令
        
        格式: /copy <name> [old_name]
        
        Args:
            text: 命令文本
            chat_id: 聊天ID
            
        Returns:
            包含cmd, name, old, chat_id的字典
            
        Raises:
            ValueError: 当参数格式不正确时
        """
        args = [i for i in text.split(' ')[1:] if i]
        
        if len(args) == 0:
            raise ValueError("未设置name参数")
        
        if not CommandParser.validate_name(args[0]):
            raise ValueError("name格式错误，仅支持字母、数字、下划线、中划线")
        
        data = {
            "cmd": "copy",
            "name": args[0],
            "old": args[1] if len(args) > 1 else "",
            "chat_id": chat_id
        }
        
        return data


class ExecCommandParser(CommandParser):
    """exec命令解析器"""
    
    @staticmethod
    def parse_timestamp(ts: str) -> int:
        """解析时间戳或日期字符串"""
        if ts.isdigit():
            return int(ts)
        
        try:
            time_format = "%Y-%m-%dT%H:%M:%S"
            dt = datetime.strptime(ts, time_format)
            tz = timezone(timedelta(hours=8))
            dt = dt.replace(tzinfo=tz)
            return int(dt.timestamp())
        except ValueError:
            return 0
    
    @staticmethod
    def parse_duration(duration: str) -> int:
        """
        解析时间段字符串
        
        支持格式: 1d2h3m4s (天、小时、分钟、秒的组合)
        
        Returns:
            秒数
        """
        if not all(ch in "dhms0123456789" for ch in duration):
            return 0
        
        total_seconds = 0
        num = 0
        
        for char in duration:
            if char.isdigit():
                num = num * 10 + int(char)
            else:
                if char == 'd':
                    total_seconds += num * 24 * 60 * 60
                elif char == 'h':
                    total_seconds += num * 60 * 60
                elif char == 'm':
                    total_seconds += num * 60
                elif char == 's':
                    total_seconds += num
                num = 0
        
        return total_seconds
    
    @staticmethod
    def parse_remain(remain: str) -> int:
        """解析剩余次数"""
        return int(remain) if remain.isdigit() else 1
    
    @staticmethod
    def parse_cron_command(text: str, chat_id: int) -> Dict[str, Any]:
        """
        解析cron格式的exec命令
        
        格式: /exec <name> cron(<cron_expr>) [remain] [taskname]
        """
        args = []
        
        # 跳过命令名称
        ns = 0
        while ns < len(text) and text[ns] != ' ':
            ns += 1
        while ns < len(text) and text[ns] == ' ':
            ns += 1
        
        # 提取第一个参数（name）
        ne = ns
        while ne < len(text) and text[ne] != ' ':
            ne += 1
        
        if ns != ne:
            args.append(text[ns:ne])
        
        # 提取cron表达式
        cs, ce = -1, -1
        for i, char in enumerate(text):
            if char == '(':
                cs = i + 1
            if char == ')':
                ce = i
        
        if cs < ce:
            args.append(text[cs:ce])
        
        # 提取剩余参数
        args.extend([i for i in text[ce+1:].split(' ') if i])
        
        if len(args) == 0 or not args[0]:
            raise ValueError("未设置name参数")
        
        if len(args) < 2 or not args[1]:
            raise ValueError("cron表达式错误")
        
        data = {
            "cmd": "exec",
            "name": args[0],
            "start": 0,
            "delay": 0,
            "freq": 0,
            "cron": args[1],
            "remain": ExecCommandParser.parse_remain(args[2]) if len(args) > 2 and args[2] else 1,
            "taskname": args[3] if len(args) > 3 and args[3] and len(args[3]) < 128 else "",
            "chat_id": chat_id
        }
        
        return data
    
    @staticmethod
    def parse(text: str, chat_id: int) -> Dict[str, Any]:
        """
        解析exec命令
        
        格式1: /exec <name> [start_time] [freq] [remain] [taskname]
        格式2: /exec <name> cron(<cron_expr>) [remain] [taskname]
        
        Args:
            text: 命令文本
            chat_id: 聊天ID
            
        Returns:
            包含执行参数的字典
            
        Raises:
            ValueError: 当参数格式不正确时
        """
        # 检查是否为cron格式
        if "cron(" in text:
            return ExecCommandParser.parse_cron_command(text, chat_id)
        
        # 普通格式解析
        args = [i for i in text.split(' ')[1:] if i]
        
        if len(args) == 0 or not args[0]:
            raise ValueError("未设置name参数")
        
        data = {
            "cmd": "exec",
            "name": args[0],
            "start": 0,
            "delay": 0,
            "freq": 0,
            "cron": "",
            "remain": 1,
            "taskname": "",
            "chat_id": chat_id
        }
        
        if len(args) > 1 and args[1]:
            data["start"] = ExecCommandParser.parse_timestamp(args[1])
            data["delay"] = ExecCommandParser.parse_duration(args[1])
        
        if len(args) > 2 and args[2]:
            data["freq"] = ExecCommandParser.parse_duration(args[2])
        
        if len(args) > 3 and args[3]:
            data["remain"] = ExecCommandParser.parse_remain(args[3])
        
        if len(args) > 4 and args[4] and len(args[4]) < 128:
            data["taskname"] = args[4]
        
        return data


class SimpleCommandParser(CommandParser):
    """简单命令解析器（用于terminate等命令）"""
    
    @staticmethod
    def parse_with_args(cmd: str, text: str, chat_id: int, arg_name: str) -> Dict[str, Any]:
        """
        解析带参数的简单命令
        
        Args:
            cmd: 命令名称
            text: 命令文本
            chat_id: 聊天ID
            arg_name: 参数名称
            
        Returns:
            包含cmd和参数的字典
        """
        return {
            "cmd": cmd,
            arg_name: " ".join(text.split(' ')[1:]),
            "chat_id": chat_id
        }
    
    @staticmethod
    def parse_no_args(cmd: str, chat_id: int) -> Dict[str, Any]:
        """
        解析无参数的简单命令
        
        Args:
            cmd: 命令名称
            chat_id: 聊天ID
            
        Returns:
            包含cmd的字典
        """
        return {
            "cmd": cmd,
            "chat_id": chat_id
        }


class TaskCommandParser(CommandParser):
    """task命令解析器"""
    
    @staticmethod
    def parse(text: str, chat_id: int) -> Dict[str, Any]:
        """
        解析task命令
        
        格式: /task <taskid>
        
        Args:
            text: 命令文本
            chat_id: 聊天ID
            
        Returns:
            包含cmd, id, chat_id的字典
            
        Raises:
            ValueError: 当未设置taskid时
        """
        args = [i for i in text.split(' ')[1:] if i]
        
        if len(args) == 0 or not args[0]:
            raise ValueError("未设置taskid参数")
        
        return {
            "cmd": "task",
            "id": args[0],
            "chat_id": chat_id
        }
